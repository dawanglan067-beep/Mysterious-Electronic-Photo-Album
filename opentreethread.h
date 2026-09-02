#ifndef OPENTREETHREAD_H
#define OPENTREETHREAD_H
#include <QThread>
#include<QTreeWidget>
class OpenTreeThread : public QThread
{
    Q_OBJECT
public:
    // 构造函数：初始化相册路径、文件计数器及树控件指针
    explicit OpenTreeThread(const QString &path, int file_count, QTreeWidget *self, QObject *parent = nullptr);

    // 打开相册的核心逻辑入口：构建根节点并启动递归遍历
    void OpenProTree(const QString &src_path, int &file_count, QTreeWidget *self);

protected:
    // 线程后台执行入口
    virtual void run();

private:
    // 递归扫描相册目录，构建目录/照片树节点，并通过引用传递 preitem 正确串联双向链表
    void RecursiveProTree(const QString &src_path, int &file_count, QTreeWidget *self,
                          QTreeWidgetItem *root, QTreeWidgetItem *parent, QTreeWidgetItem* &preitem);

    QString _src_path;      // 需要打开的相册文件夹绝对路径
    int _file_count;        // 图片文件计数器
    QTreeWidget *_self;     // 主界面树控件指针
    bool _bstop;            // 线程取消/中断标志位
    QTreeWidgetItem *_root; // 当前相册的根节点指针

signals:
    void SigFinishProGress(int); // 扫描导入完成信号（通知主线程关闭进度弹窗）
    void SigUpdataProGress(int); // 扫描进度更新信号（通知主线程更新进度条）

public slots:
    void SlotCanceled();         // 响应外部取消操作的槽函数
};

#endif // OPENTREETHREAD_H
