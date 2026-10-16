#ifndef PROTREETHREAD_H
#define PROTREETHREAD_H

#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStringList>
#include <QSet>
#include <atomic>
#include <memory>

class ProTreeThread : public QThread
{
    Q_OBJECT // 支持信号与槽机制

public:
    // 构造函数：传入源路径、目标路径、父节点、文件计数、树控件、根节点等
    ProTreeThread(const QString &src_path, const QString &dis_path, QTreeWidgetItem *parent_item,
                  int file_count, QTreeWidget *self, QTreeWidgetItem *root, QObject *parent = nullptr);
    ~ProTreeThread();

    // 在主线程中将后台构建好的节点安全挂载到目录树，并串联前后双向链表
    void CommitToTree();

protected:
    // 线程实际执行的后台任务（必须重写 run）
    virtual void run() override;

private:
    // 递归创建相册目录树节点，并利用 preItem 引用传递正确维护双向链表前后关系
    void CreateProTree(const QString &src_path, const QString &dist_path, QTreeWidgetItem *parent_item,
                       int &file_count, QTreeWidget *self, QTreeWidgetItem *root, QTreeWidgetItem* &preItem);

    // 取消导入时回滚本次新建的文件和空目录（不破坏原有相册数据）
    void RollbackImport();

    // 成员变量
    QString _src_path;                              // 源文件夹路径（用户选中的图片文件夹）
    QString _dis_path;                              // 目标文件夹路径（相册实际存放的文件夹）
    int _file_count;                                // 统计图片数量的计数器
    QTreeWidgetItem *_parent_item;                  // 当前父节点
    QTreeWidget *_self;                             // 当前树控件指针
    QTreeWidgetItem *_root;                         // 相册根节点
    std::atomic<bool> _bstop;                       // 控制线程停止/取消的原子标志位
    std::atomic<bool> _is_committed;                // 标记本次导入是否已成功提交至主界面树控件
    std::unique_ptr<QTreeWidgetItem> _temp_parent;  // 后台线程离屏构建节点的临时容器（避免跨线程直接操作 GUI 树）
    QTreeWidgetItem *_first_new_pic;                // 本次导入的第一张图片节点（用于与原有相册末尾节点串联）
    QStringList _copied_files;                      // 记录本次实际新拷贝的文件路径，供取消时安全回滚
    QStringList _created_dirs;                      // 记录本次实际新建的子文件夹路径，供取消时安全回滚
    QSet<QString> _existing_pic_paths;              // 记录相册中已有的图片路径，防止重复导入产生同名重复节点

public slots:
    void SlotCanelProgress();
    void SlotCanceled();           // 响应外部取消操作的槽函数

signals:
    void SigUpdateProgress(int count); // 发送进度更新的信号（带当前图片计数）
    void SigFinishProgress(int count); // 发送导入完成的信号
};

#endif // PROTREETHREAD_H
