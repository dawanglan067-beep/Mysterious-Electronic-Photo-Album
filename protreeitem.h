#ifndef PROTREEITEM_H
#define PROTREEITEM_H

#include <QTreeWidget>
#include <QTreeWidgetItem>

class ProTreeItem : public QTreeWidgetItem
{
public:
    ProTreeItem(QTreeWidget* view, const QString &name, const QString &path, int type = Type);
    ProTreeItem(QTreeWidgetItem* parent, const QString &name, const QString &path, QTreeWidgetItem* root, int type = Type);
    virtual ~ProTreeItem() override = default;
    const QString& GetPath() const { return _path; }
    QTreeWidgetItem* GetRoot() const { return _root; }
    void SetPreItem(QTreeWidgetItem*item);
    void SetNextItem(QTreeWidgetItem*item);
    ProTreeItem*GetPreItem();
    ProTreeItem*GetNextItem();
    ProTreeItem*GetLastPicChild();
    ProTreeItem*GetFirstPicChild();
private:
    QString _path;
    QString _name;
    QTreeWidgetItem* _root;
    QTreeWidgetItem*_pre_item;//上一张照片
    QTreeWidgetItem*_next_item;//下一张照片
};

#endif // PROTREEITEM_H
