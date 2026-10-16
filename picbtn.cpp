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
    _normal = normal;
    _hover = hover;
    _pressed = pressed;
    QPixmap tmpPixmap;
    tmpPixmap.load(normal);
    this->resize(tmpPixmap.size());
    this->setIcon(tmpPixmap);
    this->setIconSize(tmpPixmap.size());
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
    QPixmap tmpPixmap;
    tmpPixmap.load(_normal);
    this->setIcon(tmpPixmap);
    this->setIconSize(tmpPixmap.size());
}

void PicBtn::setHover()
{
    QPixmap tmpPixmap;
    tmpPixmap.load(_hover);
    this->setIcon(tmpPixmap);
    this->setIconSize(tmpPixmap.size());
}

void PicBtn::setPressed()
{
    QPixmap tmpPixmap;
    tmpPixmap.load(_pressed);
    this->setIcon(tmpPixmap);
    this->setIconSize(tmpPixmap.size());
}
