#include "protreeitem.h"
#include"const.h"
//创建相册的根节点
ProTreeItem::ProTreeItem(QTreeWidget* view, const QString &name, const QString &path, int type)
    : QTreeWidgetItem(view, type)
    , _path(path)
    , _root(this)//根节点是自己
    ,_pre_item(nullptr)
    ,_next_item(nullptr)
{

}
//创建相册里的照片/子项
ProTreeItem::ProTreeItem(QTreeWidgetItem* parent, const QString &name, const QString &path, QTreeWidgetItem* root, int type)
    : QTreeWidgetItem(parent, type)
    , _path(path)
    , _root(root)//外面传来的相册
    ,_pre_item(nullptr)
    ,_next_item(nullptr)
{

}

void ProTreeItem::SetPreItem(QTreeWidgetItem *item)
{
    _pre_item=item;
}

void ProTreeItem::SetNextItem(QTreeWidgetItem *item)
{
    _next_item=item;
}

ProTreeItem *ProTreeItem::GetPreItem()
{
    return dynamic_cast<ProTreeItem*>(_pre_item);
}

ProTreeItem *ProTreeItem::GetNextItem()
{
    return dynamic_cast<ProTreeItem*>(_next_item);
}

// 递归获取当前节点下最后一张照片
ProTreeItem *ProTreeItem::GetLastPicChild()
{
    // 1. 如果自身就是图片节点，则它没有子图片，直接返回空
    if (this->type() == TreeItemPic)
    {
        return nullptr;
    }

    // 2. 检查是否有子节点
    auto child_count = this->childCount();
    if (child_count == 0)
    {
        return nullptr;
    }

    // 3. 从后往前逆序遍历所有子项，优先查找最末尾的图片
    for (int i = child_count - 1; i >= 0; i--)
    {
        auto *child_item = dynamic_cast<ProTreeItem*>(this->child(i));
        if (!child_item)
        {
            continue;
        }

        // 3.1 如果子项直接是图片，则该项即为最后的图片
        if (child_item->type() == TreeItemPic)
        {
            return child_item;
        }

        // 3.2 如果子项是目录，递归查找该目录下的最后一张图片
        auto *last_pic = child_item->GetLastPicChild();
        if (last_pic)
        {
            return last_pic;
        }
    }

    return nullptr;
}

// 递归获取当前节点下第一张照片
ProTreeItem *ProTreeItem::GetFirstPicChild()
{
    // 1. 如果自身就是图片节点，则它没有子图片，直接返回空
    if (this->type() == TreeItemPic)
    {
        return nullptr;
    }

    // 2. 检查是否有子节点
    auto child_count = this->childCount();
    if (child_count == 0)
    {
        return nullptr;
    }

    // 3. 从前往后正序遍历所有子项，优先查找最开头的图片
    for (int i = 0; i < child_count; i++)
    {
        auto *child_item = dynamic_cast<ProTreeItem*>(this->child(i));
        if (!child_item)
        {
            continue;
        }

        // 3.1 如果子项直接是图片，则该项即为第一张图片
        if (child_item->type() == TreeItemPic)
        {
            return child_item;
        }

        // 3.2 如果子项是目录，递归查找该目录下的第一张图片
        auto *first_pic = child_item->GetFirstPicChild();
        if (first_pic)
        {
            return first_pic;
        }
    }

    return nullptr;
}
