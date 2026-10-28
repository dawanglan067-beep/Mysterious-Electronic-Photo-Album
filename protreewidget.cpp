#include "duplicatereviewdialog.h"
#include "albumloadcontroller.h"
#include "protreewidget.h"
#include "albumfolderpicker.h"
#include "materialimportservice.h"
#include "phototrashservice.h"
#include <QSignalBlocker>
#include "duplicatephotofinder.h"
#include <QDir>
#include <QIcon>
#include <QHeaderView>
#include "const.h"
#include "protreeitem.h"
#include <QGuiApplication>
#include <QMenu>
#include "workspacesettings.h"
#include <QTimer>
#include <QDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QFile>
#include <QStandardPaths>
#include "removeprodialog.h"
#include "slideshowdlg.h"

namespace {

QString MaterialCanonicalPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool MaterialPathsEqual(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

bool MaterialInsideAlbum(const QString &path, const QString &root)
{
    const QString prefix = root.endsWith('/') ? root : root + '/';
#ifdef Q_OS_WIN
    return path.startsWith(prefix, Qt::CaseInsensitive);
#else
    return path.startsWith(prefix);
#endif
}

// 检查给定的绝对路径是否属于操作系统受保护的系统级目录或用户核心个人资料库（坚决拦截本地物理删除）
bool IsSystemProtectedDirectory(const QString &rawPath)
{
    if (rawPath.trimmed().isEmpty()) return true;

    QString clean = QDir::cleanPath(rawPath);
#ifdef Q_OS_WIN
    const auto cs = Qt::CaseInsensitive;
#else
    const auto cs = Qt::CaseSensitive;
#endif

    // 根目录保护：如 "C:", "C:/", "/", "D:/" 等
    if (clean == "/" || (clean.length() <= 3 && clean.endsWith(':')) || clean.endsWith(":/"))
    {
        return true;
    }

    // 用户核心个人库保护黑名单
    QStringList protectedList = {
        QDir::cleanPath(QDir::homePath()),                                                    // 用户家目录 (如 C:/Users/Kronos)
        QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)),  // 我的图片
        QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)),   // 桌面
        QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)), // 文档
        QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)),  // 下载
        QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::MusicLocation)),     // 音乐
        QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)),    // 视频
        QDir::cleanPath(QCoreApplication::applicationDirPath())                               // 本程序执行路径
    };

    // 系统截图专属路径 (Pictures/Screenshots)
    QString picturesPath = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    if (!picturesPath.isEmpty())
    {
        protectedList.append(picturesPath + "/Screenshots");
    }

    for (const QString &prot : protectedList)
    {
        if (prot.isEmpty()) continue;
        // 目标路径完全匹配受保护目录，或是受保护目录的上层父目录，坚决拒绝物理删除
        if (clean.compare(prot, cs) == 0 || prot.startsWith(clean + "/", cs) || prot.startsWith(clean + "\\", cs))
        {
            return true;
        }
    }

    return false;
}

// 安全移入操作系统回收站 (安全可撤销，绝不硬删除物理扇区)
bool SendPathToRecycleBin(const QString &path)
{
    if (path.isEmpty() || !QFile::exists(path))
    {
        return false;
    }
    return QFile::moveToTrash(path);
}

} // namespace


ProTreeWidget::ProTreeWidget(QWidget *parent): QTreeWidget(parent)
    ,_active_item(nullptr),_right_btn_item(nullptr)
    ,_selecetd_item(nullptr),_load_controller(new AlbumLoadController(this))
    ,_only_favorites(false)
    ,_action_detect_dup(nullptr),_action_toggle_fav(nullptr)
{
    this->setHeaderHidden(true);
    this->setIconSize(QSize(20, 20));
    connect(this,&ProTreeWidget::itemPressed,this,&ProTreeWidget::SlotItemPressed);
    _action_import=new QAction(QIcon(":/icon/line/import.svg"),tr("导入文件"),this);
    _action_setstart=new QAction(QIcon(":/icon/line/active.svg"),tr("设置活动项目"),this);
    _action_closepro=new QAction(QIcon(":/icon/line/close.svg"),tr("关闭项目"),this);
    _action_slideshow=new QAction(QIcon(":/icon/line/slideshow.svg"),tr("轮播图播放"),this);
    _action_detect_dup=new QAction(QIcon(":/icon/line/duplicates.svg"),tr("相似/重复照片检测 (dHash)"),this);
    _action_toggle_fav=new QAction(QIcon(":/icon/line/favorite.svg"),tr("收藏/取消收藏该照片"),this);

    connect(_action_import,&QAction::triggered,this,&ProTreeWidget::SlotImport);
    connect(_action_setstart,&QAction::triggered,this,&ProTreeWidget::SlotSetAvtive);
    connect(_action_closepro,&QAction::triggered,this,&ProTreeWidget::SlotColsePro);
    connect(this,&ProTreeWidget::itemDoubleClicked,this,&ProTreeWidget::SlotDoubleClickItem);
    connect(_action_slideshow,&QAction::triggered,this,&ProTreeWidget::SlotSlideShow);
    connect(_action_detect_dup,&QAction::triggered,this,&ProTreeWidget::SlotDetectDuplicates);
    connect(_action_toggle_fav,&QAction::triggered,this,&ProTreeWidget::SlotToggleFavorite);
    connect(_load_controller, &AlbumLoadController::AlbumOpened,
            this, &ProTreeWidget::OnAlbumOpened);
    connect(_load_controller, &AlbumLoadController::ImportCompleted,
            this, &ProTreeWidget::OnImportCompleted);
    _action_trash_photo = new QAction(tr("删除图片／素材（移到回收站）"), this);
    _action_trash_photo->setObjectName("treeTrashPhotoAction");
    connect(_action_trash_photo, &QAction::triggered, this, [this]() {
        auto *photo = dynamic_cast<ProTreeItem*>(_right_btn_item);
        if (photo && photo->type() == TreeItemPic) emit SigDeletePhotosRequested({photo->GetPath()});
    });
    _action_hide_folder = new QAction(tr("隐藏文件夹（本次运行）"), this);
    _action_hide_folder->setObjectName("treeHideFolderAction");
    connect(_action_hide_folder, &QAction::triggered, this, [this]() {
        auto *folder = dynamic_cast<ProTreeItem*>(_right_btn_item);
        if (folder && folder->type() == TreeItemDir) HideFolder(folder->GetPath());
    });
    _action_trash_folder = new QAction(tr("删除文件夹（移到回收站）…"), this);
    _action_trash_folder->setObjectName("treeTrashFolderAction");
    connect(_action_trash_folder, &QAction::triggered, this, [this]() {
        auto *folder = dynamic_cast<ProTreeItem*>(_right_btn_item);
        if (folder && folder->type() == TreeItemDir) emit SigDeleteFolderRequested(folder->GetPath());
    });
    _action_show_folders = new QAction(tr("显示已隐藏文件夹（含空素材目录）"), this);
    _action_show_folders->setObjectName("treeShowFoldersAction");
    connect(_action_show_folders, &QAction::triggered, this, [this]() {
        auto *root = dynamic_cast<ProTreeItem*>(_right_btn_item);
        if (root && root->type() == TreeItemPro) ShowHiddenFolders(root->GetPath());
    });

    // 支持键盘上下方向键或单击切换当前树节点时，联动更新右侧图片预览
    connect(this, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
        emit SigPhotoSetChanged();
        // 右键只打开操作菜单，不先切走当前编辑图，避免删除确认前丢失编辑。
        if (QGuiApplication::mouseButtons() & Qt::RightButton) return;
        if (!current || current->type() != TreeItemPic || current->isHidden())
        {
            return;
        }
        auto *picItem = dynamic_cast<ProTreeItem*>(current);
        if (picItem && _selecetd_item != current)
        {
            _selecetd_item = current;
            emit SigUpdateSelected(picItem->GetPath());
        }
    });

    _music = new MusicController(this);
    connect(_music, &MusicController::SigMusicStateChanged, this, &ProTreeWidget::SigMusicStateChanged);
    connect(_music, &MusicController::SigMusicErrorOccurred, this, &ProTreeWidget::SigMusicErrorOccurred);
    connect(_music, &MusicController::SigMusicSettingsChanged, this, &ProTreeWidget::SigMusicSettingsChanged);
    connect(_music, &MusicController::SettingsDirty, this, &ProTreeWidget::SaveWorkspaceSettings);

    // 启动后自动恢复上次工作区状态（已打开相册、星标收藏、背景音乐与播放策略）
    QTimer::singleShot(200, this, &ProTreeWidget::LoadWorkspaceSettings);
}

ProTreeWidget::~ProTreeWidget()
{

}

void ProTreeWidget::AddProToTree(const QString name, const QString path)
{
    QDir dir(path);
    QString file_path = dir.absoluteFilePath(name);
    if (_set_path.find(file_path) != _set_path.end())
    {
        return;
    }

    QDir pro_dir(file_path);
    if (!pro_dir.exists())
    {
        bool enable = pro_dir.mkpath(file_path);
        if (!enable)
        {
            return;
        }
    }

    _set_path.insert(file_path);

    // 关键修正：传入 static_cast<QTreeWidget*>(nullptr)，
    // 杜绝 QTreeWidgetItem 构造时自动挂载一次、随后 addTopLevelItem 再次挂载导致同一节点指针在树中重复注册并引发双重释放/堆破坏！
    auto *item = new ProTreeItem(static_cast<QTreeWidget*>(nullptr), name, file_path, TreeItemPro);
    item->setData(0, Qt::DisplayRole, name);
    item->setData(0, Qt::DecorationRole, QIcon(":/icon/line/album.svg"));
    item->setData(0, Qt::ToolTipRole, file_path);
    this->addTopLevelItem(item);
    emit SigPhotoSetChanged();

    // 若当前尚未设置活动项目，默认将新建相册设为活动项目
    if (!_active_item)
    {
        _active_item = item;
        QFont boldFont;
        boldFont.setBold(true);
        _active_item->setFont(0, boldFont);
    }

    SaveWorkspaceSettings();
}

void ProTreeWidget::SlotItemPressed(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item)
    {
        return;
    }
    if (QGuiApplication::mouseButtons() == Qt::LeftButton)
    {
        if (item->type() == TreeItemPic)
        {
            auto *picItem = dynamic_cast<ProTreeItem*>(item);
            if (picItem && _selecetd_item != item)
            {
                _selecetd_item = item;
                emit SigUpdateSelected(picItem->GetPath());
            }
        }
        return;
    }
    if (QGuiApplication::mouseButtons() == Qt::RightButton)
    {
        QMenu menu(this);
        int itemtype = item->type();
        if (itemtype == TreeItemPro)
        {
            _right_btn_item = item;
            menu.addAction(_action_import);
            menu.addAction(_action_setstart);
            menu.addAction(_action_slideshow);
            menu.addAction(_action_detect_dup);
            menu.addAction(_action_show_folders);
            menu.addSeparator();
            menu.addAction(_action_closepro);
            menu.exec(QCursor::pos());
            _right_btn_item = nullptr;
        }
        else if (itemtype == TreeItemPic)
        {
            _right_btn_item = item;
            auto *picItem = dynamic_cast<ProTreeItem*>(item);
            if (picItem)
            {
                bool isFav = _favorite_paths.contains(picItem->GetPath());
                _action_toggle_fav->setText(isFav ? tr("取消星标收藏") : tr("设为星标收藏"));
                menu.addAction(_action_toggle_fav);
                menu.addSeparator();
                menu.addAction(_action_trash_photo);
                menu.exec(QCursor::pos());
            }
            _right_btn_item = nullptr;
        }
        else if (itemtype == TreeItemDir)
        {
            _right_btn_item = item;
            menu.addAction(_action_hide_folder);
            menu.addAction(_action_trash_folder);
            menu.exec(QCursor::pos());
            _right_btn_item = nullptr;
        }
    }
}

void ProTreeWidget::SlotDoubleClickItem(QTreeWidgetItem *doubleItem, int col)
{
    Q_UNUSED(col);
    if(QGuiApplication::mouseButtons()==Qt::LeftButton)
    {
        auto *tree_doubleItem=dynamic_cast<ProTreeItem*>(doubleItem);
        if(!tree_doubleItem)
        {
            return;
        }
        int itemtype=tree_doubleItem->type();
        if(itemtype==TreeItemPic && _selecetd_item != doubleItem)
        {
            _selecetd_item=doubleItem;
            emit SigUpdateSelected(tree_doubleItem->GetPath());
        }
    }
}

void ProTreeWidget::SlotImport()
{
    auto *requestedRoot = dynamic_cast<ProTreeItem*>(_right_btn_item);
    if (!requestedRoot || requestedRoot->type() != TreeItemPro) return;
    const QString path = requestedRoot->GetPath();
    const auto selection = SelectAlbumFolder(
        this, tr("导入照片 — 选中来源文件夹后确认，或进入目录后选择当前文件夹"), path);
    if (!selection.error.isEmpty())
        QMessageBox::warning(this, tr("导入文件"), selection.error);
    if (selection.path.isEmpty()) return;
    // The folder picker has a modal event loop; resolve the node again.
    ProTreeItem *album = nullptr;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        auto *candidate = dynamic_cast<ProTreeItem*>(topLevelItem(i));
        if (candidate && candidate->GetPath() == path) {
            album = candidate;
            break;
        }
    }
    QString error;
    if (!_load_controller->Import(selection.path, album, &error) && !error.isEmpty())
        QMessageBox::warning(this, tr("导入文件"), error);
}

void ProTreeWidget::SlotSetAvtive()
{
    if(!_right_btn_item)
    {
        return;
    }
    QFont nullFont;
    nullFont.setBold(false);
    if(_active_item)
    {
        _active_item->setFont(0,nullFont);
    }
    _active_item=_right_btn_item;
    nullFont.setBold(true);
    _active_item->setFont(0,nullFont);
    SaveWorkspaceSettings();
}

void ProTreeWidget::SlotColsePro()
{
    if(!_right_btn_item)
    {
        return;
    }

    // Workers may still refer to a root, and import cancellation must roll back.
    if (_load_controller->IsBusy())
    {
        QMessageBox::information(
            this,
            tr("无法关闭相册"),
            tr("相册正在打开或导入，请等待任务结束后再关闭。"));
        return;
    }

    RemoveProDialog remove_pro_dialog(this);
    auto res=remove_pro_dialog.exec();
    if(res!=QDialog::Accepted)
    {
        return;
    }
    bool b_remove=remove_pro_dialog.IsRemoved();
    auto index_right_btn=this->indexOfTopLevelItem(_right_btn_item);
    auto *protreeitem=dynamic_cast<ProTreeItem*>(_right_btn_item);
    auto delete_path=protreeitem ? protreeitem->GetPath() : QString();

    if (!delete_path.isEmpty())
    {
        _set_path.remove(delete_path);
    }

    if(b_remove && !delete_path.isEmpty())
    {
        // 关键安全防御 1：严禁物理删除系统核心个人目录与受保护系统目录！
        if (IsSystemProtectedDirectory(delete_path))
        {
            QMessageBox::warning(
                this,
                tr("安全防护拦截"),
                tr("检测到该相册路径属于系统重要个人目录（如系统截图/图片/桌面）：\n%1\n\n为了保障用户数据安全，系统已严正拦截本地物理删除操作，仅从相册列表中关闭！本地文件完好无损。")
                .arg(QDir::toNativeSeparators(delete_path)));
        }
        else
        {
            // 关键安全防御 2：使用操作系统回收站机制安全删除，坚决杜绝直接 removeRecursively() 硬抹除！
            bool ok = SendPathToRecycleBin(delete_path);
            if (!ok)
            {
                QMessageBox::warning(
                    this,
                    tr("移除提示"),
                    tr("尝试将文件夹移入系统回收站时遇到系统限制，本地文件未被移除。\n路径：%1")
                    .arg(QDir::toNativeSeparators(delete_path)));
            }
        }

        // 清理星标收藏集合中属于该已删除相册的图片记录
        QString cleanDel = QDir::cleanPath(delete_path);
        for (auto it = _favorite_paths.begin(); it != _favorite_paths.end(); )
        {
            if (it->startsWith(cleanDel))
            {
                it = _favorite_paths.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // 1. 安全解除当前选中与高亮项绑定，防止持有即将释放的野指针导致悬挂解引用
    if (_selecetd_item)
    {
        auto *selPro = dynamic_cast<ProTreeItem*>(_selecetd_item);
        if (selPro && (selPro == protreeitem || selPro->GetRoot() == _right_btn_item))
        {
            _selecetd_item = nullptr;
            emit SigUpdatePic(""); // 发送空路径信号清空 PicShow 展示区
        }
    }
    if (this->currentItem())
    {
        auto *curPro = dynamic_cast<ProTreeItem*>(this->currentItem());
        if (curPro && (curPro == protreeitem || curPro->GetRoot() == _right_btn_item))
        {
            this->setCurrentItem(nullptr);
        }
    }
    this->clearSelection();

    if(protreeitem==_active_item)
    {
        _active_item=nullptr;
    }

    // 严格校验有效索引后从树控件中移除，并通过 QTimer::singleShot 延后释放堆对象，
    // 坚决杜绝在 QTreeWidget::mousePressEvent 调用栈未退出时同步释放树节点导致的 0xc0000374 堆破坏与异常终止！
    if (index_right_btn >= 0)
    {
        QTreeWidgetItem *taken = this->takeTopLevelItem(index_right_btn);
        if (taken)
        {
            QTimer::singleShot(0, [taken]() {
                delete taken;
            });
        }
    }
    _right_btn_item=nullptr;

    ApplyTreeFilter();
    SaveWorkspaceSettings();
}
void ProTreeWidget::OnImportCompleted()
{
    for (int i = 0; i < topLevelItemCount(); ++i)
        RefreshItemFavoriteDisplay(topLevelItem(i));
    ApplyTreeFilter();
    if (!_selecetd_item) {
        for (int i = 0; i < topLevelItemCount(); ++i) {
            auto *topPro = dynamic_cast<ProTreeItem*>(topLevelItem(i));
            ProTreeItem *firstPic = topPro ? topPro->GetFirstPicChild() : nullptr;
            while (firstPic && firstPic->isHidden()) firstPic = firstPic->GetNextItem();
            if (firstPic) {
                _selecetd_item = firstPic;
                setCurrentItem(firstPic);
                emit SigUpdateSelected(firstPic->GetPath());
                break;
            }
        }
    }
    SaveWorkspaceSettings();
}

void ProTreeWidget::OnAlbumOpened(QTreeWidgetItem *root)
{
    auto *openedRoot = dynamic_cast<ProTreeItem*>(root);
    if (!openedRoot) return;
    _set_path.insert(openedRoot->GetPath());
    if (!_restoring_active_path.isEmpty()
        && MaterialPathsEqual(MaterialCanonicalPath(openedRoot->GetPath()),
                              MaterialCanonicalPath(_restoring_active_path))) {
        if (_active_item) {
            QFont font = _active_item->font(0);
            font.setBold(false);
            _active_item->setFont(0, font);
        }
        _active_item = openedRoot;
        QFont font = openedRoot->font(0);
        font.setBold(true);
        openedRoot->setFont(0, font);
        _restoring_active_path.clear();
    }
    if (!_active_item) {
        _active_item = openedRoot;
        QFont font = openedRoot->font(0);
        font.setBold(true);
        openedRoot->setFont(0, font);
    }
    for (int i = 0; i < topLevelItemCount(); ++i)
        RefreshItemFavoriteDisplay(topLevelItem(i));
    ApplyTreeFilter();
    setCurrentItem(openedRoot);
    if (!_selecetd_item) {
        ProTreeItem *firstPic = openedRoot->GetFirstPicChild();
        while (firstPic && firstPic->isHidden()) firstPic = firstPic->GetNextItem();
        if (firstPic && firstPic->GetRoot() == openedRoot) {
            _selecetd_item = firstPic;
            setCurrentItem(firstPic);
            emit SigUpdateSelected(firstPic->GetPath());
        }
    }
    SaveWorkspaceSettings();
    emit SigAlbumOpened(openedRoot->GetPath());
}

void ProTreeWidget::SlotSlideShow()
{
    if(!_right_btn_item)
    {
        return;
    }
    auto *right_pro_item=dynamic_cast<ProTreeItem*>(_right_btn_item);
    if (!right_pro_item)
    {
        return;
    }
    auto *first_child_item=right_pro_item->GetFirstPicChild();
    while (first_child_item && first_child_item->isHidden())
    {
        first_child_item = first_child_item->GetNextItem();
    }
    auto *last_child_item=right_pro_item->GetLastPicChild();
    while (last_child_item && last_child_item->isHidden())
    {
        last_child_item = last_child_item->GetPreItem();
    }
    if(!first_child_item || !last_child_item ||
       first_child_item->GetRoot() != right_pro_item ||
       last_child_item->GetRoot() != right_pro_item)
    {
        QMessageBox::information(this, tr("轮播图播放"), tr("当前相册（或当前搜索/星标筛选条件下）没有可播放的照片。"));
        return;
    }
    // 先销毁旧窗口，避免其析构时停止新窗口刚启动的背景音乐。
    _slide_show_dlg.reset();
    _slide_show_dlg=std::make_shared<SlideShowDlg>(this,first_child_item,last_child_item);
    _slide_show_dlg->setModal(true);
    _slide_show_dlg->showMaximized();
}

void ProTreeWidget::SlotOpenPro(const QString &path)
{
    _load_controller->Open(path);
}

// Respond to the editor next-photo action.
void ProTreeWidget::SlotNextShow()
{
    if(!_selecetd_item)
    {
        return;
    }
    // 获取当前图片节点在双向链表中的后继节点（自动跳过被搜索/星标过滤隐藏的节点）
    auto *curItem=dynamic_cast<ProTreeItem*>(_selecetd_item)->GetNextItem();
    while(curItem && curItem->isHidden())
    {
        curItem = curItem->GetNextItem();
    }
    if(!curItem)
    {
        return; // 已经到达相册末尾，没有下一张可见图片
    }
    // 发送图片路径更新信号，通知 PicShow 加载并显示下一张
    emit SigUpdatePic(curItem->GetPath());
    _selecetd_item=curItem;
    this->setCurrentItem(curItem); // 同步树目录中的当前高亮选中项
}

// 响应 PicShow 界面点击“上一张”按钮的槽函数
void ProTreeWidget::SlotPreShow()
{
    if(!_selecetd_item)
    {
        return;
    }
    // 获取当前图片节点在双向链表中的前驱节点（自动跳过被搜索/星标过滤隐藏的节点）
    auto *curItem=dynamic_cast<ProTreeItem*>(_selecetd_item)->GetPreItem();
    while(curItem && curItem->isHidden())
    {
        curItem = curItem->GetPreItem();
    }
    if(!curItem)
    {
        return; // 已经到达相册起始，没有上一张可见图片
    }
    // 发送图片路径更新信号，通知 PicShow 加载并显示上一张
    emit SigUpdatePic(curItem->GetPath());
    _selecetd_item=curItem;
    this->setCurrentItem(curItem); // 同步树目录中的当前高亮选中项
}

// ======================== 智能检索、星标收藏与工作区持久化 ========================

void ProTreeWidget::SlotToggleFavorite()
{
    if (!_right_btn_item || _right_btn_item->type() != TreeItemPic)
    {
        return;
    }
    auto *picItem = dynamic_cast<ProTreeItem*>(_right_btn_item);
    if (!picItem)
    {
        return;
    }
    QString path = picItem->GetPath();
    if (_favorite_paths.contains(path))
    {
        _favorite_paths.remove(path);
    }
    else
    {
        _favorite_paths.insert(path);
    }
    RefreshItemFavoriteDisplay(picItem);
    ApplyTreeFilter();
    SaveWorkspaceSettings();
}

void ProTreeWidget::RefreshItemFavoriteDisplay(QTreeWidgetItem *item)
{
    if (!item) return;
    if (item->type() == TreeItemPic)
    {
        auto *picItem = dynamic_cast<ProTreeItem*>(item);
        if (picItem)
        {
            QFileInfo fi(picItem->GetPath());
            bool isFav = _favorite_paths.contains(picItem->GetPath());
            item->setData(0, Qt::DisplayRole, fi.fileName());
            item->setIcon(0, QIcon(isFav ? ":/icon/line/favorite.svg" : ":/icon/line/core.svg"));
        }
    }
    for (int i = 0; i < item->childCount(); ++i)
    {
        RefreshItemFavoriteDisplay(item->child(i));
    }
}

void ProTreeWidget::SlotSearchTextChanged(const QString &text)
{
    _search_keyword = text.trimmed();
    ApplyTreeFilter();
}

void ProTreeWidget::SlotSetOnlyFavorites(bool onlyFav)
{
    _only_favorites = onlyFav;
    ApplyTreeFilter();
}

void ProTreeWidget::ApplyTreeFilter()
{
    for (int i = 0; i < this->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *topItem = this->topLevelItem(i);
        FilterItemRecursive(topItem);
        topItem->setHidden(false); // 顶层相册项目始终可见
    }
    emit SigPhotoSetChanged();
}

QString ProTreeWidget::CurrentAlbumRootPath() const
{
    auto *item = currentItem();
    if (!item) item = _active_item;
    if (!item && topLevelItemCount() > 0) item = topLevelItem(0);
    while (item && item->parent()) item = item->parent();
    auto *root = dynamic_cast<ProTreeItem*>(item);
    return root ? root->GetPath() : QString();
}

QString ProTreeWidget::AlbumRootForPhoto(const QString &path) const
{
    if (path.isEmpty()) return {};
    const QString normalized = MaterialCanonicalPath(path);
    QTreeWidgetItemIterator it(const_cast<ProTreeWidget*>(this));
    while (*it) {
        auto *photo = dynamic_cast<ProTreeItem*>(*it);
        if (photo && photo->type() == TreeItemPic
            && MaterialPathsEqual(MaterialCanonicalPath(photo->GetPath()), normalized)) {
            auto *root = dynamic_cast<ProTreeItem*>(photo->GetRoot());
            return root ? root->GetPath() : QString();
        }
        ++it;
    }
    return {};
}

bool ProTreeWidget::TrashPhoto(const QString &path, QString *error)
{
    return TrashPath(path, false, error);
}

bool ProTreeWidget::TrashFolder(const QString &path, QString *error)
{
    return TrashPath(path, true, error);
}

QStringList ProTreeWidget::FolderPhotoPaths(const QString &path) const
{
    QStringList paths;
    const QString folder = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QTreeWidgetItemIterator it(const_cast<ProTreeWidget*>(this));
    while (*it) {
        auto *photo = dynamic_cast<ProTreeItem*>(*it);
        if (photo && photo->type() == TreeItemPic
            && MaterialInsideAlbum(QDir::cleanPath(QFileInfo(photo->GetPath()).absoluteFilePath()), folder))
            paths.append(photo->GetPath());
        ++it;
    }
    paths.removeDuplicates();
    return paths;
}

void ProTreeWidget::HideFolder(const QString &path)
{
    _hidden_folder_paths.insert(MaterialCanonicalPath(path));
    ApplyTreeFilter();
}

void ProTreeWidget::ShowHiddenFolders(const QString &albumRoot)
{
    const QString root = MaterialCanonicalPath(albumRoot);
    for (auto it = _hidden_folder_paths.begin(); it != _hidden_folder_paths.end();) {
        if (MaterialInsideAlbum(*it, root)) it = _hidden_folder_paths.erase(it);
        else ++it;
    }
    _show_empty_material_roots.insert(root);
    ApplyTreeFilter();
}

bool ProTreeWidget::TrashPath(const QString &path, bool folder, QString *error)
{
    if (error) error->clear();
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (_load_controller->IsBusy()
        || (_slide_show_dlg && _slide_show_dlg->isVisible()))
        return fail(tr("相册正在加载、导入或轮播，请结束后再删除。"));
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (folder) {
        if (IsSystemProtectedDirectory(absolute)) return fail(tr("不能删除受保护目录。"));
        const QString canonicalFolder = MaterialCanonicalPath(absolute);
        for (int i = 0; i < topLevelItemCount(); ++i) {
            auto *opened = dynamic_cast<ProTreeItem*>(topLevelItem(i));
            if (!opened) continue;
            const QString openedPath = MaterialCanonicalPath(opened->GetPath());
            if (MaterialPathsEqual(openedPath, canonicalFolder) || MaterialInsideAlbum(openedPath, canonicalFolder))
                return fail(tr("该目录本身或其子目录已作为独立相册打开，请先关闭对应相册。"));
        }
    }
    QList<ProTreeItem*> targets;
    QTreeWidgetItemIterator it(this);
    while (*it) {
        auto *photo = dynamic_cast<ProTreeItem*>(*it);
        if (photo && photo->type() == (folder ? TreeItemDir : TreeItemPic)
            && MaterialPathsEqual(QDir::cleanPath(QFileInfo(photo->GetPath()).absoluteFilePath()), absolute))
            targets.append(photo);
        ++it;
    }
    if (targets.isEmpty()) return fail(tr("图片已不在已打开的相册中，请刷新后重试。"));
    auto *root = dynamic_cast<ProTreeItem*>(targets.first()->GetRoot());
    if (!root) return fail(tr("无法确定图片所属相册。"));
    const auto result = folder ? PhotoTrashService::moveFolderToTrash(path, root->GetPath())
                               : PhotoTrashService::moveToTrash(path, root->GetPath());
    if (!result.success) return fail(result.error);

    // 回收站移动成功后才修改节点；鼠标菜单调用栈内只摘除，稍后释放。
    const auto removed = [&targets](QTreeWidgetItem *item) {
        for (; item; item = item->parent())
            if (targets.contains(dynamic_cast<ProTreeItem*>(item))) return true;
        return false;
    };
    const bool selectedRemoved = removed(_selecetd_item);
    const bool currentRemoved = removed(currentItem());
    ProTreeItem *replacement = nullptr;
    if (selectedRemoved || currentRemoved) {
        QList<ProTreeItem*> photos;
        CollectPicItemsRecursive(root, photos);
        for (auto *photo : photos) {
            if (!removed(photo) && !photo->isHidden()) { replacement = photo; break; }
        }
    }
    QSet<ProTreeItem*> roots;
    {
        const QSignalBlocker blocker(this);
        if (removed(_right_btn_item)) _right_btn_item = nullptr;
        if (removed(_active_item)) _active_item = nullptr;
        for (auto *photo : targets) {
            if (auto *owner = dynamic_cast<ProTreeItem*>(photo->GetRoot())) roots.insert(owner);
            QList<ProTreeItem*> removedPhotos;
            if (folder) CollectPicItemsRecursive(photo, removedPhotos);
            else removedPhotos.append(photo);
            for (auto *removedPhoto : removedPhotos) _favorite_paths.remove(removedPhoto->GetPath());
            if (photo->parent()) photo->parent()->takeChild(photo->parent()->indexOfChild(photo));
            QTimer::singleShot(0, [photo]() { delete photo; });
        }
        if (selectedRemoved) _selecetd_item = replacement;
        if (currentRemoved) setCurrentItem(replacement ? static_cast<QTreeWidgetItem*>(replacement) : root);
        for (auto *owner : roots) {
            QList<ProTreeItem*> photos;
            CollectPicItemsRecursive(owner, photos);
            for (int i = 0; i < photos.size(); ++i) {
                photos[i]->SetPreItem(i ? photos[i-1] : nullptr);
                photos[i]->SetNextItem(i+1 < photos.size() ? photos[i+1] : nullptr);
            }
        }
    }
    if (selectedRemoved) emit SigUpdatePic(replacement ? replacement->GetPath() : QString());
    ApplyTreeFilter();
    SaveWorkspaceSettings();
    return true;
}

bool ProTreeWidget::RegisterMaterial(const QString &albumRoot, const QString &savedPath,
                                    QString *registeredPath, QString *error)
{
    if (registeredPath) registeredPath->clear();
    if (error) error->clear();
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (_load_controller->IsBusy())
        return fail(tr("相册正在加载或导入，请完成后再加入素材。"));
    if (albumRoot.isEmpty() || !QFileInfo(albumRoot).isDir())
        return fail(tr("目标相册目录不存在。"));
    const QString canonicalRoot = MaterialCanonicalPath(albumRoot);
    ProTreeItem *root = nullptr;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        auto *item = dynamic_cast<ProTreeItem*>(topLevelItem(i));
        if (item && MaterialPathsEqual(MaterialCanonicalPath(item->GetPath()), canonicalRoot)) {
            root = item;
            break;
        }
    }
    if (!root) return fail(tr("目标相册已经关闭，请重新打开后加入素材。"));
    const auto prepared = MaterialImportService::prepare(savedPath, root->GetPath());
    if (!prepared.success) return fail(prepared.error);
    const QString target = prepared.albumPath;
    // 同一路径去重也要恢复显示，不能只在新建节点时恢复隐藏目录。
    for (QString parentPath = QFileInfo(target).absolutePath(); MaterialInsideAlbum(parentPath, canonicalRoot);
         parentPath = QFileInfo(parentPath).absolutePath())
        _hidden_folder_paths.remove(MaterialCanonicalPath(parentPath));
    QList<ProTreeItem*> photos;
    CollectPicItemsRecursive(root, photos);
    for (auto *photo : photos) {
        if (MaterialPathsEqual(MaterialCanonicalPath(photo->GetPath()), target)) {
            if (registeredPath) *registeredPath = photo->GetPath();
            ApplyTreeFilter(); // 同路径文件也可能被重新保存，需要刷新缩略图。
            return true;
        }
    }
    const QString relative = QDir(canonicalRoot).relativeFilePath(target);
    const auto components = relative.split('/', Qt::SkipEmptyParts);
    QTreeWidgetItem *parent = root;
    QString directory = root->GetPath();
    for (int i = 0; i + 1 < components.size(); ++i) {
        directory = QDir(directory).filePath(components[i]);
        QTreeWidgetItem *found = nullptr;
        for (int j = 0; j < parent->childCount(); ++j) {
            auto *child = dynamic_cast<ProTreeItem*>(parent->child(j));
            if (child && child->type() == TreeItemDir
                && MaterialPathsEqual(MaterialCanonicalPath(child->GetPath()), MaterialCanonicalPath(directory))) {
                found = child;
                break;
            }
        }
        if (!found) {
            found = new ProTreeItem(parent, components[i], directory, root, TreeItemDir);
            found->setText(0, components[i]);
            found->setIcon(0, QIcon(":/icon/line/dir.svg"));
        }
        parent = found;
    }
    const QString nodePath = QDir(directory).filePath(components.last());
    auto *photo = new ProTreeItem(parent, components.last(), nodePath, root, TreeItemPic);
    photo->setIcon(0, QIcon(":/icon/line/core.svg"));
    photos.clear();
    CollectPicItemsRecursive(root, photos);
    for (int i = 0; i < photos.size(); ++i) {
        photos[i]->SetPreItem(i ? photos[i-1] : nullptr);
        photos[i]->SetNextItem(i+1 < photos.size() ? photos[i+1] : nullptr);
    }
    RefreshItemFavoriteDisplay(root);
    ApplyTreeFilter();
    SaveWorkspaceSettings();
    if (registeredPath) *registeredPath = nodePath;
    return true;
}

QList<PhotoEntry> ProTreeWidget::VisiblePhotos(const QString &albumRootPath) const
{
    QList<PhotoEntry> photos;
    const auto collect = [&photos](auto &&self, QTreeWidgetItem *item) -> void {
        if (!item || item->isHidden()) return;
        if (item->type() == TreeItemPic) {
            if (auto *photo = dynamic_cast<ProTreeItem*>(item)) photos.append({photo->GetPath(), QString()});
        }
        for (int i = 0; i < item->childCount(); ++i) self(self, item->child(i));
    };
    for (int i = 0; i < topLevelItemCount(); ++i) {
        auto *root = dynamic_cast<ProTreeItem*>(topLevelItem(i));
        if (root && root->GetPath() == albumRootPath) collect(collect, root);
    }
    return photos;
}

bool ProTreeWidget::SelectPhotoByPath(const QString &path)
{
    QTreeWidgetItemIterator it(this);
    while (*it) {
        auto *photo = dynamic_cast<ProTreeItem*>(*it);
        if (photo && photo->type() == TreeItemPic && !photo->isHidden() && photo->GetPath() == path) {
            const bool alreadySelected = _selecetd_item == photo;
            setCurrentItem(photo);
            scrollToItem(photo);
            _selecetd_item = photo;
            if (alreadySelected) emit SigUpdateSelected(path);
            return true;
        }
        ++it;
    }
    return false;
}

bool ProTreeWidget::FilterItemRecursive(QTreeWidgetItem *item)
{
    if (!item) return false;

    if (item->type() == TreeItemDir) {
        auto *directory = static_cast<ProTreeItem*>(item);
        const QString path = MaterialCanonicalPath(directory->GetPath());
        const auto *root = static_cast<ProTreeItem*>(directory->GetRoot());
        const QFileInfo info(directory->GetPath());
        const bool emptyMaterial = info.fileName() == tr("素材") && info.isDir()
            && !info.isSymLink() && !info.isShortcut()
            && QDir(info.absoluteFilePath()).isReadable()
            && !_show_empty_material_roots.contains(MaterialCanonicalPath(root->GetPath()))
            && QDir(info.absoluteFilePath()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        if (_hidden_folder_paths.contains(path) || emptyMaterial) {
            // 子图片也显式隐藏，照片墙及前后导航共用同一可见性。
            const auto hide = [](auto &&self, QTreeWidgetItem *node) -> void {
                node->setHidden(true);
                for (int i = 0; i < node->childCount(); ++i) self(self, node->child(i));
            };
            hide(hide, item);
            return false;
        }
    }

    if (item->type() == TreeItemPic)
    {
        auto *picItem = dynamic_cast<ProTreeItem*>(item);
        QString fileName = QFileInfo(picItem ? picItem->GetPath() : "").fileName();
        bool matchKeyword = _search_keyword.isEmpty() ||
                            fileName.contains(_search_keyword, Qt::CaseInsensitive);
        bool matchFav = !_only_favorites ||
                        (picItem && _favorite_paths.contains(picItem->GetPath()));
        bool visible = matchKeyword && matchFav;
        item->setHidden(!visible);
        return visible;
    }

    bool anyChildVisible = false;
    for (int i = 0; i < item->childCount(); ++i)
    {
        if (FilterItemRecursive(item->child(i)))
        {
            anyChildVisible = true;
        }
    }

    if (item->type() == TreeItemDir)
    {
        bool noFilter = _search_keyword.isEmpty() && !_only_favorites;
        item->setHidden(!noFilter && !anyChildVisible);
        if (!noFilter && anyChildVisible)
        {
            item->setExpanded(true);
        }
    }
    else if (item->type() == TreeItemPro && (!_search_keyword.isEmpty() || _only_favorites) && anyChildVisible)
    {
        item->setExpanded(true);
    }

    return anyChildVisible;
}

void ProTreeWidget::SaveWorkspaceSettings()
{
    QVariantMap settings;
    QStringList openedList(_set_path.begin(), _set_path.end());
    QStringList favList(_favorite_paths.begin(), _favorite_paths.end());
    settings.insert("opened_projects", openedList);
    settings.insert("favorite_photos", favList);
    auto *activePro = dynamic_cast<ProTreeItem*>(_active_item);
    settings.insert("active_project", activePro ? activePro->GetPath() : QString());
    _music->WriteSettings(settings);
    WorkspaceSettings::Save(settings, "AlbumStudio");
}

void ProTreeWidget::LoadWorkspaceSettings()
{
    QVariantMap settings = WorkspaceSettings::Load("AlbumStudio");
    QStringList favList = settings.value("favorite_photos").toStringList();
    _favorite_paths = QSet<QString>(favList.begin(), favList.end());
    QString savedActivePath = settings.value("active_project").toString();
    _restoring_active_path = savedActivePath;
    const QStringList openedList = settings.value("opened_projects").toStringList();

    _music->ReadSettings(settings);

    for (const QString &proPath : openedList)
    {
        QDir dir(proPath);
        if (dir.exists() && !_set_path.contains(proPath))
        {
            SlotOpenPro(proPath);
        }
    }

    // 恢复上次设置的活动项目状态（加粗高亮）
    if (!savedActivePath.isEmpty())
    {
        for (int i = 0; i < this->topLevelItemCount(); ++i)
        {
            auto *topPro = dynamic_cast<ProTreeItem*>(this->topLevelItem(i));
            if (topPro && topPro->GetPath() == savedActivePath)
            {
                QFont f;
                if (_active_item && _active_item != topPro)
                {
                    f.setBold(false);
                    _active_item->setFont(0, f);
                }
                _active_item = topPro;
                f.setBold(true);
                _active_item->setFont(0, f);
                break;
            }
        }
    }
    SaveWorkspaceSettings();
}

// ======================== 64-bit 差异感知哈希 (dHash) 相似图像检测算法 ========================

// 计算图像 64-bit 差异感知哈希值 (Difference Hash, dHash)：
// 1. 将图像缩放为 9x8 像素矩阵（共 8 行，每行 9 列）并转为灰度值
// 2. 比较每行相邻像素 I(x, y) > I(x+1, y)，生成 8x8 = 64 位二进制纹理梯度指纹
void ProTreeWidget::CollectPicItemsRecursive(QTreeWidgetItem *item, QList<ProTreeItem*> &outList)
{
    if (!item) return;
    if (item->type() == TreeItemPic)
    {
        auto *picItem = dynamic_cast<ProTreeItem*>(item);
        if (picItem)
        {
            outList.append(picItem);
        }
    }
    for (int i = 0; i < item->childCount(); ++i)
    {
        CollectPicItemsRecursive(item->child(i), outList);
    }
}

void ProTreeWidget::SlotDetectDuplicates()
{
    QList<ProTreeItem*> picItems;
    // 仅当由相册右键菜单直接触发时才限定扫描该右键相册；菜单栏/工具栏按钮触发时扫描全部已打开相册
    if (sender() == _action_detect_dup && _right_btn_item && _right_btn_item->type() == TreeItemPro)
    {
        CollectPicItemsRecursive(_right_btn_item, picItems);
    }
    else
    {
        for (int i = 0; i < this->topLevelItemCount(); ++i)
        {
            CollectPicItemsRecursive(this->topLevelItem(i), picItems);
        }
    }

    if (picItems.size() < 2)
    {
        QMessageBox::information(this, tr("相似照片检测 (dHash)"),
                                 tr("当前相册中的图片数量不足 2 张，请先打开或导入包含多张照片的相册项目。"));
        return;
    }

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);

    QVector<DuplicatePhotoFinder::PhotoHash> fingerprints;
    fingerprints.reserve(picItems.size());
    for (const auto *item : picItems) {
        const QString path = item->GetPath();
        if (QFileInfo::exists(path))
            fingerprints.append({path, QString::number(reinterpret_cast<quintptr>(item->GetRoot())),
                                 DuplicatePhotoFinder::ComputeDHash(path)});
    }
    const int kHammingThreshold = 10;
    const auto matches = DuplicatePhotoFinder::FindMatches(fingerprints, kHammingThreshold);

    QGuiApplication::restoreOverrideCursor();

    if (matches.isEmpty())
    {
        QMessageBox::information(this, tr("感知哈希 (dHash) 扫描完成"),
                                 tr("已扫描 %1 张照片的 64-bit 差异感知哈希指纹：\n\n未检测到重复或高度相似的照片（汉明距离阈值 ≤ %2）。")
                                     .arg(fingerprints.size())
                                     .arg(kHammingThreshold));
        return;
    }

    DuplicateReviewDialog dialog(fingerprints, matches, this);
    if (dialog.exec() == QDialog::Accepted) emit SigDeletePhotosRequested(dialog.SelectedPaths());
}


bool ProTreeWidget::IsMusicPlaying() const { return _music->IsMusicPlaying(); }
void ProTreeWidget::SlotSetMusic() { _music->SlotSetMusic(); }
void ProTreeWidget::SlotSelectMusicFile() { _music->SlotSelectMusicFile(); }
void ProTreeWidget::SlotStartMusic() { _music->SlotStartMusic(); }
void ProTreeWidget::SlotPauseMusic() { _music->SlotPauseMusic(); }
void ProTreeWidget::SlotStopMusic() { _music->SlotStopMusic(); }
void ProTreeWidget::SlotToggleMusic() { _music->SlotToggleMusic(); }
void ProTreeWidget::SlotSlideShowStartMusic() { _music->SlotSlideShowStartMusic(); }
void ProTreeWidget::SlotSlideShowStopMusic() { _music->SlotSlideShowStopMusic(); }
void ProTreeWidget::SetPlayImmediately(bool value) { _music->SetPlayImmediately(value); }
void ProTreeWidget::SetAutoPlayOnSlideshow(bool value) { _music->SetAutoPlayOnSlideshow(value); }
void ProTreeWidget::SetAutoStopOnSlideshowExit(bool value) { _music->SetAutoStopOnSlideshowExit(value); }
void ProTreeWidget::SetMusicVolume(int value) { _music->SetMusicVolume(value); }
QString ProTreeWidget::SniffAudioFileFormat(const QString &path, QString *reason) { return AudioFileProbe::SniffAudioFileFormat(path, reason); }
bool ProTreeWidget::IsAudioFileValid(const QString &path, QString *reason) { return AudioFileProbe::IsAudioFileValid(path, reason); }
