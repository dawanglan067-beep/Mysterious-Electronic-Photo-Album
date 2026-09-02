#ifndef PICSTATEBTN_H
#define PICSTATEBTN_H

#include <QPushButton>
#include <QEvent>

class PicStateBtn : public QPushButton
{
    Q_OBJECT
public:
    PicStateBtn(QWidget* parent = nullptr);
    virtual ~PicStateBtn();

    // 设置双状态的图标路径（状态1：正常/悬浮/按下；状态2：正常/悬浮/按下）
    void SetIcons(const QString &normal, const QString &hover, const QString &pressed,
                  const QString &normal_2, const QString &hover_2, const QString &pressed_2);

protected:
    // 事件过滤/处理函数：统一捕获鼠标进入、离开、按下、释放事件
    bool event(QEvent *e) override;

private:
    // ===== 状态 1（播放状态）图标设置 =====
    void setNormalIcon();   // 状态1：正常显示（默认显示播放图标）
    void setHoverIcon();    // 状态1：鼠标移入悬停高亮
    void setPressedIcon();  // 状态1：鼠标点击按下

    // ===== 状态 2（暂停状态）图标设置 =====
    void setNormal2Icon();  // 状态2：正常显示（默认显示暂停图标）
    void setHover2Icon();   // 状态2：鼠标移入悬停高亮
    void setPressed2Icon(); // 状态2：鼠标点击按下

    // 状态 1 对应的三态图片路径
    QString _normal;
    QString _hover;
    QString _pressed;

    // 状态 2 对应的三态图片路径
    QString _normal_2;
    QString _hover_2;
    QString _pressed_2;
    int _cur_state; // 记录当前按钮所处的交互状态枚举值（PicBtnState）
public slots:
    void SlotStart();
    void SlotStop();
};

#endif // PICSTATEBTN_H
