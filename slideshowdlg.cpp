#include "slideshowdlg.h"
#include "ui_slideshowdlg.h"
#include"prolistwid.h"
#include"protreewidget.h"
SlideShowDlg::SlideShowDlg(QWidget *parent, QTreeWidgetItem *first_item, QTreeWidgetItem *last_item)
    : QDialog(parent), _first_item(first_item), _last_item(last_item)
    , ui(new Ui::SlideShowDlg)
{
    ui->setupUi(this);
    this->setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
    ui->slidepreBtn->SetIcons(":/icon/previous.png",
                              ":/icon/previous_hover.png",
                              ":/icon/previous_press.png");
    ui->slidenextBtn->SetIcons(":/icon/next.png",
                               ":/icon/next_hover.png",
                               ":/icon/next_press.png");
    ui->closeBtn->SetIcons(":/icon/closeshow.png",
                           ":/icon/closeshow_hover.png",
                           ":/icon/closeshow_press.png");
    ui->playBtn->SetIcons(":/icon/play.png",
                          ":/icon/play_hover.png",
                          ":/icon/play_press.png",
                          ":/icon/pause.png",
                          ":/icon/pause_hover.png",
                          ":/icon/pause_press.png");
    connect(ui->closeBtn,&QPushButton::clicked,this,&SlideShowDlg::close);
    connect(ui->slidenextBtn,&QPushButton::clicked,this,&SlideShowDlg::SlotSlidNext);
    connect(ui->slidepreBtn,&QPushButton::clicked,this,&SlideShowDlg::SlotSlidPre);
    auto *prolistWid=dynamic_cast<ProListWid*>(ui->preListWidget);
    connect(ui->picanimation,&PicAnimationWid::SigUpPreList,prolistWid,
            &ProListWid::SlotUpPreList);
    connect(ui->picanimation,&PicAnimationWid::SigSelectItem,prolistWid,
            &ProListWid::SlotUpSelect);
    connect(prolistWid,&ProListWid::SigUpSelectShow,ui->picanimation,
            &PicAnimationWid::SlotUpSelectShow);
    connect(ui->playBtn,&PicStateBtn::clicked,ui->picanimation,
            &PicAnimationWid::SlotStartOrStop);
    connect(ui->picanimation,&PicAnimationWid::SigStart,ui->playBtn,
            &PicStateBtn::SlotStart);
    connect(ui->picanimation,&PicAnimationWid::SigStop,ui->playBtn,
            &PicStateBtn::SlotStop);
    auto *_protree_widget=dynamic_cast<ProTreeWidget*>(parent);
    connect(ui->picanimation,&PicAnimationWid::SigStartMusic,_protree_widget,
            &ProTreeWidget::SlotStartMusic);
    connect(ui->picanimation,&PicAnimationWid::SigStopMusic,_protree_widget,
            &ProTreeWidget::SlotStopMusic);

    //装载起始图片与预加载
    ui->picanimation->SetPixmap(_first_item);
    //按下播放键，启动动画引擎
    ui->picanimation->Start();
}

SlideShowDlg::~SlideShowDlg()
{
    delete ui;
}

void SlideShowDlg::SlotSlidNext()
{
    ui->picanimation->SlidNext();
}

void SlideShowDlg::SlotSlidPre()
{
    ui->picanimation->SlidPre();
}
