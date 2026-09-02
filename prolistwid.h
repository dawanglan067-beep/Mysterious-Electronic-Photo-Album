#ifndef PROLISTWID_H
#define PROLISTWID_H
#include<QTreeWidgetItem>
#include <QListWidget>

class ProListWid : public QListWidget
{
    Q_OBJECT
public:
    ProListWid(QWidget*parent=nullptr);
    virtual ~ProListWid();
private:
    QMap<QString ,QListWidgetItem*> _set_items;
    void AddListItem(const QString &path);
    int _global;
    QPoint _pos_origin;
    int last_index;

public slots:
    void SlotUpPreList(QTreeWidgetItem*tree_item);
    void SlotUpSelect(QTreeWidgetItem*tree_item);
    void SlotItemPressed(QListWidgetItem*item);
signals:
    void SigUpSelectShow(QString &path);
};

#endif // PROLISTWID_H
