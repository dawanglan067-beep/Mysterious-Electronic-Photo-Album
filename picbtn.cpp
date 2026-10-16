#include "picbtn.h"
#include <QPixmap>
#include <QEvent>
#include <QMouseEvent>
PicBtn::PicBtn(QWidget* parent)
    : QPushButton(parent)
{

}

void PicBtn::SetIcons(const QString &normal, const QString &hover, const QString &pressed)
{
    setIconSize(QSize(28, 28));
    setMinimumSize(44, 44);
    setCursor(Qt::PointingHandCursor);
    _normal = normal;
    _hover = hover;
    _pressed = pressed;
    setIcon(QIcon(normal));
}

bool PicBtn::event(QEvent *e)
{
    switch(e->type())
    {
    case QEvent::Enter:
        setHover();
        break;
    case QEvent::Leave:
        setNormal();
        break;
    case QEvent::MouseButtonPress:
        if (static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton)
        {
            setPressed();
        }
        break;
    case QEvent::MouseButtonRelease:
    {
        auto *me = static_cast<QMouseEvent*>(e);
        if (me->button() == Qt::LeftButton)
        {
            if (this->rect().contains(me->pos()))
                setHover();
            else
                setNormal();
        }
        break;
    }
    default:
        break;
    }
    return QPushButton::event(e);
}

void PicBtn::setNormal()
{
    setIcon(QIcon(_normal));
}

void PicBtn::setHover()
{
    setIcon(QIcon(_hover));
}

void PicBtn::setPressed()
{
    setIcon(QIcon(_pressed));
}
