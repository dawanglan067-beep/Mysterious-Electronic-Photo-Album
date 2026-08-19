#ifndef PROTREEWIDGET_H
#define PROTREEWIDGET_H
#include<QSet>
#include <QTreeWidget>

class ProTreeWidget : public QTreeWidget
{
public:
    ProTreeWidget(QWidget *parent=nullptr);
    void AddProToTree(const QString name,const QString path);
private:
    QSet<QString> _set_path;
};

#endif // PROTREEWIDGET_H
