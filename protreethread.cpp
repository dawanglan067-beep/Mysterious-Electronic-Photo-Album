#include "protreethread.h"
#include <QDir>
#include "protreeitem.h"
#include "const.h"

// 构造函数：初始化成员变量，注意 _bstop 初始为 false（不停止）
ProTreeThread::ProTreeThread(const QString &src_path, const QString &dis_path, QTreeWidgetItem *parent_item,
                             int file_count, QTreeWidget *self, QTreeWidgetItem *root, QObject *parent)
    : QThread(parent)
    , _src_path(src_path)
    , _dis_path(dis_path)
    , _file_count(file_count)
    , _parent_item(parent_item)
    , _self(self)
    , _root(root)
    , _bstop(false)
{

}

ProTreeThread::~ProTreeThread()
{

}

// 线程实际执行的后台入口函数
void ProTreeThread::run()
{
    // 1. 初始化链表前驱节点指针，并在后台开始扫描并导入图片
    QTreeWidgetItem *preItem = nullptr;
    CreateProTree(_src_path, _dis_path, _parent_item, _file_count, _self, _root, preItem);

    // 2. 如果扫描中途用户点击了“取消”，清理未完成目录并退出，不发完成信号
    if (_bstop)
    {
        auto path=dynamic_cast<ProTreeItem*>(_root)->GetPath();
        auto index=_self->indexOfTopLevelItem(_root);
        delete _self->takeTopLevelItem(index);
        QDir dir(path);
        dir.removeRecursively();
        return;
    }

    // 3. 全部扫描导入完毕！发送“完成”信号，通知主界面关闭等待弹窗
    emit SigFinishProgress(_file_count);
}

// 递归创建相册目录树
void ProTreeThread::CreateProTree(const QString &src_path, const QString &dist_path, QTreeWidgetItem *parent_item, int &file_count, QTreeWidget *self, QTreeWidgetItem *root, QTreeWidgetItem* &preItem)
{
    // 每次进入函数先检查用户是否取消
    if (_bstop)
    {
        return;
    }

    // 判断是否需要拷贝：如果源路径和目标路径相同，说明是打开已有相册，不需要重复拷贝
    bool needcopy = true;
    if (src_path == dist_path)
    {
        needcopy = false;
    }

    QDir import_dir(src_path);
    QStringList nameFileters;
    // 过滤保留文件夹和文件，并排除 . 和 .. 避免死循环
    import_dir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    // 设置排序：按文件名升序排序
    import_dir.setSorting(QDir::Name);

    // 一次性获取当前文件夹下的所有条目信息
    QFileInfoList list = import_dir.entryInfoList();

    // 循环遍历每个条目
    for (int i = 0; i < list.size(); i++)
    {
        // 每次循环都检查用户是否点击了取消
        if (_bstop)
        {
            return;
        }

        // 取出第 i 个条目的详细信息
        QFileInfo fileinfo = list.at(i);
        // 判断是否是文件夹
        bool bIsDir = fileinfo.isDir();

        if (bIsDir)
        {
            // 如果是文件夹：先检查用户是否点击了取消
            if (_bstop)
            {
                return;
            }

            // 统计计数 +1 并发送进度更新信号
            file_count++;
            emit SigUpdateProgress(file_count);

            // 拼接目标相册中的子文件夹路径（名字不变，换到相册目录下）
            QDir dist_dir(dist_path);
            QString sub_dist_path = dist_dir.absoluteFilePath(fileinfo.fileName());

            // 在硬盘上创建该子文件夹（如果不存在）
            QDir sub_dist_dir(sub_dist_path);
            if (!sub_dist_dir.exists())
            {
                bool ok = sub_dist_dir.mkpath(sub_dist_path);
                if (!ok)
                {
                    continue; // 创建失败则跳过该文件夹
                }
            }

            // 在树控件中创建文件夹节点（类型为 TreeItemDir）
            auto *item = new ProTreeItem(parent_item, fileinfo.fileName(), sub_dist_path, root, TreeItemDir);
            item->setData(0, Qt::DisplayRole, fileinfo.fileName());          // 显示文件夹名字
            item->setData(0, Qt::DecorationRole, QIcon(":/icon/dir.png"));   // 显示文件夹黄色图标
            item->setData(0, Qt::ToolTipRole, sub_dist_path);              // 鼠标悬停显示完整路径
            CreateProTree(fileinfo.absoluteFilePath(),sub_dist_path,item,file_count,self,root,preItem);
        }
        else
        {
            // 如果是普通文件
            if (_bstop)
            {
                return;
            }

            // 获取文件完整的后缀名
            const QString &suffix = fileinfo.completeSuffix();
            // 图片过滤器：如果不是 png、jpeg、jpg 就直接跳过
            if (suffix != "png" && suffix != "jpeg" && suffix != "jpg")
            {
                continue;
            }

            // 确认为有效图片，图片总数计数器 +1
            file_count++;

            // 发送进度更新信号，通知主界面进度条往前涨
            emit SigUpdateProgress(file_count);

            // 1. 拼接图片在目标相册文件夹中的完整目标绝对路径
            QDir dis_dir(dist_path);
            QString dist_file_path = dis_dir.absoluteFilePath(fileinfo.fileName());

            // 2. 文件拷贝判定：
            //    - 只有在“需要拷贝”（源路径与目标相册路径不同）时才执行复制操作；
            //    - 先通过 QFile::exists 检查目标文件是否已存在，避免重复覆盖拷贝；
            //    - 无论是否需要拷贝（或者拷贝已存在），都不中断流程，保证下方能正常创建树节点。
            if (needcopy)
            {
                if (!QFile::exists(dist_file_path)) {
                    QFile::copy(fileinfo.absoluteFilePath(), dist_file_path);
                }
            }

            // 3. 在相册树目录中创建图片节点（类型为 TreeItemPic）：
            //    - parent_item: 挂载在当前所属的文件夹/相册根节点下
            //    - fileinfo.fileName(): 界面上显示的图片文件名
            //    - dist_file_path: 该图片在相册目录中的绝对物理路径
            //    - root: 当前所属相册顶层根节点
            auto *item = new ProTreeItem(parent_item, fileinfo.fileName(), dist_file_path, root, TreeItemPic);
            item->setData(0, Qt::DisplayRole, fileinfo.fileName());          // 设置第0列显示文件名文本
            item->setData(0, Qt::DecorationRole, QIcon(":/icon/core.png"));   // 设置左侧小图标
            item->setData(0, Qt::ToolTipRole, dist_file_path);              // 鼠标悬停显示完整物理路径

            // 4. 双向链表串联（用于 PicShow 和 轮播图 中上一张/下一张无缝切换）：
            if (preItem)
            {
                // 如果存在前驱节点，将前一张照片的 next 指向当前照片
                auto *pre_proitem = dynamic_cast<ProTreeItem*>(preItem);
                pre_proitem->SetNextItem(item);
            }
            // 将当前照片的 pre 指向前驱照片（若为相册第一张则为 nullptr）
            item->SetPreItem(preItem);

            // 将 preItem 指针更新为当前照片，作为后续扫描到的下一张照片的前驱
            preItem = item;
        }
    }
}

void ProTreeThread::SlotCanelProgress()
{
    this->_bstop=true;
}

// 响应取消操作的槽函数
void ProTreeThread::SlotCanceled()
{
    this->_bstop = true; // 将停止标志位置为 true
}

