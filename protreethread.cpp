#include "protreethread.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QDebug>
#include "protreeitem.h"
#include "const.h"

namespace {
enum class FileComparison { Same, Different, Error, Canceled };

FileComparison CompareFileContents(const QString &sourcePath, const QString &targetPath,
                                   const std::atomic<bool> &canceled)
{
    QFile source(sourcePath);
    QFile target(targetPath);
    if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::ReadOnly))
        return FileComparison::Error;
    if (source.size() != target.size())
        return FileComparison::Different;

    qint64 remaining = source.size();
    while (remaining > 0)
    {
        if (canceled.load()) return FileComparison::Canceled;
        const qint64 chunkSize = qMin(remaining, qint64(64 * 1024));
        const QByteArray sourceChunk = source.read(chunkSize);
        const QByteArray targetChunk = target.read(chunkSize);
        if (sourceChunk.size() != chunkSize || targetChunk.size() != chunkSize)
            return FileComparison::Error;
        if (sourceChunk != targetChunk) return FileComparison::Different;
        remaining -= chunkSize;
    }
    return FileComparison::Same;
}

// 在 GUI 主线程中递归为新建节点绑定图标，避免后台线程访问 QPixmap/QIcon 绘图资源
void ApplyTreeItemIconsMainThread(QTreeWidgetItem *item)
{
    if (!item) return;
    if (item->type() == TreeItemDir || item->type() == TreeItemPro)
    {
        item->setData(0, Qt::DecorationRole, QIcon(item->type() == TreeItemPro ? ":/icon/line/album.svg" : ":/icon/line/dir.svg"));
    }
    else if (item->type() == TreeItemPic)
    {
        item->setData(0, Qt::DecorationRole, QIcon(":/icon/line/core.svg"));
    }
    for (int i = 0; i < item->childCount(); ++i)
    {
        ApplyTreeItemIconsMainThread(item->child(i));
    }
}

// 在主线程递归收集当前相册已有的图片节点路径，防止重复导入产生冗余节点
void CollectExistingPicPaths(QTreeWidgetItem *item, QSet<QString> &outPaths)
{
    if (!item) return;
    if (item->type() == TreeItemPic)
    {
        auto *proItem = dynamic_cast<ProTreeItem*>(item);
        if (proItem)
        {
            outPaths.insert(QDir::cleanPath(proItem->GetPath()));
        }
    }
    for (int i = 0; i < item->childCount(); ++i)
    {
        CollectExistingPicPaths(item->child(i), outPaths);
    }
}

// 在主线程将新建节点合并挂载到目标父节点，若同名子目录已存在则复用合并，避免目录重复
void MergeTreeChildrenMainThread(QTreeWidgetItem *dstParent, QTreeWidgetItem *srcTempParent)
{
    if (!dstParent || !srcTempParent) return;
    QList<QTreeWidgetItem*> children = srcTempParent->takeChildren();
    for (QTreeWidgetItem *child : children)
    {
        if (child->type() == TreeItemDir)
        {
            auto *childPro = dynamic_cast<ProTreeItem*>(child);
            QString childPath = childPro ? QDir::cleanPath(childPro->GetPath()) : QString();
            QTreeWidgetItem *existingDir = nullptr;
            for (int i = 0; i < dstParent->childCount(); ++i)
            {
                QTreeWidgetItem *dstChild = dstParent->child(i);
                if (dstChild && dstChild->type() == TreeItemDir)
                {
                    auto *dstChildPro = dynamic_cast<ProTreeItem*>(dstChild);
                    if (dstChildPro && QDir::cleanPath(dstChildPro->GetPath()) == childPath)
                    {
                        existingDir = dstChild;
                        break;
                    }
                }
            }
            if (existingDir)
            {
                MergeTreeChildrenMainThread(existingDir, child);
                delete child;
                continue;
            }
        }
        ApplyTreeItemIconsMainThread(child);
        dstParent->addChild(child);
    }
}

// 在主线程深度优先收集指定根节点下所有的图片节点
void CollectPicItemsDepthFirst(QTreeWidgetItem *item, QList<ProTreeItem*> &outList)
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
        CollectPicItemsDepthFirst(item->child(i), outList);
    }
}

// 严密重建相册内全部图片节点的双向链表，使其与目录树展示拓扑顺序严格一致
void RebuildPicLinkedListMainThread(QTreeWidgetItem *rootItem)
{
    if (!rootItem) return;
    QList<ProTreeItem*> picList;
    CollectPicItemsDepthFirst(rootItem, picList);

    for (int i = 0; i < picList.size(); ++i)
    {
        picList[i]->SetPreItem(i > 0 ? picList[i - 1] : nullptr);
        picList[i]->SetNextItem(i + 1 < picList.size() ? picList[i + 1] : nullptr);
    }
}
} // namespace

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
    , _is_committed(false)
    , _first_new_pic(nullptr)
{
    CollectExistingPicPaths(_root, _existing_pic_paths);
}

ProTreeThread::~ProTreeThread()
{
    _bstop = true;
    wait();
    // 若本次导入尚未成功提交至主界面树控件（中途取消、异常退出或完成槽未执行），
    // 强制执行事务回滚，清理本次已落盘的孤儿文件与空子目录，确保数据完整性
    if (!_is_committed.load())
    {
        RollbackImport();
    }
}

// 线程实际执行的后台入口函数
void ProTreeThread::run()
{
    // 1. 创建离屏临时父节点并在后台开始扫描并导入图片（不直接触碰 GUI 树控件）
    _temp_parent = std::make_unique<QTreeWidgetItem>();
    _first_new_pic = nullptr;
    _copied_files.clear();
    _created_dirs.clear();

    QTreeWidgetItem *preItem = nullptr;
    CreateProTree(_src_path, _dis_path, _temp_parent.get(), _file_count, _self, _root, preItem);

    // 2. 如果扫描中途用户点击了“取消”，仅回滚本次导入新产生的文件与临时节点，保留相册原有数据
    if (_bstop.load())
    {
        RollbackImport();
        return;
    }

    // 3. 全部扫描导入完毕！发送“完成”信号，通知主线程挂载节点并关闭等待弹窗
    emit SigFinishProgress(_file_count);
}

void ProTreeThread::CommitToTree()
{
    if (!_temp_parent || !_parent_item || !_root || _bstop.load())
    {
        return;
    }

    MergeTreeChildrenMainThread(_parent_item, _temp_parent.get());
    _parent_item->setExpanded(true);

    // 重新按目录树深度优先几何顺序严密重建相册内部全部图片节点的双向链表，
    // 彻底解决向中间已有子目录追加导入导致旧末尾节点覆盖、断链与孤立照片的问题
    RebuildPicLinkedListMainThread(_root);

    _is_committed.store(true);
    _temp_parent.reset();
}

void ProTreeThread::RollbackImport()
{
    // 释放离屏临时树节点（不会影响主界面已有的相册树节点）
    _temp_parent.reset();
    _first_new_pic = nullptr;

    // 仅删除本次导入过程中新拷贝的文件
    for (const QString &filePath : std::as_const(_copied_files))
    {
        QFile::remove(filePath);
    }
    _copied_files.clear();

    // 逆序删除本次导入过程中新建的空子目录（由深到浅）
    for (int i = _created_dirs.size() - 1; i >= 0; --i)
    {
        QDir dir(_created_dirs.at(i));
        if (dir.exists() && dir.isEmpty())
        {
            dir.rmdir(".");
        }
    }
    _created_dirs.clear();
}

// 递归创建相册目录树
void ProTreeThread::CreateProTree(const QString &src_path, const QString &dist_path, QTreeWidgetItem *parent_item, int &file_count, QTreeWidget *self, QTreeWidgetItem *root, QTreeWidgetItem* &preItem)
{
    // 每次进入函数先检查用户是否取消
    if (_bstop.load())
    {
        return;
    }

    // 判断是否需要拷贝：如果源路径和目标路径相同，说明是打开已有相册，不需要重复拷贝
    bool needcopy = true;
    if (QDir::cleanPath(src_path) == QDir::cleanPath(dist_path))
    {
        needcopy = false;
    }

    QDir import_dir(src_path);
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
        if (_bstop.load())
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
            if (_bstop.load())
            {
                return;
            }

            // 关键安全校验：若待扫描源目录与目标相册目录相同、或是目标相册目录自身/其内部子目录，
            // 坚决跳过，彻底杜绝当“导入源目录包含目标相册目录”时自我嵌套递归无限创建 album/album/... 导致死循环与磁盘耗尽
            const QString subSrcPath = fileinfo.absoluteFilePath();
            QString cleanSubSrc = QFileInfo(subSrcPath).canonicalFilePath();
            if (cleanSubSrc.isEmpty())
            {
                cleanSubSrc = QDir::cleanPath(subSrcPath);
            }

            QString cleanDisRoot = QFileInfo(_dis_path).canonicalFilePath();
            if (cleanDisRoot.isEmpty())
            {
                cleanDisRoot = QDir::cleanPath(_dis_path);
            }

#ifdef Q_OS_WIN
            const auto cs = Qt::CaseInsensitive;
#else
            const auto cs = Qt::CaseSensitive;
#endif

            if (cleanSubSrc.compare(cleanDisRoot, cs) == 0 ||
                cleanSubSrc.startsWith(cleanDisRoot + "/", cs) ||
                cleanSubSrc.startsWith(cleanDisRoot + "\\", cs))
            {
                continue;
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
                _created_dirs.append(sub_dist_path);
            }

            // 在离屏临时容器中创建文件夹节点（类型为 TreeItemDir）
            auto *item = new ProTreeItem(parent_item, fileinfo.fileName(), sub_dist_path, root, TreeItemDir);
            item->setData(0, Qt::DisplayRole, fileinfo.fileName());          // 显示文件夹名字
            item->setData(0, Qt::ToolTipRole, sub_dist_path);                // 鼠标悬停显示完整路径
            CreateProTree(fileinfo.absoluteFilePath(), sub_dist_path, item, file_count, self, root, preItem);

            // 若该子文件夹内没有新增任何有效图片（例如全部已存在），移除空文件夹节点
            if (item->childCount() == 0 && !_bstop.load())
            {
                parent_item->removeChild(item);
                delete item;
            }
        }
        else
        {
            // 如果是普通文件
            if (_bstop.load())
            {
                return;
            }

            // 获取文件后缀名并统一转为小写（使用 suffix 避免带句点的多段文件名如 trip.2026.jpg 被跳过）
            const QString suffix = fileinfo.suffix().toLower();
            // 图片过滤器：支持 png、jpeg、jpg、bmp、webp 常见格式
            if (suffix != "png" && suffix != "jpeg" && suffix != "jpg" && suffix != "bmp" && suffix != "webp")
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

            // 同名不代表同一张照片。逐个检查原名及编号名，重复导入时也可复用已有副本。
            if (needcopy)
            {
                int suffixIndex = 0;
                bool comparisonFailed = false;
                while (QFileInfo::exists(dist_file_path))
                {
                    if (_bstop.load()) return;
                    if (QFileInfo(dist_file_path).isFile())
                    {
                        const auto result = CompareFileContents(
                            fileinfo.absoluteFilePath(), dist_file_path, _bstop);
                        if (result == FileComparison::Canceled) return;
                        if (result == FileComparison::Error)
                        {
                            qWarning() << "Import: cannot compare files"
                                       << fileinfo.absoluteFilePath() << dist_file_path;
                            comparisonFailed = true;
                            break;
                        }
                        if (result == FileComparison::Same) break;
                    }
                    dist_file_path = dis_dir.absoluteFilePath(
                        fileinfo.completeBaseName() + "_" + QString::number(++suffixIndex)
                        + "." + fileinfo.suffix());
                }
                if (comparisonFailed) continue;
                if (_bstop.load()) return;
                if (!QFile::exists(dist_file_path))
                {
                    if (QFile::copy(fileinfo.absoluteFilePath(), dist_file_path))
                    {
                        _copied_files.append(dist_file_path);
                    }
                    else
                    {
                        // 复制失败，跳过该文件，不记录路径、不创建树节点，杜绝数据损坏与幽灵节点
                        continue;
                    }
                }
            }

            const QString clean_dist_path = QDir::cleanPath(dist_file_path);
            if (_existing_pic_paths.contains(clean_dist_path)) continue;
            _existing_pic_paths.insert(clean_dist_path);

            // 3. 在离屏临时容器中创建图片节点（类型为 TreeItemPic）：
            //    - parent_item: 挂载在当前所属的文件夹/临时根节点下
            //    - fileinfo.fileName(): 界面上显示的图片文件名
            //    - dist_file_path: 该图片在相册目录中的绝对物理路径
            //    - root: 当前所属相册顶层根节点
            const QString savedName = QFileInfo(dist_file_path).fileName();
            auto *item = new ProTreeItem(parent_item, savedName, dist_file_path, root, TreeItemPic);
            item->setData(0, Qt::DisplayRole, savedName);
            item->setData(0, Qt::ToolTipRole, dist_file_path);               // 鼠标悬停显示完整物理路径

            // 4. 双向链表串联（用于 PicShow 和 轮播图 中上一张/下一张无缝切换）：
            if (!_first_new_pic)
            {
                _first_new_pic = item;
            }
            if (preItem)
            {
                // 如果存在前驱节点，将前一张照片的 next 指向当前照片
                auto *pre_proitem = dynamic_cast<ProTreeItem*>(preItem);
                pre_proitem->SetNextItem(item);
            }
            // 将当前照片的 pre 指向前驱照片（若为本次导入第一张则稍后在 CommitToTree 中与旧末尾节点连接）
            item->SetPreItem(preItem);

            // 将 preItem 指针更新为当前照片，作为后续扫描到的下一张照片的前驱
            preItem = item;
        }
    }
}

// 响应取消操作的槽函数
void ProTreeThread::SlotCanceled()
{
    this->_bstop.store(true); // 将停止标志位置为 true
}
