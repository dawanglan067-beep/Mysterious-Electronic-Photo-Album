#include "picbtn.h"
#include<QPixmap>
#include<QEvent>
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
        setPressed();
        break;
    case QEvent::MouseButtonRelease:
        setHover();
        break;
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
