#ifndef PROTREE_H
#define PROTREE_H

#include <QDialog>
#include<QTreeWidget>
namespace Ui {
class ProTree;
}

class ProTree : public QDialog
{
    Q_OBJECT

public:
    explicit ProTree(QWidget *parent = nullptr);
    ~ProTree();
    QTreeWidget*GetTreeWidget();
protected:
    void reject() override {}
private:
    Ui::ProTree *ui;

public slots:
    void AddProToTree(const QString name,const QString path);
};

#endif // PROTREE_H
