#include "opentreethread.h"
#include <QDir>
#include "const.h"
#include "protreeitem.h"

// 构造函数：初始化线程运行所需的相册路径、计数器及父对象
OpenTreeThread::OpenTreeThread(const QString &src_path, int file_count, QTreeWidget *self, QObject *parent)
    : QThread(parent), _bstop(false), _src_path(src_path), _file_count(file_count), _self(self), _root(nullptr)
{

}

// 打开相册的核心逻辑：构建相册顶层根节点并开始递归扫描
void OpenTreeThread::OpenProTree(const QString &src_path, int &file_count, QTreeWidget *self)
{
    // 1. 获取相册目录并提取纯文件夹名称作为相册名
    QDir src_dir(src_path);
    auto name = src_dir.dirName();

    // 2. 创建相册的顶层项目节点（类型为 TreeItemPro 文件夹项目）
    auto *item = new ProTreeItem(self, name, src_path, TreeItemPro);

    // 3. 设置节点在界面树上的展示属性
    item->setData(0, Qt::DisplayRole, name);                       // 显示的相册标题文字
    item->setData(0, Qt::DecorationRole, QIcon(":/icon/dir.png")); // 左侧显示的文件夹图标
    item->setData(0, Qt::ToolTipRole, src_path);                   // 鼠标悬停时的完整路径提示

    // 4. 记录当前项目的根节点指针（供后续添加的照片子节点关联使用）
    _root = item;

    // 5. 初始化前驱节点指针，并递归扫描相册子文件与子文件夹
    QTreeWidgetItem *preitem = nullptr;
    RecursiveProTree(src_path, file_count, self, _root, item, preitem);

    // 6. 扫描完成后自动展开相册根节点，使内部照片立即可见
    if(_root)
    {
        _root->setExpanded(true);
    }

    // 7. 全部扫描完成，发送完成信号通知主线程关闭等待弹窗
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

    for(int i = 0; i < list.size(); i++)
    {
        // 循环每一步都检查用户是否点击了取消操作
        if(_bstop)
        {
            return;
        }

        QFileInfo fileInfo = list.at(i);
        bool bIsDir = fileInfo.isDir();

        if(bIsDir)
        {
            if(_bstop)
            {
                return;
            }

            file_count++;
            emit SigUpdataProGress(file_count);

            // 在树中创建文件夹子节点（TreeItemDir 类型）
            auto *item = new ProTreeItem(parent, fileInfo.fileName(), fileInfo.absoluteFilePath(), _root, TreeItemDir);
            item->setData(0, Qt::DisplayRole, fileInfo.fileName());
            item->setData(0, Qt::DecorationRole, QIcon(":/icon/dir.png"));
            item->setData(0, Qt::ToolTipRole, fileInfo.absoluteFilePath());

            // 递归进入子文件夹进行扫描，传递 preitem 引用以保持全相册照片顺序串联
            RecursiveProTree(fileInfo.absoluteFilePath(), file_count, self, root, item, preitem);
        }
        else
        {
            if(_bstop)
            {
                return;
            }

            // 统一转为小写以支持 .png / .jpg / .jpeg / .bmp 各种大小写格式
            const QString suffix = fileInfo.completeSuffix().toLower();
            if(suffix != "png" && suffix != "jpeg" && suffix != "jpg" && suffix != "bmp")
            {
                continue; // 跳过非图片文件
            }

            file_count++;
            emit SigUpdataProGress(file_count);

            // 在树中创建图片子节点（TreeItemPic 类型）
            auto *item = new ProTreeItem(parent, fileInfo.fileName(), fileInfo.absoluteFilePath(), _root, TreeItemPic);
            item->setData(0, Qt::DisplayRole, fileInfo.fileName());
            item->setData(0, Qt::DecorationRole, QIcon(":/icon/core.png"));
            item->setData(0, Qt::ToolTipRole, fileInfo.absoluteFilePath());

            // 双向链表串联：构建上一张/下一张的线性前后关系
            if(preitem)
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
    this->_bstop = true;
}
