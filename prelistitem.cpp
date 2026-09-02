#include "prelistitem.h"
PreListItem::PreListItem(const QIcon &icon, const QString &text,
                         const int &index, QListWidget *listview, int type)
    :QListWidgetItem(icon,"",listview,type),_path(text),_index(index)
{

}

int PreListItem::GetIndex()
{
    return _index;
}

QString PreListItem::GetPath()
{
    return _path;
}
