#ifndef PROTREETHREAD_H
#define PROTREETHREAD_H

#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>

class ProTreeThread : public QThread
{
    Q_OBJECT // 支持信号与槽机制

public:
    // 构造函数：传入源路径、目标路径、父节点、文件计数、树控件、根节点等
    ProTreeThread(const QString &src_path, const QString &dis_path, QTreeWidgetItem *parent_item,
                  int file_count, QTreeWidget *self, QTreeWidgetItem *root, QObject *parent = nullptr);
    ~ProTreeThread();

protected:
    // 线程实际执行的后台任务（必须重写 run）
    virtual void run();

private:
    // 递归创建相册目录树节点，并利用 preItem 引用传递正确维护双向链表前后关系
    void CreateProTree(const QString &src_path, const QString &dist_path, QTreeWidgetItem *parent_item,
                       int &file_count, QTreeWidget *self, QTreeWidgetItem *root, QTreeWidgetItem* &preItem);

    // 成员变量
    QString _src_path;             // 源文件夹路径（用户选中的图片文件夹）
    QString _dis_path;             // 目标文件夹路径（相册实际存放的文件夹）
    int _file_count;               // 统计图片数量的计数器
    QTreeWidgetItem *_parent_item; // 当前父节点
    QTreeWidget *_self;            // 当前树控件指针
    QTreeWidgetItem *_root;        // 相册根节点
    bool _bstop;                   // 控制线程停止/取消的标志位
public slots:
    void SlotCanelProgress();

public:
    void SlotCanceled();           // 响应外部取消操作的槽函数

signals:
    void SigUpdateProgress(int count); // 发送进度更新的信号（带当前图片计数）
    void SigFinishProgress(int count); // 发送导入完成的信号
};

#endif // PROTREETHREAD_H


