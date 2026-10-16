#include "protreewidget.h"
#include <QDir>
#include <QIcon>
#include <QHeaderView>
#include "const.h"
#include "protreeitem.h"
#include <QGuiApplication>
#include <QMenu>
#include <QFileDialog>
#include <qprogressdialog.h>
#include <QSettings>
#include <QTimer>
#include <QScopedValueRollback>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QFrame>
#include <QListWidget>
#include <QMessageBox>
#include <QFileInfo>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QStandardPaths>
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#ifdef small
#undef small
#endif
#include "removeprodialog.h"
#include "slideshowdlg.h"

namespace {

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

// 静态 MCI 别名常量
static const wchar_t* kMciAlias = L"album_bgm_channel";

ProTreeWidget::ProTreeWidget(QWidget *parent): QTreeWidget(parent)
    ,_active_item(nullptr),_right_btn_item(nullptr),_open_progressdlg(nullptr),_dialog_progress(nullptr)
    ,_selecetd_item(nullptr),_thread_create_pro(nullptr),_thread_open_pro(nullptr)
    ,_only_favorites(false),_music_volume(80),_play_immediately(true)
    ,_auto_play_on_slideshow(true),_auto_stop_on_slideshow_exit(true)
    ,_is_native_playing(false),_is_native_paused(false)
    ,_action_detect_dup(nullptr),_action_toggle_fav(nullptr)
{
    this->setHeaderHidden(true);
    this->setIconSize(QSize(20, 20));
    connect(this,&ProTreeWidget::itemPressed,this,&ProTreeWidget::SlotItemPressed);
    _action_import=new QAction(QIcon(":/icon/import.png"),tr("导入文件"),this);
    _action_setstart=new QAction(QIcon(":/icon/core.png"),tr("设置活动项目"),this);
    _action_closepro=new QAction(QIcon(":/icon/close.png"),tr("关闭项目"),this);
    _action_slideshow=new QAction(QIcon(":/icon/slideshow.png"),tr("轮播图播放"),this);
    _action_detect_dup=new QAction(QIcon(":/icon/setting.png"),tr("🧹 相似/重复照片检测 (dHash)"),this);
    _action_toggle_fav=new QAction(QIcon(":/icon/core.png"),tr("⭐ 收藏/取消收藏该照片"),this);

    connect(_action_import,&QAction::triggered,this,&ProTreeWidget::SlotImport);
    connect(_action_setstart,&QAction::triggered,this,&ProTreeWidget::SlotSetAvtive);
    connect(_action_closepro,&QAction::triggered,this,&ProTreeWidget::SlotColsePro);
    connect(this,&ProTreeWidget::itemDoubleClicked,this,&ProTreeWidget::SlotDoubleClickItem);
    connect(_action_slideshow,&QAction::triggered,this,&ProTreeWidget::SlotSlideShow);
    connect(_action_detect_dup,&QAction::triggered,this,&ProTreeWidget::SlotDetectDuplicates);
    connect(_action_toggle_fav,&QAction::triggered,this,&ProTreeWidget::SlotToggleFavorite);

    // 支持键盘上下方向键或单击切换当前树节点时，联动更新右侧图片预览
    connect(this, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
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

    _player = new QMediaPlayer(this);
    _audio_output = new QAudioOutput(this);
    _audio_output->setMuted(false);
    _audio_output->setVolume(_music_volume / 100.0f);
    _player->setAudioOutput(_audio_output);

    // 监听播放状态流转（确保跨音频工作线程安全排队到 GUI 主线程）
    connect(_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        emit SigMusicStateChanged(state == QMediaPlayer::PlayingState);
    }, Qt::QueuedConnection);

    // 核心容错与可观测性：监听媒体底层错误，消灭静默失败（线程安全排队）
    connect(_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &errorString) {
        QString errDetail;
        switch (error)
        {
        case QMediaPlayer::FormatError:
            errDetail = tr("音频格式不支持或解码失败（若文件为视频或扩展名与实际格式不符，底层系统解码器将拒绝播放）: %1").arg(errorString);
            break;
        case QMediaPlayer::ResourceError:
            errDetail = tr("无法打开音频资源或文件已被删除/占用: %1").arg(errorString);
            break;
        case QMediaPlayer::NetworkError:
            errDetail = tr("网络音频传输错误: %1").arg(errorString);
            break;
        case QMediaPlayer::AccessDeniedError:
            errDetail = tr("访问权限不足，无法读取音频文件: %1").arg(errorString);
            break;
        default:
            errDetail = tr("音频播放出错 (错误码 %1): %2").arg(QString::number(static_cast<int>(error)), errorString);
            break;
        }
        _last_music_error = errDetail;
        qWarning() << "[MusicPlayer] Error occurred:" << error << errorString << "Detail:" << errDetail;
        emit SigMusicErrorOccurred(errDetail);
        emit SigMusicStateChanged(false);
    }, Qt::QueuedConnection);

    // 监听媒体加载状态（线程安全排队）
    connect(_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia)
        {
            _last_music_error.clear();
        }
        else if (status == QMediaPlayer::InvalidMedia)
        {
            if (_last_music_error.isEmpty())
            {
                _last_music_error = tr("无效的媒体源（文件损坏或格式非法）");
            }
            qWarning() << "[MusicPlayer] MediaStatus: InvalidMedia for path:" << _music_path;
        }
    }, Qt::QueuedConnection);

    // 启动后自动恢复上次工作区状态（已打开相册、星标收藏、背景音乐与播放策略）
    QTimer::singleShot(200, this, &ProTreeWidget::LoadWorkspaceSettings);
}

ProTreeWidget::~ProTreeWidget()
{
    NativeMciClose();
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
    item->setData(0, Qt::DecorationRole, QIcon(":/icon/dir.png"));
    item->setData(0, Qt::ToolTipRole, file_path);
    this->addTopLevelItem(item);

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
                _action_toggle_fav->setText(isFav ? tr("⭐ 取消星标收藏") : tr("⭐ 设为星标收藏"));
                menu.addAction(_action_toggle_fav);
                menu.exec(QCursor::pos());
            }
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
    QFileDialog file_dialog;
    file_dialog.setFileMode(QFileDialog::Directory);
    file_dialog.setWindowTitle(tr("选择导入的文件夹"));
    QString path = "";
    if (!_right_btn_item)
    {
        qDebug() << "_right_btn_item is empty!";
        return;
    }
    QTreeWidgetItem *target_pro_item = _right_btn_item;
    path = dynamic_cast<ProTreeItem*>(target_pro_item)->GetPath();
    file_dialog.setDirectory(path);
    file_dialog.setViewMode(QFileDialog::Detail);

    QStringList fileNames;
    if (file_dialog.exec())
    {
        fileNames = file_dialog.selectedFiles();
    }
    if (fileNames.length() <= 0)
    {
        return;
    }
    QString import_path = fileNames.at(0);

    // 防御性校验：若用户选择了相册自身或其内部子目录作为导入源，直接提示并拦截
    QString cleanImport = QFileInfo(import_path).canonicalFilePath();
    if (cleanImport.isEmpty()) cleanImport = QDir::cleanPath(import_path);
    QString cleanAlbum = QFileInfo(path).canonicalFilePath();
    if (cleanAlbum.isEmpty()) cleanAlbum = QDir::cleanPath(path);

#ifdef Q_OS_WIN
    const auto cs = Qt::CaseInsensitive;
#else
    const auto cs = Qt::CaseSensitive;
#endif

    if (cleanImport.compare(cleanAlbum, cs) == 0 ||
        cleanImport.startsWith(cleanAlbum + "/", cs) ||
        cleanImport.startsWith(cleanAlbum + "\\", cs))
    {
        QMessageBox::information(this, tr("导入文件"), tr("不能选择相册目录自身或相册内部子目录作为导入源。"));
        return;
    }

    int file_count = 0;

    // 进度监控机制（关闭 autoClose/autoReset 防止进度取模回零或满值时误触发 canceled 信号）
    _dialog_progress = new QProgressDialog(this);
    _dialog_progress->setAutoClose(false);
    _dialog_progress->setAutoReset(false);

    // 线程：按值/安全指针传入参数，避免局部变量引用悬垂
    _thread_create_pro = std::make_shared<ProTreeThread>(
        import_path, path, target_pro_item, file_count, this, target_pro_item, nullptr);
    connect(_thread_create_pro.get(), &ProTreeThread::SigUpdateProgress, this, &ProTreeWidget::SlotUpdateProgress);
    connect(_thread_create_pro.get(), &ProTreeThread::SigFinishProgress, this, &ProTreeWidget::SlotFinishProgress);
    connect(_dialog_progress, &QProgressDialog::canceled, this, &ProTreeWidget::SlotCancelProgress);
    connect(this, &ProTreeWidget::SigCancelProgress, _thread_create_pro.get(), &ProTreeThread::SlotCanceled);
    _thread_create_pro->start();
    _dialog_progress->setWindowTitle("Please wait...");
    _dialog_progress->setFixedWidth(PROGRESS_WIDTH);
    _dialog_progress->setRange(0, PROGRESS_WIDTH);
    _dialog_progress->exec();
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

    // 导入线程析构时会回滚本次已复制文件，关闭项目不能触发这条数据删除路径。
    // 等导入完成后再允许关闭，避免项目关闭与后台导入竞态导致永久删除。
    if (_thread_create_pro)
    {
        QMessageBox::information(
            this,
            tr("无法关闭相册"),
            tr("当前仍有图片正在导入，请等待导入完成后再关闭相册。"));
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

    // 2. 打开相册的扫描线程只拥有临时树节点，不删除磁盘文件。
    if (_thread_open_pro && _thread_open_pro->GetRoot() == _right_btn_item)
    {
        disconnect(_thread_open_pro.get(), nullptr, this, nullptr);
        _thread_open_pro->SlotCanceled();
        _thread_open_pro->wait();
        _thread_open_pro.reset();
    }

    // 3. 严格校验有效索引后从树控件中移除，并通过 QTimer::singleShot 延后释放堆对象，
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
void ProTreeWidget::SlotUpdateProgress(int count)
{
    if(!_dialog_progress)
    {
        return;
    }
    if(count>=PROGRESS_MAX)
    {
        _dialog_progress->setValue(count%PROGRESS_MAX);
    }
    else
    {
        _dialog_progress->setValue(count);
    }
}

void ProTreeWidget::SlotFinishProgress()
{
    if (_thread_create_pro)
    {
        _thread_create_pro->CommitToTree();
    }
    if (_dialog_progress)
    {
        disconnect(_dialog_progress, &QProgressDialog::canceled, this, &ProTreeWidget::SlotCancelProgress);
        _dialog_progress->setValue(PROGRESS_MAX);
        _dialog_progress->accept();
        _dialog_progress->deleteLater();
        _dialog_progress = nullptr;
    }
    for (int i = 0; i < this->topLevelItemCount(); ++i)
    {
        RefreshItemFavoriteDisplay(this->topLevelItem(i));
    }
    ApplyTreeFilter();

    // 导入完成后，若当前画布尚未展示任何图片，自动选中并展示首张可见图片
    if (!_selecetd_item)
    {
        for (int i = 0; i < this->topLevelItemCount(); ++i)
        {
            auto *topPro = dynamic_cast<ProTreeItem*>(this->topLevelItem(i));
            ProTreeItem *firstPic = topPro ? topPro->GetFirstPicChild() : nullptr;
            while (firstPic && firstPic->isHidden())
            {
                firstPic = firstPic->GetNextItem();
            }
            if (firstPic)
            {
                _selecetd_item = firstPic;
                this->setCurrentItem(firstPic);
                emit SigUpdateSelected(firstPic->GetPath());
                break;
            }
        }
    }

    SaveWorkspaceSettings();
    _thread_create_pro.reset();
}

void ProTreeWidget::SlotCancelProgress()
{
    // 取消追加导入时，仅通知后台导入线程中止并回滚本次新建文件，保留原有相册项目与数据
    if (_thread_create_pro)
    {
        disconnect(_thread_create_pro.get(), &ProTreeThread::SigUpdateProgress, this, &ProTreeWidget::SlotUpdateProgress);
        disconnect(_thread_create_pro.get(), &ProTreeThread::SigFinishProgress, this, &ProTreeWidget::SlotFinishProgress);
    }
    emit SigCancelProgress();
    _thread_create_pro.reset();
    if (_dialog_progress)
    {
        disconnect(_dialog_progress, &QProgressDialog::canceled, this, &ProTreeWidget::SlotCancelProgress);
        _dialog_progress->deleteLater();
        _dialog_progress = nullptr;
    }
}

void ProTreeWidget::SlotUpOpenProgress(int count)
{
    if(!_open_progressdlg)
    {
        return;
    }
    if(count>=PROGRESS_MAX)
    {
        _open_progressdlg->setValue(count%PROGRESS_MAX);
    }
    else
    {
        _open_progressdlg->setValue(count);
    }
}

void ProTreeWidget::SlotFinishOpenProgress()
{
    ProTreeItem *openedRoot = nullptr;
    if (_thread_open_pro)
    {
        _thread_open_pro->CommitToTree();
        openedRoot = dynamic_cast<ProTreeItem*>(_thread_open_pro->GetRoot());
        if (openedRoot && openedRoot->GetPath() == _restoring_active_path)
        {
            if (_active_item)
            {
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
        if (!_active_item && _thread_open_pro->GetRoot())
        {
            _active_item = _thread_open_pro->GetRoot();
            QFont boldFont;
            boldFont.setBold(true);
            _active_item->setFont(0, boldFont);
        }
    }
    if (_open_progressdlg)
    {
        disconnect(_open_progressdlg, &QProgressDialog::canceled, this, &ProTreeWidget::SlotCancelOpenProgress);
        _open_progressdlg->setValue(PROGRESS_MAX);
        _open_progressdlg->accept();
        _open_progressdlg->deleteLater();
        _open_progressdlg = nullptr;
    }
    for (int i = 0; i < this->topLevelItemCount(); ++i)
    {
        RefreshItemFavoriteDisplay(this->topLevelItem(i));
    }
    ApplyTreeFilter();

    // 打开相册后，若当前画布尚未展示任何图片，自动选中并展示该相册的第一张可见照片
    if (!_selecetd_item && openedRoot)
    {
        ProTreeItem *firstPic = openedRoot->GetFirstPicChild();
        while (firstPic && firstPic->isHidden())
        {
            firstPic = firstPic->GetNextItem();
        }
        if (firstPic && firstPic->GetRoot() == openedRoot)
        {
            _selecetd_item = firstPic;
            this->setCurrentItem(firstPic);
            emit SigUpdateSelected(firstPic->GetPath());
        }
    }

    SaveWorkspaceSettings();
    _thread_open_pro.reset();
}

void ProTreeWidget::SlotCancelOpenProgress()
{
    if (_thread_open_pro)
    {
        disconnect(_thread_open_pro.get(), &OpenTreeThread::SigUpdataProGress, this, &ProTreeWidget::SlotUpOpenProgress);
        disconnect(_thread_open_pro.get(), &OpenTreeThread::SigFinishProGress, this, &ProTreeWidget::SlotFinishOpenProgress);
        _set_path.remove(_thread_open_pro->GetSrcPath());
    }
    emit SigCancelOpenProgress();
    _thread_open_pro.reset();
    if (_open_progressdlg)
    {
        disconnect(_open_progressdlg, &QProgressDialog::canceled, this, &ProTreeWidget::SlotCancelOpenProgress);
        _open_progressdlg->deleteLater();
        _open_progressdlg = nullptr;
    }
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
    if (!_set_path.contains(path) && !_pending_open_paths.contains(path))
    {
        _pending_open_paths.append(path);
    }
    // exec() processes timers and can reenter this slot during workspace restoration.
    // Keep ownership of the current task until its modal loop has returned.
    if (_opening_projects) return;
    QScopedValueRollback<bool> openingGuard(_opening_projects, true);
    while (!_pending_open_paths.isEmpty())
    {
    const QString path = _pending_open_paths.takeFirst();
    //避免重复打开
    if(_set_path.find(path)!=_set_path.end())
    {
        continue;
    }
    _set_path.insert(path);
    int file_count=0;
    QDir pro_dir(path);
    QString proname=pro_dir.dirName();

    _open_progressdlg=new QProgressDialog(this);
    _open_progressdlg->setAutoClose(false);
    _open_progressdlg->setAutoReset(false);
    _thread_open_pro=std::make_shared<OpenTreeThread>(path,file_count,this,nullptr);
    connect(_thread_open_pro.get(),&OpenTreeThread::SigUpdataProGress,this,&ProTreeWidget::SlotUpOpenProgress);
    connect(_thread_open_pro.get(),&OpenTreeThread::SigFinishProGress,this,&ProTreeWidget::SlotFinishOpenProgress);
    connect(_open_progressdlg,&QProgressDialog::canceled,this,&ProTreeWidget::SlotCancelOpenProgress);
    connect(this,&ProTreeWidget::SigCancelOpenProgress,_thread_open_pro.get(),&OpenTreeThread::SlotCanceled);
    _thread_open_pro->start();
    _open_progressdlg->setWindowTitle("Please wait...");
    _open_progressdlg->setFixedWidth(PROGRESS_WIDTH);
    _open_progressdlg->setRange(0,PROGRESS_WIDTH);
    _open_progressdlg->exec();
    }
}

// 响应 PicShow 界面点击“下一张”按钮的槽函数
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

static QString FindDefaultMusicDir()
{
    QStringList baseDirs = { QDir::currentPath(), QCoreApplication::applicationDirPath() };
    for (const QString &base : baseDirs)
    {
        QDir dir(base);
        for (int depth = 0; depth < 4; ++depth)
        {
            if (dir.exists("music"))
            {
                QDir musicDir(dir.filePath("music"));
                return musicDir.absolutePath();
            }
            if (!dir.cdUp()) break;
        }
    }
    return QDir::currentPath();
}

QString ProTreeWidget::SniffAudioFileFormat(const QString &filePath, QString *outDiagReason)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
    {
        if (outDiagReason) *outDiagReason = tr("音频文件未找到");
        return QString();
    }
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (outDiagReason) *outDiagReason = tr("无法读取文件（可能被其他程序独占或无访问权限）");
        return QString();
    }
    QByteArray header = f.read(32);
    f.close();

    if (header.size() < 4)
    {
        if (outDiagReason) *outDiagReason = tr("文件数据为空或已损坏 (<4 字节)");
        return QString();
    }

    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    // 1. 深度检测 MP4 容器 (包含 ftyp 标识)
    if (header.size() >= 8 && header.mid(4, 4) == "ftyp")
    {
        if (ext == "mp3" || ext == "wav" || ext == "flac")
        {
            if (outDiagReason)
            {
                *outDiagReason = tr("⚠️ 严重格式冲突：该文件实质为 MP4 容器（录屏/视频），却被强行命名为了 .%1！系统原生解码器判定文件头非法而拒绝播放。建议改回 .mp4 真实扩展名，或提供真正的纯音频文件。").arg(ext);
            }
        }
        else if (outDiagReason)
        {
            *outDiagReason = tr("MP4 格式媒体（纯音频提取播放模式）");
        }
        return "mp4";
    }

    // 2. 检测标准 RIFF WAVE
    if (header.size() >= 12 && header.startsWith("RIFF") && header.mid(8, 4) == "WAVE")
    {
        if (outDiagReason) *outDiagReason = tr("标准 WAVE (PCM) 无损/纯音频");
        return "wav";
    }

    // 3. 检测标准 FLAC
    if (header.startsWith("fLaC"))
    {
        if (outDiagReason) *outDiagReason = tr("标准 FLAC 无损音频");
        return "flac";
    }

    // 4. 检测 MP3 标志 (ID3v2 标签 或 MPEG-1 Audio Sync Word)
    if (header.startsWith("ID3"))
    {
        if (outDiagReason) *outDiagReason = tr("标准 MP3 音频 (ID3 标签)");
        return "mp3";
    }
    const unsigned char *u = reinterpret_cast<const unsigned char*>(header.constData());
    if ((u[0] == 0xFF) && ((u[1] & 0xE0) == 0xE0))
    {
        if (outDiagReason) *outDiagReason = tr("标准 MP3 音频 (MPEG 帧同步头)");
        return "mp3";
    }

    // 5. 检测 Ogg
    if (header.startsWith("OggS"))
    {
        if (outDiagReason) *outDiagReason = tr("标准 OGG 音频");
        return "ogg";
    }

    if (outDiagReason)
    {
        *outDiagReason = tr("通用音频编码格式 (.%1)").arg(ext);
    }
    return ext;
}

bool ProTreeWidget::IsAudioFileValid(const QString &filePath, QString *outDiagReason)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
    {
        if (outDiagReason) *outDiagReason = tr("音频文件不存在");
        return false;
    }
    QFileInfo fi(filePath);
    if (fi.size() < 1024)
    {
        if (outDiagReason) *outDiagReason = tr("音频文件损坏或大小异常 (<1KB)");
        return false;
    }

    QString sniffReason;
    QString realFormat = SniffAudioFileFormat(filePath, &sniffReason);
    QString ext = fi.suffix().toLower();
    if (realFormat == "mp4" && (ext == "mp3" || ext == "wav" || ext == "flac"))
    {
        if (outDiagReason) *outDiagReason = sniffReason;
        return false;
    }
    if (outDiagReason) *outDiagReason = sniffReason;
    return true;
}

static QString FindDefaultMusicFile()
{
    QString musicDirPath = FindDefaultMusicDir();
    QDir musicDir(musicDirPath);

    // 优先采用用户原始高保真音轨
    QString preferredBandicam = musicDir.filePath("bandicam_original.wav");
    if (QFileInfo::exists(preferredBandicam) && ProTreeWidget::IsAudioFileValid(preferredBandicam))
    {
        return preferredBandicam;
    }

    // 扫描匹配可用合法音频
    QStringList filters = { "*.wav", "*.mp3", "*.flac", "*.m4a", "*.aac", "*.ogg", "*.mp4" };
    QFileInfoList files = musicDir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files)
    {
        QString reason;
        if (ProTreeWidget::IsAudioFileValid(fi.absoluteFilePath(), &reason))
        {
            return fi.absoluteFilePath();
        }
        else
        {
            qWarning() << "[MusicPlayer] FindDefaultMusicFile skipped invalid file:" << fi.fileName() << "Reason:" << reason;
        }
    }

    // 系统兜底高品质铃声
    QString winMediaDir = "C:/Windows/Media";
    if (QDir(winMediaDir).exists())
    {
        QStringList sysPreferred = { "Ring08.wav", "Ring05.wav", "Ring09.wav", "Ring03.wav", "Alarm01.wav" };
        for (const QString &sysFile : sysPreferred)
        {
            QString fullSys = winMediaDir + "/" + sysFile;
            if (QFileInfo::exists(fullSys))
            {
                return fullSys;
            }
        }
    }

    return QString();
}

void ProTreeWidget::EnsureMusicSourceLoaded()
{
    QString diagReason;
    if (_music_path.isEmpty() || !QFileInfo::exists(_music_path) || !IsAudioFileValid(_music_path, &diagReason))
    {
        if (!_music_path.isEmpty())
        {
            qWarning() << "[MusicPlayer] Current music path invalid:" << _music_path << "Reason:" << diagReason;
            _last_music_error = diagReason;
        }
        _music_path = FindDefaultMusicFile();
        SaveWorkspaceSettings();
    }

    if (!_music_path.isEmpty() && QFileInfo::exists(_music_path))
    {
        QUrl expectedUrl = QUrl::fromLocalFile(_music_path);
        if (_player->source() != expectedUrl)
        {
            _player->setSource(expectedUrl);
            _player->setLoops(QMediaPlayer::Infinite);
        }
    }
    if (_audio_output)
    {
        _audio_output->setMuted(false);
        _audio_output->setVolume(qBound(0, _music_volume, 100) / 100.0f);
    }
}

void ProTreeWidget::NativeMciClose()
{
    mciSendStringW(QString("stop %1").arg(QString::fromWCharArray(kMciAlias)).toStdWString().c_str(), NULL, 0, NULL);
    mciSendStringW(QString("close %1").arg(QString::fromWCharArray(kMciAlias)).toStdWString().c_str(), NULL, 0, NULL);
    _is_native_playing = false;
    _is_native_paused = false;
}

bool ProTreeWidget::NativeMciPlay(const QString &filePath)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
    {
        _last_music_error = tr("音频文件未找到");
        return false;
    }

    // 若当前正在暂停同一首曲目，执行恢复播放
    if (_is_native_paused)
    {
        MCIERROR errResume = mciSendStringW(QString("resume %1").arg(QString::fromWCharArray(kMciAlias)).toStdWString().c_str(), NULL, 0, NULL);
        if (errResume == 0)
        {
            _is_native_playing = true;
            _is_native_paused = false;
            NativeMciSetVolume(_music_volume);
            emit SigMusicStateChanged(true);
            return true;
        }
    }

    // 重新打开新音轨：先彻底清理旧连接
    NativeMciClose();

    // 转换为 Windows 原生规范绝对路径
    QString nativePath = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    std::wstring wPath = nativePath.toStdWString();

    // 优先采用 mpegvideo 类型打开（全兼容 MP3 / WAV / WMA / AAC）
    std::wstring cmdOpen = L"open \"" + wPath + L"\" type mpegvideo alias " + kMciAlias;
    MCIERROR err = mciSendStringW(cmdOpen.c_str(), NULL, 0, NULL);

    // 若类型指定失败，回退尝试由 Windows MCI 依据注册表文件扩展名自动协商驱动
    if (err != 0)
    {
        std::wstring cmdFallback = L"open \"" + wPath + L"\" alias " + kMciAlias;
        err = mciSendStringW(cmdFallback.c_str(), NULL, 0, NULL);
    }

    if (err != 0)
    {
        wchar_t errBuf[256] = {0};
        mciGetErrorStringW(err, errBuf, 256);
        _last_music_error = tr("Windows 原生音频引擎打开失败: %1").arg(QString::fromWCharArray(errBuf));
        qWarning() << "[NativeAudio]" << _last_music_error;
        emit SigMusicErrorOccurred(_last_music_error);
        emit SigMusicStateChanged(false);
        return false;
    }

    // 设置音量 (MCI 范围 0 ~ 1000)
    NativeMciSetVolume(_music_volume);

    // 循环播放
    std::wstring cmdPlay = L"play " + std::wstring(kMciAlias) + L" repeat";
    err = mciSendStringW(cmdPlay.c_str(), NULL, 0, NULL);
    if (err != 0)
    {
        cmdPlay = L"play " + std::wstring(kMciAlias);
        err = mciSendStringW(cmdPlay.c_str(), NULL, 0, NULL);
    }

    if (err != 0)
    {
        wchar_t errBuf[256] = {0};
        mciGetErrorStringW(err, errBuf, 256);
        _last_music_error = tr("Windows 原生音频引擎播放失败: %1").arg(QString::fromWCharArray(errBuf));
        qWarning() << "[NativeAudio]" << _last_music_error;
        emit SigMusicErrorOccurred(_last_music_error);
        emit SigMusicStateChanged(false);
        return false;
    }

    _is_native_playing = true;
    _is_native_paused = false;
    _last_music_error.clear();
    emit SigMusicStateChanged(true);
    return true;
}

void ProTreeWidget::NativeMciPause()
{
    if (_is_native_playing && !_is_native_paused)
    {
        std::wstring cmd = L"pause " + std::wstring(kMciAlias);
        mciSendStringW(cmd.c_str(), NULL, 0, NULL);
        _is_native_playing = false;
        _is_native_paused = true;
        emit SigMusicStateChanged(false);
    }
}

void ProTreeWidget::NativeMciStop()
{
    if (_is_native_playing || _is_native_paused)
    {
        std::wstring cmd = L"stop " + std::wstring(kMciAlias);
        mciSendStringW(cmd.c_str(), NULL, 0, NULL);
        std::wstring cmdSeek = L"seek " + std::wstring(kMciAlias) + L" to start";
        mciSendStringW(cmdSeek.c_str(), NULL, 0, NULL);
        _is_native_playing = false;
        _is_native_paused = false;
        emit SigMusicStateChanged(false);
    }
}

void ProTreeWidget::NativeMciSetVolume(int volume)
{
    int vol1000 = qBound(0, volume, 100) * 10;
    std::wstring cmd = L"setaudio " + std::wstring(kMciAlias) + L" volume to " + std::to_wstring(vol1000);
    mciSendStringW(cmd.c_str(), NULL, 0, NULL);
}

bool ProTreeWidget::NativeMciIsPlaying() const
{
    if (!_is_native_playing) return false;
    wchar_t statusBuf[128] = {0};
    std::wstring cmd = L"status " + std::wstring(kMciAlias) + L" mode";
    if (mciSendStringW(cmd.c_str(), statusBuf, 128, NULL) == 0)
    {
        QString mode = QString::fromWCharArray(statusBuf).trimmed().toLower();
        return (mode == "playing");
    }
    return _is_native_playing;
}

qint64 ProTreeWidget::NativeMciGetPosition() const
{
    wchar_t posBuf[128] = {0};
    std::wstring cmd = L"status " + std::wstring(kMciAlias) + L" position";
    if (mciSendStringW(cmd.c_str(), posBuf, 128, NULL) == 0)
    {
        return QString::fromWCharArray(posBuf).toLongLong();
    }
    return 0;
}

qint64 ProTreeWidget::NativeMciGetDuration() const
{
    wchar_t lenBuf[128] = {0};
    std::wstring cmd = L"status " + std::wstring(kMciAlias) + L" length";
    if (mciSendStringW(cmd.c_str(), lenBuf, 128, NULL) == 0)
    {
        return QString::fromWCharArray(lenBuf).toLongLong();
    }
    return 0;
}

bool ProTreeWidget::IsMusicPlaying() const
{
    return NativeMciIsPlaying() || (_player && _player->playbackState() == QMediaPlayer::PlayingState);
}

void ProTreeWidget::SetPlayImmediately(bool enable)
{
    if (_play_immediately == enable) return;
    _play_immediately = enable;
    SaveWorkspaceSettings();
    emit SigMusicSettingsChanged();
}

void ProTreeWidget::SetAutoPlayOnSlideshow(bool enable)
{
    if (_auto_play_on_slideshow == enable) return;
    _auto_play_on_slideshow = enable;
    SaveWorkspaceSettings();
    emit SigMusicSettingsChanged();
}

void ProTreeWidget::SetAutoStopOnSlideshowExit(bool enable)
{
    if (_auto_stop_on_slideshow_exit == enable) return;
    _auto_stop_on_slideshow_exit = enable;
    SaveWorkspaceSettings();
    emit SigMusicSettingsChanged();
}

void ProTreeWidget::SetMusicVolume(int volume)
{
    _music_volume = qBound(0, volume, 100);
    NativeMciSetVolume(_music_volume);
    if (_audio_output)
    {
        _audio_output->setVolume(_music_volume / 100.0f);
    }
    SaveWorkspaceSettings();
    emit SigMusicSettingsChanged();
}

void ProTreeWidget::SlotSelectMusicFile()
{
    QFileDialog file_dialog(this);
    file_dialog.setFileMode(QFileDialog::ExistingFiles);
    file_dialog.setWindowTitle(tr("选择导入的音频文件"));
    file_dialog.setDirectory(FindDefaultMusicDir());
    file_dialog.setViewMode(QFileDialog::Detail);
    file_dialog.setNameFilter(tr("音频文件 (*.wav *.mp3 *.flac *.m4a *.aac *.ogg *.mp4);;纯音频 (*.wav *.mp3 *.flac);;所有文件 (*.*)"));
    QStringList fileNames;
    if (file_dialog.exec())
    {
        fileNames = file_dialog.selectedFiles();
    }
    else
    {
        return;
    }
    if (!fileNames.isEmpty())
    {
        QString chosenPath = fileNames.first();
        QString diagReason;
        if (!IsAudioFileValid(chosenPath, &diagReason))
        {
            QMessageBox::warning(this, tr("音频文件格式冲突或损坏"),
                                 tr("所选文件存在格式问题，系统可能无法正常解码播放：\n\n%1\n\n请选择有效的纯音频文件或重命名为正确扩展名。")
                                     .arg(diagReason));
            return;
        }

        NativeMciStop();
        _player->stop();
        _music_path = chosenPath;
        _last_music_error.clear();
        _player->setSource(QUrl::fromLocalFile(_music_path));
        _player->setLoops(QMediaPlayer::Infinite);
        _player->setPosition(0);
        SaveWorkspaceSettings();
        emit SigMusicSettingsChanged();

        // 根据用户自主勾选的策略决定是否选完立即播放
        if (_play_immediately)
        {
            SlotStartMusic();
        }
    }
}

void ProTreeWidget::SlotSetMusic()
{
    EnsureMusicSourceLoaded();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("🎵 背景音乐控制与偏好设置"));
    dlg.setMinimumWidth(500);
    dlg.setStyleSheet(
        "QDialog { background-color: rgb(43,45,48); color: rgb(230,233,238); }"
        "QLabel { color: rgb(225,228,235); font-size: 12px; }"
        "QCheckBox { color: rgb(225,228,235); font-size: 12px; spacing: 8px; padding: 3px 0; }"
        "QCheckBox::indicator { width: 16px; height: 16px; }");

    QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(18, 16, 18, 16);
    mainLayout->setSpacing(12);

    // 1. 当前音频文件信息与选择区
    QLabel *titleTrack = new QLabel(tr("<b>🎧 当前背景音乐曲目与设备诊断</b>"), &dlg);
    mainLayout->addWidget(titleTrack);

    QLabel *lblCurrentTrack = new QLabel(&dlg);
    lblCurrentTrack->setWordWrap(true);
    lblCurrentTrack->setStyleSheet(
        "background-color: rgb(54,56,60); border: 1px solid rgb(82,85,90); "
        "border-radius: 5px; padding: 10px; color: rgb(235,238,242); line-height: 140%;");

    auto updateTrackLabel = [this, lblCurrentTrack]() {
        QString statusStr;
        if (!_last_music_error.isEmpty())
        {
            statusStr = tr("🔴 播放异常: %1").arg(_last_music_error);
        }
        else if (IsMusicPlaying())
        {
            statusStr = tr("🟢 正在播放");
        }
        else if (_is_native_paused || (_player && _player->playbackState() == QMediaPlayer::PausedState))
        {
            statusStr = tr("🟡 已暂停");
        }
        else
        {
            statusStr = tr("⚪ 已停止");
        }

        QString devName = QMediaDevices::defaultAudioOutput().description();
        if (devName.isEmpty())
        {
            devName = tr("默认系统音频输出设备");
        }

        if (_music_path.isEmpty() || !QFileInfo::exists(_music_path))
        {
            lblCurrentTrack->setText(tr("状态：%1\n曲目：未设置有效音频文件\n输出设备：%2").arg(statusStr, devName));
        }
        else
        {
            QFileInfo fi(_music_path);
            QString sniffReason;
            SniffAudioFileFormat(_music_path, &sniffReason);
            lblCurrentTrack->setText(
                tr("状态：%1\n曲目：%2\n格式：%3\n输出设备：%4\n路径：%5")
                    .arg(statusStr, fi.fileName(), sniffReason, devName, QDir::toNativeSeparators(fi.absoluteFilePath())));
        }
    };
    updateTrackLabel();
    mainLayout->addWidget(lblCurrentTrack);

    QHBoxLayout *fileBtnRow = new QHBoxLayout();
    QPushButton *btnChooseFile = new QPushButton(tr("📂 选择/更换音乐文件..."), &dlg);
    btnChooseFile->setObjectName("canvasToolBtn");
    btnChooseFile->setCursor(Qt::PointingHandCursor);
    btnChooseFile->setMinimumHeight(30);

    QPushButton *btnDefaultFile = new QPushButton(tr("🔄 恢复默认音乐 (高品质旋律)"), &dlg);
    btnDefaultFile->setObjectName("canvasToolBtn");
    btnDefaultFile->setCursor(Qt::PointingHandCursor);
    btnDefaultFile->setMinimumHeight(30);

    fileBtnRow->addWidget(btnChooseFile);
    fileBtnRow->addWidget(btnDefaultFile);
    fileBtnRow->addStretch();
    mainLayout->addLayout(fileBtnRow);

    QFrame *sep1 = new QFrame(&dlg);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setStyleSheet("color: rgb(72,75,80);");
    mainLayout->addWidget(sep1);

    // 2. 实时播放控制与音量调节区
    QLabel *titleCtrl = new QLabel(tr("<b>🎛️ 实时播放控制与音量</b>"), &dlg);
    mainLayout->addWidget(titleCtrl);

    QHBoxLayout *ctrlRow = new QHBoxLayout();
    QPushButton *btnPlay = new QPushButton(tr("▶️ 立即播放"), &dlg);
    btnPlay->setObjectName("canvasToolBtn");
    btnPlay->setCursor(Qt::PointingHandCursor);
    btnPlay->setMinimumHeight(30);

    QPushButton *btnPause = new QPushButton(tr("⏸️ 暂停"), &dlg);
    btnPause->setObjectName("canvasToolBtn");
    btnPause->setCursor(Qt::PointingHandCursor);
    btnPause->setMinimumHeight(30);

    QPushButton *btnStop = new QPushButton(tr("⏹️ 停止音乐"), &dlg);
    btnStop->setObjectName("canvasToolBtn");
    btnStop->setCursor(Qt::PointingHandCursor);
    btnStop->setMinimumHeight(30);

    ctrlRow->addWidget(btnPlay);
    ctrlRow->addWidget(btnPause);
    ctrlRow->addWidget(btnStop);
    ctrlRow->addStretch();
    mainLayout->addLayout(ctrlRow);

    QHBoxLayout *volRow = new QHBoxLayout();
    QLabel *lblVolTitle = new QLabel(tr("🔊 音量大小:"), &dlg);
    QSlider *sliderVol = new QSlider(Qt::Horizontal, &dlg);
    sliderVol->setRange(0, 100);
    sliderVol->setValue(_music_volume);
    QLabel *lblVolVal = new QLabel(QString("%1%").arg(_music_volume), &dlg);
    lblVolVal->setFixedWidth(40);
    volRow->addWidget(lblVolTitle);
    volRow->addWidget(sliderVol, 1);
    volRow->addWidget(lblVolVal);
    mainLayout->addLayout(volRow);

    QFrame *sep2 = new QFrame(&dlg);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet("color: rgb(72,75,80);");
    mainLayout->addWidget(sep2);

    // 3. 人性化播放策略选项区（由用户自主决定）
    QLabel *titleOpts = new QLabel(tr("<b>⚙️ 播放行为自主选项（自动记忆）</b>"), &dlg);
    mainLayout->addWidget(titleOpts);

    QCheckBox *chkImmediate = new QCheckBox(tr("选定音乐文件后立即开始播放"), &dlg);
    chkImmediate->setChecked(_play_immediately);

    QCheckBox *chkSlideshowPlay = new QCheckBox(tr("进入【轮播图播放】时自动开启背景音乐"), &dlg);
    chkSlideshowPlay->setChecked(_auto_play_on_slideshow);

    QCheckBox *chkSlideshowStop = new QCheckBox(
        tr("退出或暂停【轮播图播放】时自动停止背景音乐\n（若取消勾选，退出轮播后音乐将继续在主界面后台播放）"), &dlg);
    chkSlideshowStop->setChecked(_auto_stop_on_slideshow_exit);

    mainLayout->addWidget(chkImmediate);
    mainLayout->addWidget(chkSlideshowPlay);
    mainLayout->addWidget(chkSlideshowStop);

    // 底部关闭按钮
    QHBoxLayout *bottomRow = new QHBoxLayout();
    bottomRow->addStretch();
    QPushButton *btnClose = new QPushButton(tr("完成并关闭"), &dlg);
    btnClose->setObjectName("canvasToolBtn");
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setMinimumSize(100, 30);
    bottomRow->addWidget(btnClose);
    mainLayout->addLayout(bottomRow);

    // 信号绑定
    connect(btnChooseFile, &QPushButton::clicked, &dlg, [this, updateTrackLabel]() {
        SlotSelectMusicFile();
        updateTrackLabel();
    });
    connect(btnDefaultFile, &QPushButton::clicked, &dlg, [this, updateTrackLabel]() {
        QString defFile = FindDefaultMusicFile();
        if (!defFile.isEmpty())
        {
            bool wasPlaying = IsMusicPlaying();
            NativeMciStop();
            _player->stop();
            _music_path = defFile;
            _last_music_error.clear();
            _player->setSource(QUrl::fromLocalFile(_music_path));
            _player->setLoops(QMediaPlayer::Infinite);
            SaveWorkspaceSettings();
            if (wasPlaying || _play_immediately)
            {
                SlotStartMusic();
            }
            updateTrackLabel();
        }
    });
    connect(btnPlay, &QPushButton::clicked, &dlg, [this, updateTrackLabel]() {
        SlotStartMusic();
        updateTrackLabel();
    });
    connect(btnPause, &QPushButton::clicked, &dlg, [this, updateTrackLabel]() {
        SlotPauseMusic();
        updateTrackLabel();
    });
    connect(btnStop, &QPushButton::clicked, &dlg, [this, updateTrackLabel]() {
        SlotStopMusic();
        updateTrackLabel();
    });
    connect(this, &ProTreeWidget::SigMusicStateChanged, &dlg, [updateTrackLabel](bool) {
        updateTrackLabel();
    });
    connect(_player, &QMediaPlayer::playbackStateChanged, &dlg, [updateTrackLabel](QMediaPlayer::PlaybackState) {
        updateTrackLabel();
    });
    connect(_player, &QMediaPlayer::errorOccurred, &dlg, [updateTrackLabel](QMediaPlayer::Error, const QString &) {
        updateTrackLabel();
    });
    connect(sliderVol, &QSlider::valueChanged, &dlg, [this, lblVolVal](int val) {
        lblVolVal->setText(QString("%1%").arg(val));
        SetMusicVolume(val);
    });
    connect(chkImmediate, &QCheckBox::toggled, this, &ProTreeWidget::SetPlayImmediately);
    connect(chkSlideshowPlay, &QCheckBox::toggled, this, &ProTreeWidget::SetAutoPlayOnSlideshow);
    connect(chkSlideshowStop, &QCheckBox::toggled, this, &ProTreeWidget::SetAutoStopOnSlideshowExit);
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}

void ProTreeWidget::SlotStartMusic()
{
    EnsureMusicSourceLoaded();
    if (_music_path.isEmpty() || !QFileInfo::exists(_music_path))
    {
        _last_music_error = tr("未找到有效的音频文件，请在音乐设置中重新选择音频。");
        qWarning() << "[MusicPlayer]" << _last_music_error;
        emit SigMusicErrorOccurred(_last_music_error);
        return;
    }

    QString diagReason;
    if (!IsAudioFileValid(_music_path, &diagReason))
    {
        _last_music_error = diagReason;
        qWarning() << "[MusicPlayer] Pre-check failed:" << diagReason;
        emit SigMusicErrorOccurred(_last_music_error);
        return;
    }

    _last_music_error.clear();

    // 核心解决点：通过 Windows 原生多媒体引擎直接将音频流推入物理声卡，绕过 Qt 6.8 FFmpeg 插件的空通道重采样 Bug
    bool mciOk = NativeMciPlay(_music_path);
    if (!mciOk)
    {
        qWarning() << "[MusicPlayer] Native MCI failed, trying QMediaPlayer fallback...";
        if (_audio_output)
        {
            _audio_output->setMuted(false);
            _audio_output->setVolume(qBound(0, _music_volume, 100) / 100.0f);
        }
        if (!_player->source().isEmpty())
        {
            _player->play();
        }
    }
}

void ProTreeWidget::SlotPauseMusic()
{
    NativeMciPause();
    if (_player)
    {
        _player->pause();
    }
}

void ProTreeWidget::SlotStopMusic()
{
    NativeMciStop();
    if (_player)
    {
        _player->stop();
    }
}

void ProTreeWidget::SlotToggleMusic()
{
    if (IsMusicPlaying())
    {
        SlotStopMusic();
    }
    else
    {
        SlotStartMusic();
    }
}

void ProTreeWidget::SlotSlideShowStartMusic()
{
    if (_auto_play_on_slideshow)
    {
        SlotStartMusic();
    }
}

void ProTreeWidget::SlotSlideShowStopMusic()
{
    if (_auto_stop_on_slideshow_exit)
    {
        SlotStopMusic();
    }
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
            item->setData(0, Qt::DisplayRole, isFav ? ("⭐ " + fi.fileName()) : fi.fileName());
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
}

bool ProTreeWidget::FilterItemRecursive(QTreeWidgetItem *item)
{
    if (!item) return false;

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
    QSettings settings("AlbumStudio", "AlbumApp");
    QStringList openedList(_set_path.begin(), _set_path.end());
    QStringList favList(_favorite_paths.begin(), _favorite_paths.end());
    settings.setValue("opened_projects", openedList);
    settings.setValue("favorite_photos", favList);
    auto *activePro = dynamic_cast<ProTreeItem*>(_active_item);
    settings.setValue("active_project", activePro ? activePro->GetPath() : QString());
    settings.setValue("music_path", _music_path);
    settings.setValue("music_volume", _music_volume);
    settings.setValue("music_play_immediately", _play_immediately);
    settings.setValue("music_auto_play_slideshow", _auto_play_on_slideshow);
    settings.setValue("music_auto_stop_slideshow", _auto_stop_on_slideshow_exit);
}

void ProTreeWidget::LoadWorkspaceSettings()
{
    QSettings settings("AlbumStudio", "AlbumApp");
    QStringList favList = settings.value("favorite_photos").toStringList();
    _favorite_paths = QSet<QString>(favList.begin(), favList.end());
    QString savedActivePath = settings.value("active_project").toString();
    _restoring_active_path = savedActivePath;
    const QStringList openedList = settings.value("opened_projects").toStringList();

    _music_volume = settings.value("music_volume", 80).toInt();
    _play_immediately = settings.value("music_play_immediately", true).toBool();
    _auto_play_on_slideshow = settings.value("music_auto_play_slideshow", true).toBool();
    _auto_stop_on_slideshow_exit = settings.value("music_auto_stop_slideshow", true).toBool();

    _music_path = settings.value("music_path").toString();
    QString diagReason;
    if (_music_path.isEmpty() || !QFileInfo::exists(_music_path) || !IsAudioFileValid(_music_path, &diagReason))
    {
        _music_path = FindDefaultMusicFile();
        settings.setValue("music_path", _music_path);
    }
    if (!_music_path.isEmpty() && QFileInfo::exists(_music_path))
    {
        _player->setSource(QUrl::fromLocalFile(_music_path));
        _player->setLoops(QMediaPlayer::Infinite);
    }
    if (_audio_output)
    {
        _audio_output->setMuted(false);
        _audio_output->setVolume(qBound(0, _music_volume, 100) / 100.0f);
    }
    emit SigMusicSettingsChanged();

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
quint64 ProTreeWidget::ComputeDHash(const QString &imgPath)
{
    QImage img(imgPath);
    if (img.isNull()) return 0;

    QImage smallImg = img.scaled(9, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_RGB32);
    quint64 hash = 0;
    int bitIndex = 0;
    for (int y = 0; y < 8; ++y)
    {
        const QRgb *line = reinterpret_cast<const QRgb*>(smallImg.constScanLine(y));
        for (int x = 0; x < 8; ++x)
        {
            int grayLeft = qRed(line[x]) * 77 + qGreen(line[x]) * 150 + qBlue(line[x]) * 29;
            int grayRight = qRed(line[x + 1]) * 77 + qGreen(line[x + 1]) * 150 + qBlue(line[x + 1]) * 29;
            if (grayLeft > grayRight)
            {
                hash |= (1ULL << bitIndex);
            }
            ++bitIndex;
        }
    }
    return hash;
}

// 计算两个 64-bit 感知哈希指纹之间的汉明距离 (Hamming Distance)
int ProTreeWidget::HammingDistance(quint64 h1, quint64 h2)
{
    quint64 x = h1 ^ h2;
    int dist = 0;
    while (x)
    {
        dist += static_cast<int>(x & 1ULL);
        x >>= 1;
    }
    return dist;
}

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

    struct PicHashNode {
        ProTreeItem *item;
        QString path;
        quint64 hash;
    };
    QVector<PicHashNode> nodes;
    nodes.reserve(picItems.size());

    for (ProTreeItem *it : picItems)
    {
        QString p = it->GetPath();
        if (QFileInfo::exists(p))
        {
            nodes.push_back({it, p, ComputeDHash(p)});
        }
    }

    struct DupPair {
        PicHashNode keepNode;
        PicHashNode dupNode;
        int hammingDist;
        double similarity;
    };
    QVector<DupPair> dupPairs;
    QSet<ProTreeItem*> markedAsDup;

    // 汉明距离阈值 <= 10（即 64 位中至少 54 位一致，结构相似度 >= 84.4%）判定为相似/重复照片
    const int kHammingThreshold = 10;
    for (int i = 0; i < nodes.size(); ++i)
    {
        if (markedAsDup.contains(nodes[i].item)) continue;
        for (int j = i + 1; j < nodes.size(); ++j)
        {
            if (markedAsDup.contains(nodes[j].item)) continue;
            // 仅在同一相册内部比对查重，避免误删其他独立相册中的照片
            if (nodes[i].item->GetRoot() != nodes[j].item->GetRoot()) continue;
            // 同一物理路径跳过
            if (nodes[i].path == nodes[j].path) continue;
            int dist = HammingDistance(nodes[i].hash, nodes[j].hash);
            if (dist <= kHammingThreshold)
            {
                double sim = ((64.0 - dist) / 64.0) * 100.0;
                dupPairs.push_back({nodes[i], nodes[j], dist, sim});
                markedAsDup.insert(nodes[j].item);
            }
        }
    }

    QGuiApplication::restoreOverrideCursor();

    if (dupPairs.isEmpty())
    {
        QMessageBox::information(this, tr("感知哈希 (dHash) 扫描完成"),
                                 tr("已扫描 %1 张照片的 64-bit 差异感知哈希指纹：\n\n未检测到重复或高度相似的照片（汉明距离阈值 ≤ %2）。")
                                     .arg(nodes.size())
                                     .arg(kHammingThreshold));
        return;
    }

    // 弹出相似/重复照片可视化比对与清理对话框
    QDialog dlg(this);
    dlg.setWindowTitle(tr("🧹 感知哈希 (dHash) 相似/重复照片智能检测报告"));
    dlg.resize(720, 480);
    dlg.setStyleSheet("QDialog { background-color: rgb(43,45,48); color: rgb(230,233,238); }");

    QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);
    QLabel *summaryLbl = new QLabel(
        tr("共提取 <b>%1</b> 张图像的 64-bit dHash 特征指纹，发现 <b>%2</b> 组重复/相似照片（勾选代表待清理的冗余副本）：")
            .arg(nodes.size())
            .arg(dupPairs.size()), &dlg);
    summaryLbl->setStyleSheet("color: rgb(225,228,235); font-size: 13px; padding: 4px;");
    mainLayout->addWidget(summaryLbl);

    QListWidget *listWid = new QListWidget(&dlg);
    listWid->setIconSize(QSize(72, 72));
    listWid->setSpacing(6);
    listWid->setStyleSheet(
        "QListWidget { background-color: rgb(54,56,60); color: rgb(230,233,238); border: 1px solid rgb(85,88,94); border-radius: 5px; }"
        "QListWidget::item { padding: 6px; border-bottom: 1px solid rgb(70,73,78); }");

    for (int i = 0; i < dupPairs.size(); ++i)
    {
        const auto &pair = dupPairs[i];
        QFileInfo fiKeep(pair.keepNode.path);
        QFileInfo fiDup(pair.dupNode.path);

        QString text = QString(
            "【相似组 #%1】 相似度: %2%  (汉明距离: %3 / 64)\n"
            "  ├─ 保留基准: %4  [dHash: 0x%5]\n"
            "  └─ 待清副本: %6  [dHash: 0x%7]  (%8)")
            .arg(i + 1)
            .arg(pair.similarity, 0, 'f', 1)
            .arg(pair.hammingDist)
            .arg(fiKeep.fileName())
            .arg(pair.keepNode.hash, 16, 16, QChar('0'))
            .arg(fiDup.fileName())
            .arg(pair.dupNode.hash, 16, 16, QChar('0'))
            .arg(fiDup.absoluteFilePath());

        QPixmap thumb(pair.dupNode.path);
        QListWidgetItem *lItem = new QListWidgetItem(
            QIcon(thumb.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation)),
            text, listWid);
        lItem->setFlags(lItem->flags() | Qt::ItemIsUserCheckable);
        lItem->setCheckState(Qt::Checked);
        lItem->setData(Qt::UserRole, i);
    }
    mainLayout->addWidget(listWid, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton *btnClean = new QPushButton(tr("🗑️ 一键清理勾选的相似副本"), &dlg);
    btnClean->setObjectName("canvasToolBtn");
    btnClean->setMinimumHeight(32);
    QPushButton *btnClose = new QPushButton(tr("关闭"), &dlg);
    btnClose->setObjectName("canvasToolBtn");
    btnClose->setMinimumHeight(32);
    btnLayout->addWidget(btnClean);
    btnLayout->addWidget(btnClose);
    mainLayout->addLayout(btnLayout);

    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnClean, &QPushButton::clicked, &dlg, [&]() {
        int checkedCount = 0;
        for (int row = 0; row < listWid->count(); ++row)
        {
            if (listWid->item(row)->checkState() == Qt::Checked)
            {
                ++checkedCount;
            }
        }
        if (checkedCount == 0)
        {
            QMessageBox::information(&dlg, tr("提示"), tr("请先勾选需要清理的相似副本照片。"));
            return;
        }
        auto reply = QMessageBox::question(
            &dlg,
            tr("确认清理冗余副本"),
            tr("确定要从相册及磁盘中永久删除勾选的 %1 张相似副本照片吗？").arg(checkedCount),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes)
        {
            return;
        }

        int removedCount = 0;
        QStringList failedPaths;
        for (int row = 0; row < listWid->count(); ++row)
        {
            QListWidgetItem *li = listWid->item(row);
            if (li->checkState() != Qt::Checked) continue;

            int pairIdx = li->data(Qt::UserRole).toInt();
            ProTreeItem *dupItem = dupPairs[pairIdx].dupNode.item;
            QString dupPath = dupPairs[pairIdx].dupNode.path;
            if (!dupItem) continue;

            // Keep the tree, selection, links and favorites intact if disk deletion fails.
            QFile duplicateFile(dupPath);
            if (!duplicateFile.remove())
            {
                failedPaths.append(tr("%1：%2").arg(QDir::toNativeSeparators(dupPath), duplicateFile.errorString()));
                continue;
            }

            // 1. 维护双向链表前后指针不断链
            ProTreeItem *pre = dupItem->GetPreItem();
            ProTreeItem *next = dupItem->GetNextItem();
            if (pre) pre->SetNextItem(next);
            if (next) next->SetPreItem(pre);

            if (_selecetd_item == dupItem)
            {
                _selecetd_item = pre ? pre : next;
                emit SigUpdatePic(_selecetd_item ? dynamic_cast<ProTreeItem*>(_selecetd_item)->GetPath() : "");
            }

            // 2. 从树节点中移除并删除磁盘冗余文件
            if (dupItem->parent())
            {
                dupItem->parent()->removeChild(dupItem);
            }
            delete dupItem;
            _favorite_paths.remove(dupPath);
            ++removedCount;
        }
        ApplyTreeFilter();
        SaveWorkspaceSettings();
        if (failedPaths.isEmpty())
        {
            QMessageBox::information(&dlg, tr("清理完成"), tr("已成功清理 %1 张相似/重复冗余照片！").arg(removedCount));
        }
        else
        {
            QMessageBox::warning(&dlg, tr("部分文件未能清理"),
                tr("成功清理 %1 张，失败 %2 张。失败图片的相册记录已保留。\n\n%3")
                    .arg(removedCount).arg(failedPaths.size()).arg(failedPaths.join("\n")));
        }
        dlg.accept();
    });

    dlg.exec();
}



