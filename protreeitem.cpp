#include "protreeitem.h"

ProTreeItem::ProTreeItem(QTreeWidget* view, const QString &name, const QString &path, int type)
    : QTreeWidgetItem(view, type)
    , _path(path)
    , _name(name)
    , _root(this)
{
}

ProTreeItem::ProTreeItem(QTreeWidgetItem* parent, const QString &name, const QString &path, QTreeWidgetItem* root, int type)
    : QTreeWidgetItem(parent, type)
    , _path(path)
    , _name(name)
    , _root(root)
{
}
