#ifndef PICSHOW_H
#define PICSHOW_H

#include <QDialog>
#include<QPropertyAnimation>
namespace Ui {
class PicShow;
}

class PicShow : public QDialog
{
    Q_OBJECT

public:
    explicit PicShow(QWidget *parent = nullptr);
    ~PicShow();

    // 重新加载/刷新当前图片显示（触发 label 重新绘制）
    void ReloadPic();

    // 清空当前图片数据并重绘清空展示区
    void Clear();

protected:
    // 监听鼠标进入/离开事件，用于控制上一张/下一张按钮的淡入淡出动画
    bool event(QEvent *event) override;

    // 事件过滤器：拦截 ui->label 的 Paint 事件，实现图片无损平滑缩放与居中绘制
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 播放按钮透明度动画：b_show 为 true 表示淡入显示，false 表示淡出隐藏
    void ShowPreNextBtns(bool b_show);

    Ui::PicShow *ui;
    QPropertyAnimation* _animation_show_pre;   // 上一张按钮的透明度动画
    QPropertyAnimation* _animation_show_next;  // 下一张按钮的透明度动画
    bool _b_btnvisible;                       // 标记当前按钮是否处于可见状态
    QString _selected_path;                   // 当前正在展示的图片本地绝对路径
    QPixmap Original_map;                     // 当前图片的高清原图数据（避免多次缩放导致画质退化）

public slots:
    // 树节点被双击选中时的图片加载槽函数
    void SlotSelectItem(const QString &path);

    // 切换上一张/下一张时的图片更新槽函数
    void SlotUpdatePic(const QString &path);

    // 外部清空图片槽函数（如相册关闭/删除时调用）
    void SlotClear();

signals:
    void SigNextClicked(); // 点击下一张按钮时发射的信号
    void SigPreClicked();  // 点击上一张按钮时发射的信号
};

#endif // PICSHOW_H
