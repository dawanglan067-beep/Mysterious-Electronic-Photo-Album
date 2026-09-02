#ifndef PICBTN_H
#define PICBTN_H

#include <QPushButton>

class PicBtn : public QPushButton
{
    Q_OBJECT
public:
    PicBtn(QWidget*parent=nullptr);
    void SetIcons(const QString &normal,const QString &hover,const QString &pressed);
protected:
    bool event(QEvent*e) override;
private:
    void setNormal();
    void setHover();
    void setPressed();
    QString _normal;
    QString _hover;
    QString _pressed;
};

#endif // PICBTN_H
