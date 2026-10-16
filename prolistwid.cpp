#include "prolistwid.h"
#include "protreeitem.h"
#include "const.h"
#include <QPainter>
#include <QImageReader>
#include "prelistitem.h"
#include <QGuiApplication>
ProListWid::ProListWid(QWidget *parent)
    :_global(0),QListWidget(parent),last_index(17)
{
    this->setViewMode(QListWidget::IconMode);
    this->setIconSize(QSize(PREICON_SIZE,PREICON_SIZE));
    //设置单元格的外边距
    this->setSpacing(5);
    connect(this,&ProListWid::itemPressed,this,&ProListWid::SlotItemPressed);
}

ProListWid::~ProListWid()
{

}

void ProListWid::AddListItem(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize origSize = reader.size();
    if (origSize.isValid())
    {
        reader.setScaledSize(origSize.scaled(PREICON_SIZE, PREICON_SIZE, Qt::KeepAspectRatio));
    }
    QImage img = reader.read();
    QPixmap src_pixmap;
    if (!img.isNull())
    {
        src_pixmap = QPixmap::fromImage(img.scaled(PREICON_SIZE, PREICON_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    else
    {
        src_pixmap.load(path);
        src_pixmap = src_pixmap.scaled(PREICON_SIZE, PREICON_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QPixmap dst_pixmap(QSize(PREICON_SIZE,PREICON_SIZE));
    //填充颜色
    dst_pixmap.fill(QColor(220,220,200,50));
    //绑定画笔到&dst_pixmap图片上
    QPainter painter(&dst_pixmap);
    //获取原图的宽高
    auto src_width=src_pixmap.width();
    auto src_height=src_pixmap.height();
    auto dist_width=dst_pixmap.width();
    auto dist_height=dst_pixmap.height();
    auto x=(dist_width-src_width)/2;
    auto y=(dist_height-src_height)/2;
    painter.drawPixmap(x,y,src_pixmap);
    _global++;
    PreListItem *pItem=new PreListItem(QIcon(dst_pixmap),path,_global,this);
    pItem->setSizeHint(QSize(PREITEM_SIZE,PREITEM_SIZE));
    this->addItem(pItem);
    _set_items[path]=pItem;
    if(_global==1)
    {
        _pos_origin=this->pos();
    }
}

void ProListWid::SlotUpPreList(QTreeWidgetItem *tree_item)
{
    if(!tree_item)
    {
        return;
    }
    auto *pro_item=dynamic_cast<ProTreeItem*>(tree_item);
    //获取路径
    auto path=pro_item->GetPath();
    auto iter=_set_items.find(path);
    if(iter!=_set_items.end())
    {
        return;
    }
    AddListItem(path);


}

void ProListWid::SlotUpSelect(QTreeWidgetItem *tree_item)
{
    if (!tree_item)
    {
        return;
    }
    auto *pro_item = dynamic_cast<ProTreeItem*>(tree_item);
    if (!pro_item)
    {
        return;
    }
    auto path = pro_item->GetPath();
    auto iter = _set_items.find(path);
    if (iter == _set_items.end())
    {
        return;
    }
    auto *list_item = dynamic_cast<PreListItem*>(iter.value());
    if (!list_item)
    {
        return;
    }
    auto index = list_item->GetIndex();
    const int itemStep = PREITEM_SIZE + this->spacing() * 2;
    const int parentW = (this->parentWidget() && this->parentWidget()->width() > 200)
                            ? (this->parentWidget()->width() - 20)
                            : 1700;
    const int maxVisibleCount = std::max(1, parentW / itemStep);

    if (index > maxVisibleCount)
    {
        int shiftCount = index - maxVisibleCount;
        this->move(_pos_origin.x() - shiftCount * itemStep, _pos_origin.y());
        last_index = index;
    }
    else
    {
        this->move(_pos_origin);
        last_index = maxVisibleCount;
    }
    this->setCurrentItem(iter.value());
}

void ProListWid::SlotItemPressed(QListWidgetItem *item)
{
    if (QGuiApplication::mouseButtons() != Qt::LeftButton)
    {
        return;
    }
    auto *list_item = dynamic_cast<PreListItem*>(item);
    if (!list_item)
    {
        return;
    }
    auto path = list_item->GetPath();
    this->setCurrentItem(item);
    emit SigUpSelectShow(path);
}
