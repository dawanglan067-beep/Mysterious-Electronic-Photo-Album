#include "opentreethread.h"
#include <QDir>
#include <QIcon>
#include "const.h"
#include "protreeitem.h"

namespace {
// 在 GUI 主线程中递归为节点绑定图标，避免后台线程访问 QPixmap/QIcon 绘图资源
void ApplyOpenTreeIconsMainThread(QTreeWidgetItem *item)
{
    if (!item) return;
    if (item->type() == TreeItemPro || item->type() == TreeItemDir)
    {
        item->setData(0, Qt::DecorationRole, QIcon(item->type() == TreeItemPro ? ":/icon/line/album.svg" : ":/icon/line/dir.svg"));
    }
    else if (item->type() == TreeItemPic)
    {
        item->setData(0, Qt::DecorationRole, QIcon(":/icon/line/core.svg"));
    }
    for (int i = 0; i < item->childCount(); ++i)
    {
        ApplyOpenTreeIconsMainThread(item->child(i));
    }
}
} // namespace

// 构造函数：初始化线程运行所需的相册路径、计数器及父对象
OpenTreeThread::OpenTreeThread(const QString &src_path, int file_count, QTreeWidget *self, QObject *parent)
    : QThread(parent), _src_path(src_path), _file_count(file_count), _self(self), _bstop(false), _root(nullptr), _transferred_to_tree(false)
{

}

OpenTreeThread::~OpenTreeThread()
{
    _bstop.store(true);
    wait();
    // 仅在根节点未移交主界面树控件所有权时才负责释放临时根节点；
    // 一旦通过 CommitToTree 挂载至树控件，生命周期已完全由 QTreeWidget 接管，严禁解引用野指针触发 UAF
    if (!_transferred_to_tree && _root)
    {
        delete _root;
        _root = nullptr;
    }
}

void OpenTreeThread::CommitToTree()
{
    if (!_root || !_self || _bstop.load())
    {
        return;
    }
    if (!_transferred_to_tree)
    {
        ApplyOpenTreeIconsMainThread(_root);
        _self->addTopLevelItem(_root);
        _root->setExpanded(true);
        _transferred_to_tree = true; // 标记所有权已成功移交
    }
}

// 打开相册的核心逻辑：离屏构建相册顶层根节点并开始递归扫描
void OpenTreeThread::OpenProTree(const QString &src_path, int &file_count, QTreeWidget *self)
{
    // 1. 获取相册目录并提取纯文件夹名称作为相册名
    QDir src_dir(src_path);
    auto name = src_dir.dirName();

    // 2. 离屏创建相册的顶层项目节点（不立即挂载到 QTreeWidget，避免跨线程直接操作 GUI 控件）
    auto *item = new ProTreeItem(static_cast<QTreeWidget*>(nullptr), name, src_path, TreeItemPro);

    // 3. 设置节点在界面树上的展示属性
    item->setData(0, Qt::DisplayRole, name);     // 显示的相册标题文字
    item->setData(0, Qt::ToolTipRole, src_path); // 鼠标悬停时的完整路径提示

    // 4. 记录当前项目的根节点指针（供后续添加的照片子节点关联使用）
    _root = item;

    // 5. 初始化前驱节点指针，并递归扫描相册子文件与子文件夹
    QTreeWidgetItem *preitem = nullptr;
    RecursiveProTree(src_path, file_count, self, _root, item, preitem);

    // 如果扫描中途用户点击了“取消”，直接释放离屏构建的未完成节点并退出
    if (_bstop.load())
    {
        delete _root;
        _root = nullptr;
        return;
    }

    // 6. 全部扫描完成，发送完成信号通知主线程挂载节点并关闭等待弹窗
    emit SigFinishProGress(file_count);
}

// QThread 线程入口：在后台线程中执行相册打开逻辑
void OpenTreeThread::run()
{
    OpenProTree(_src_path, _file_count, _self);
}

// 递归遍历相册目录结构，构建树节点并串联照片双向链表
void OpenTreeThread::RecursiveProTree(const QString &src_path, int &file_count, QTreeWidget *self, QTreeWidgetItem *root, QTreeWidgetItem *parent, QTreeWidgetItem* &preitem)
{
    QDir src_dir(src_path);
    src_dir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    src_dir.setSorting(QDir::Name);
    QFileInfoList list = src_dir.entryInfoList();

    for (int i = 0; i < list.size(); i++)
    {
        // 循环每一步都检查用户是否点击了取消操作
        if (_bstop.load())
        {
            return;
        }

        QFileInfo fileInfo = list.at(i);
        bool bIsDir = fileInfo.isDir();

        if (bIsDir)
        {
            if (_bstop.load())
            {
                return;
            }

            file_count++;
            emit SigUpdataProGress(file_count);

            // 在树中创建文件夹子节点（TreeItemDir 类型）
            auto *item = new ProTreeItem(parent, fileInfo.fileName(), fileInfo.absoluteFilePath(), _root, TreeItemDir);
            item->setData(0, Qt::DisplayRole, fileInfo.fileName());
            item->setData(0, Qt::ToolTipRole, fileInfo.absoluteFilePath());

            // 递归进入子文件夹进行扫描，传递 preitem 引用以保持全相册照片顺序串联
            RecursiveProTree(fileInfo.absoluteFilePath(), file_count, self, root, item, preitem);
        }
        else
        {
            if (_bstop.load())
            {
                return;
            }

            // 统一转为小写以支持 .png / .jpg / .jpeg / .bmp / .webp 各种大小写格式（使用 suffix 避免带句点的多段文件名如 trip.2026.jpg 被跳过）
            const QString suffix = fileInfo.suffix().toLower();
            if (suffix != "png" && suffix != "jpeg" && suffix != "jpg" && suffix != "bmp" && suffix != "webp")
            {
                continue; // 跳过非图片文件
            }

            file_count++;
            emit SigUpdataProGress(file_count);

            // 在树中创建图片子节点（TreeItemPic 类型）
            auto *item = new ProTreeItem(parent, fileInfo.fileName(), fileInfo.absoluteFilePath(), _root, TreeItemPic);
            item->setData(0, Qt::DisplayRole, fileInfo.fileName());
            item->setData(0, Qt::ToolTipRole, fileInfo.absoluteFilePath());

            // 双向链表串联：构建上一张/下一张的线性前后关系
            if (preitem)
            {
                auto *pre_proitem = dynamic_cast<ProTreeItem*>(preitem);
                pre_proitem->SetNextItem(item);
            }
            item->SetPreItem(preitem);

            // 更新 preitem 为当前照片，作为后续照片的前驱
            preitem = item;
        }
    }
}

// 响应取消按钮，将停止标记置为 true
void OpenTreeThread::SlotCanceled()
{
    this->_bstop.store(true);
}
