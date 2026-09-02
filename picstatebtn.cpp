#include "picstatebtn.h"
#include "const.h"

PicStateBtn::PicStateBtn(QWidget *parent)
    : QPushButton(parent)
{

}

PicStateBtn::~PicStateBtn()
{

}

// 初始化设置两组状态（共6张）图片的路径，并初始化按钮为“状态1-正常显示”
void PicStateBtn::SetIcons(const QString &normal, const QString &hover, const QString &pressed,
                           const QString &normal_2, const QString &hover_2, const QString &pressed_2)
{
    _normal = normal;
    _hover = hover;
    _pressed = pressed;
    _normal_2 = normal_2;
    _hover_2 = hover_2;
    _pressed_2 = pressed_2;

    // 默认以状态1的正常图标作为初始显示
    QPixmap tmpPixmap;
    tmpPixmap.load(normal);
    this->resize(tmpPixmap.size());
    this->setIcon(tmpPixmap);
    this->setIconSize(tmpPixmap.size());
    _cur_state = PicBtnStateNormal;
}

// 统一捕获处理鼠标交互事件（移入、移出、按下、释放）
bool PicStateBtn::event(QEvent *e)
{
    switch (e->type())
    {
        // 鼠标移入按钮区域
        case QEvent::Enter:
            if (_cur_state < PicBtnState2Normal)
            {
                setHoverIcon();    // 当前处于状态1 -> 显示状态1悬浮图标
            }
            else
            {
                setHover2Icon();   // 当前处于状态2 -> 显示状态2悬浮图标
            }
            break;

        // 鼠标移出按钮区域
        case QEvent::Leave:
            if (_cur_state < PicBtnState2Normal)
            {
                setNormalIcon();   // 当前处于状态1 -> 恢复状态1正常图标
            }
            else
            {
                setNormal2Icon();  // 当前处于状态2 -> 恢复状态2正常图标
            }
            break;

        // 鼠标按下按钮
        case QEvent::MouseButtonPress:
            if (_cur_state < PicBtnState2Normal)
            {
                setPressedIcon();  // 当前处于状态1 -> 显示状态1按下图标
            }
            else
            {
                setPressed2Icon(); // 当前处于状态2 -> 显示状态2按下图标
            }
            break;

        // 鼠标在按钮上释放（完成一次点击，执行状态大翻转：状态1 <-> 状态2）
        case QEvent::MouseButtonRelease:
            if (_cur_state < PicBtnState2Normal)
            {
                setHover2Icon();   // 状态1 切换到 状态2（由于鼠标仍在按钮内，显示状态2悬浮图标）
            }
            else
            {
                setHoverIcon();    // 状态2 切换回 状态1（由于鼠标仍在按钮内，显示状态1悬浮图标）
            }
            break;

        default:
            break;
    }

    // 调用基类 QPushButton 默认的事件处理，保证 click 信号等正常发射
    return QPushButton::event(e);
}

// ===================== 状态 1（如播放态）图标设置 =====================

// 状态1 正常显示
void PicStateBtn::setNormalIcon()
{
    QPixmap tmpPixmap;
    tmpPixmap.load(_normal);
    this->setIcon(tmpPixmap);
    _cur_state = PicBtnStateNormal; // 记录状态为：状态1-正常
}

// 状态1 鼠标悬停高亮
void PicStateBtn::setHoverIcon()
{
    QPixmap tmpPixmap;
    tmpPixmap.load(_hover);
    this->setIcon(tmpPixmap);
    _cur_state = PicBtnStateHover;  // 记录状态为：状态1-悬浮
}

// 状态1 鼠标按下
void PicStateBtn::setPressedIcon()
{
    QPixmap tmpPixmap;
    tmpPixmap.load(_pressed);
    this->setIcon(tmpPixmap);
    _cur_state = PicBtnStatePressed;// 记录状态为：状态1-按下
}

// ===================== 状态 2（如暂停态）图标设置 =====================

// 状态2 正常显示
void PicStateBtn::setNormal2Icon()
{
    QPixmap tmpPixmap;
    tmpPixmap.load(_normal_2);
    this->setIcon(tmpPixmap);
    _cur_state = PicBtnState2Normal; // 记录状态为：状态2-正常
}

// 状态2 鼠标悬停高亮
void PicStateBtn::setHover2Icon()
{
    QPixmap tmpPixmap;
    tmpPixmap.load(_hover_2);
    this->setIcon(tmpPixmap);
    _cur_state = PicBtnState2Hover;  // 记录状态为：状态2-悬浮
}

// 状态2 鼠标按下
void PicStateBtn::setPressed2Icon()
{
    QPixmap tmpPixmap;
    tmpPixmap.load(_pressed_2);
    this->setIcon(tmpPixmap);
    _cur_state = PicBtnState2Pressed;// 记录状态为：状态2-按下
}

void PicStateBtn::SlotStart()
{
    setNormal2Icon();
}

void PicStateBtn::SlotStop()
{
    setNormalIcon();
}
