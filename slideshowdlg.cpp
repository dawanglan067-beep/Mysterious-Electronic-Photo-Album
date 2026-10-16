#include "slideshowdlg.h"
#include "ui_slideshowdlg.h"
#include "prolistwid.h"
#include "protreewidget.h"
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QCloseEvent>

SlideShowDlg::SlideShowDlg(QWidget *parent, QTreeWidgetItem *first_item, QTreeWidgetItem *last_item)
    : QDialog(parent), _first_item(first_item), _last_item(last_item)
    , ui(new Ui::SlideShowDlg)
{
    ui->setupUi(this);
    this->setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
    ui->slidepreBtn->SetIcons(":/icon/line/previous.svg",
                              ":/icon/line/previous_hover.svg",
                              ":/icon/line/previous_press.svg");
    ui->slidenextBtn->SetIcons(":/icon/line/next.svg",
                               ":/icon/line/next_hover.svg",
                               ":/icon/line/next_press.svg");
    ui->closeBtn->SetIcons(":/icon/line/close.svg",
                           ":/icon/line/close_hover.svg",
                           ":/icon/line/close_press.svg");
    ui->playBtn->SetIcons(":/icon/line/play.svg",
                          ":/icon/line/play_hover.svg",
                          ":/icon/line/play_press.svg",
                          ":/icon/line/pause.svg",
                          ":/icon/line/pause_hover.svg",
                          ":/icon/line/pause_press.svg");

    auto *_protree_widget = dynamic_cast<ProTreeWidget*>(parent);

    // 在幻灯片顶部构建专业轮播控制条（转场特效选择 + 播放倍速 + 循环开关 + 背景音乐控制）
    QWidget *ctrlBar = new QWidget(this);
    ctrlBar->setObjectName("canvasToolbar");
    QHBoxLayout *ctrlLayout = new QHBoxLayout(ctrlBar);
    ctrlLayout->setContentsMargins(12, 4, 12, 4);
    ctrlLayout->setSpacing(10);

    QLabel *lblEffect = new QLabel(tr("🎬 转场特效:"), ctrlBar);
    lblEffect->setStyleSheet("color: rgb(220, 224, 230); font-size: 12px;");
    QComboBox *comboEffect = new QComboBox(ctrlBar);
    comboEffect->setObjectName("studioCombo");
    comboEffect->addItem(tr("✨ 经典淡入淡出 (Cross Fade)"), PicAnimationWid::EffectFade);
    comboEffect->addItem(tr("➡️ 左右平滑推拉 (Slide Push)"), PicAnimationWid::EffectSlidePush);
    comboEffect->addItem(tr("🪟 百叶窗切换 (Venetian Blinds)"), PicAnimationWid::EffectBlinds);
    comboEffect->addItem(tr("⭕ 圆形光圈扩散 (Iris Circle)"), PicAnimationWid::EffectIrisCircle);
    comboEffect->addItem(tr("🎥 电影感推拉镜头 (Ken Burns)"), PicAnimationWid::EffectKenBurns);

    QLabel *lblSpeed = new QLabel(tr("⚡ 播放倍速:"), ctrlBar);
    lblSpeed->setStyleSheet("color: rgb(220, 224, 230); font-size: 12px;");
    QComboBox *comboSpeed = new QComboBox(ctrlBar);
    comboSpeed->setObjectName("studioCombo");
    comboSpeed->addItem("0.5x 慢速", 0.5f);
    comboSpeed->addItem("1.0x 标准", 1.0f);
    comboSpeed->addItem("1.5x 轻快", 1.5f);
    comboSpeed->addItem("2.0x 极速", 2.0f);
    comboSpeed->setCurrentIndex(1);

    QPushButton *btnLoop = new QPushButton(tr("循环轮播: 开启"), ctrlBar);
    btnLoop->setIcon(QIcon(":/icon/line/loop.svg"));
    btnLoop->setIconSize(QSize(20, 20));
    btnLoop->setObjectName("canvasToolBtn");
    btnLoop->setCheckable(true);
    btnLoop->setChecked(true);
    btnLoop->setCursor(Qt::PointingHandCursor);

    QPushButton *btnMusic = new QPushButton(ctrlBar);
    btnMusic->setIcon(QIcon(":/icon/line/music.svg"));
    btnMusic->setIconSize(QSize(20, 20));
    btnMusic->setObjectName("canvasToolBtn");
    btnMusic->setCheckable(true);
    btnMusic->setCursor(Qt::PointingHandCursor);
    auto syncMusicBtn = [_protree_widget, btnMusic]() {
        bool playing = _protree_widget && _protree_widget->IsMusicPlaying();
        btnMusic->blockSignals(true);
        btnMusic->setChecked(playing);
        if (playing)
        {
            btnMusic->setText(tr("音乐: 播放中"));
            btnMusic->setToolTip(tr("点击停止背景音乐\n曲目: %1").arg(_protree_widget->GetMusicPath()));
        }
        else if (_protree_widget && !_protree_widget->GetLastMusicError().isEmpty())
        {
            btnMusic->setText(tr("音乐: 异常"));
            btnMusic->setToolTip(tr("播放异常: %1\n点击尝试重新播放").arg(_protree_widget->GetLastMusicError()));
        }
        else
        {
            btnMusic->setText(tr("音乐: 已停止"));
            btnMusic->setToolTip(tr("点击开启背景音乐"));
        }
        btnMusic->blockSignals(false);
    };
    syncMusicBtn();

    QLabel *lblHint = new QLabel(tr("快捷键: [Space] 暂停/播放  [←/→] 翻页  [Esc] 退出"), ctrlBar);
    lblHint->setStyleSheet("color: rgb(150, 155, 165); font-size: 11px;");

    ctrlLayout->addWidget(lblEffect);
    ctrlLayout->addWidget(comboEffect);
    ctrlLayout->addSpacing(8);
    ctrlLayout->addWidget(lblSpeed);
    ctrlLayout->addWidget(comboSpeed);
    ctrlLayout->addSpacing(8);
    ctrlLayout->addWidget(btnLoop);
    ctrlLayout->addWidget(btnMusic);
    ctrlLayout->addSpacing(12);
    ctrlLayout->addWidget(lblHint);
    ctrlLayout->addStretch();

    // 避免子控件抢占键盘焦点导致 Space / 左右方向键快捷键失效或误触按钮
    this->setFocusPolicy(Qt::StrongFocus);
    comboEffect->setFocusPolicy(Qt::NoFocus);
    comboSpeed->setFocusPolicy(Qt::NoFocus);
    btnLoop->setFocusPolicy(Qt::NoFocus);
    btnMusic->setFocusPolicy(Qt::NoFocus);
    ui->slidepreBtn->setFocusPolicy(Qt::NoFocus);
    ui->slidenextBtn->setFocusPolicy(Qt::NoFocus);
    ui->closeBtn->setFocusPolicy(Qt::NoFocus);
    ui->playBtn->setFocusPolicy(Qt::NoFocus);
    ui->preListWidget->setFocusPolicy(Qt::NoFocus);

    ui->gridLayout->addWidget(ctrlBar, 0, 1, Qt::AlignCenter);

    connect(comboEffect, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, comboEffect](int) {
        auto eff = static_cast<PicAnimationWid::TransitionEffect>(comboEffect->currentData().toInt());
        ui->picanimation->SetTransitionEffect(eff);
    });
    connect(comboSpeed, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, comboSpeed](int) {
        float mult = comboSpeed->currentData().toFloat();
        ui->picanimation->SetSpeedMultiplier(mult);
    });
    connect(btnLoop, &QPushButton::toggled, this, [this, btnLoop](bool checked) {
        btnLoop->setText(checked ? tr("循环轮播: 开启") : tr("循环轮播: 关闭"));
        ui->picanimation->SetLoopMode(checked);
    });
    if (_protree_widget)
    {
        connect(btnMusic, &QPushButton::clicked, _protree_widget, &ProTreeWidget::SlotToggleMusic);
        connect(_protree_widget, &ProTreeWidget::SigMusicStateChanged, this, [syncMusicBtn](bool) {
            syncMusicBtn();
        }, Qt::QueuedConnection);
        connect(_protree_widget, &ProTreeWidget::SigMusicErrorOccurred, this, [syncMusicBtn](const QString &) {
            syncMusicBtn();
        }, Qt::QueuedConnection);
    }

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
    if (_protree_widget)
    {
        connect(ui->picanimation,&PicAnimationWid::SigStartMusic,_protree_widget,
                &ProTreeWidget::SlotSlideShowStartMusic);
        connect(ui->picanimation,&PicAnimationWid::SigStopMusic,_protree_widget,
                &ProTreeWidget::SlotSlideShowStopMusic);
    }

    // 记录相册首尾节点用于双向循环播放，并装载起始图片与预加载
    ui->picanimation->SetFirstItem(_first_item);
    ui->picanimation->SetLastItem(_last_item);
    ui->picanimation->SetPixmap(_first_item);
    // 按下播放键，启动动画引擎
    ui->picanimation->Start();
}

SlideShowDlg::~SlideShowDlg()
{
    ui->picanimation->Stop();
    delete ui;
}

void SlideShowDlg::closeEvent(QCloseEvent *event)
{
    ui->picanimation->Stop();
    QDialog::closeEvent(event);
}

void SlideShowDlg::reject()
{
    ui->picanimation->Stop();
    QDialog::reject();
}

void SlideShowDlg::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        close();
        return;
    }
    else if (event->key() == Qt::Key_Space)
    {
        ui->picanimation->SlotStartOrStop();
        return;
    }
    else if (event->key() == Qt::Key_Right)
    {
        SlotSlidNext();
        return;
    }
    else if (event->key() == Qt::Key_Left)
    {
        SlotSlidPre();
        return;
    }
    QDialog::keyPressEvent(event);
}

void SlideShowDlg::SlotSlidNext()
{
    ui->picanimation->SlidNext();
}

void SlideShowDlg::SlotSlidPre()
{
    ui->picanimation->SlidPre();
}

