#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMenu>
#include <QDebug>
#include <QKeyEvent>
#include "wizard.h"
#include"protree.h"
#include<QFileDialog>
#include"protreewidget.h"
#include"picshow.h"
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    //创建菜单
    QMenu*menu_file=menuBar()->addMenu(tr("文件(&F)"));

    //创建项目
    QAction*act_create_pro=new QAction(QIcon(":/icon/createpro.png"),tr("创建项目"),this);
    act_create_pro->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    menu_file->addAction(act_create_pro);

    //打开项目动作
    QAction*act_open_pro=new QAction(QIcon(":/icon/openpro.png"),tr("打开项目"),this);
    act_open_pro->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_O));
    menu_file->addAction(act_open_pro);

    //创建设置菜单
    QMenu*menu_set=menuBar()->addMenu(tr("设置(&S)"));
    //背景音乐控制与偏好设置中心
    QAction*act_music=new QAction(QIcon(":/icon/music.png"),tr("🎵 背景音乐设置中心..."),this);
    act_music->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_M));
    menu_set->addAction(act_music);

    //快速选择音频文件
    QAction*act_select_music=new QAction(QIcon(":/icon/import.png"),tr("📂 快速更换音乐文件..."),this);
    menu_set->addAction(act_select_music);

    //一键播放/停止背景音乐
    QAction*act_toggle_music=new QAction(QIcon(":/icon/play.png"),tr("▶️ 播放背景音乐"),this);
    act_toggle_music->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_B));
    menu_set->addAction(act_toggle_music);

    menu_set->addSeparator();

    //用户自主播放策略勾选项
    QAction*act_opt_immediate=new QAction(tr("选定音乐后立即播放"),this);
    act_opt_immediate->setCheckable(true);
    act_opt_immediate->setChecked(true);
    menu_set->addAction(act_opt_immediate);

    QAction*act_opt_slide_play=new QAction(tr("进入轮播图时自动播放音乐"),this);
    act_opt_slide_play->setCheckable(true);
    act_opt_slide_play->setChecked(true);
    menu_set->addAction(act_opt_slide_play);

    QAction*act_opt_slide_stop=new QAction(tr("退出轮播图时自动停止音乐"),this);
    act_opt_slide_stop->setCheckable(true);
    act_opt_slide_stop->setChecked(true);
    menu_set->addAction(act_opt_slide_stop);

    //创建智能算法菜单（参赛亮点）
    QMenu*menu_algo=menuBar()->addMenu(tr("智能算法(&A)"));
    QAction*act_dhash=new QAction(QIcon(":/icon/setting.png"),tr("感知哈希 (dHash) 相似照片查重"),this);
    act_dhash->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_D));
    menu_algo->addAction(act_dhash);

    //连接信号和槽
    connect(act_create_pro,&QAction::triggered,this,&MainWindow::SlotCreatePro);
    connect(act_open_pro,&QAction::triggered,this,&MainWindow::SlotOpenPro);
    _protree = new ProTree();
    ui->proLayout->addWidget(_protree);
    QTreeWidget*tree_widget=dynamic_cast<ProTree*>(_protree)->GetTreeWidget();
    auto *pro_tree_widget=dynamic_cast<ProTreeWidget*>(tree_widget);
    connect(this,&MainWindow::SigOpenPro,pro_tree_widget,&ProTreeWidget::SlotOpenPro);

    _picshow=new PicShow();
    ui->picLayout->addWidget(_picshow);
    auto *pro_pic_show=dynamic_cast<PicShow*>(_picshow);
    connect(pro_tree_widget,&ProTreeWidget::SigUpdateSelected,pro_pic_show,&PicShow::SlotSelectItem);
    connect(pro_pic_show,&PicShow::SigNextClicked,pro_tree_widget,&ProTreeWidget::SlotNextShow);
    connect(pro_pic_show,&PicShow::SigPreClicked,pro_tree_widget,&ProTreeWidget::SlotPreShow);
    connect(pro_tree_widget,&ProTreeWidget::SigUpdatePic,pro_pic_show,&PicShow::SlotUpdatePic);
    connect(act_music,&QAction::triggered,pro_tree_widget,&ProTreeWidget::SlotSetMusic);
    connect(act_select_music,&QAction::triggered,pro_tree_widget,&ProTreeWidget::SlotSelectMusicFile);
    connect(act_toggle_music,&QAction::triggered,pro_tree_widget,&ProTreeWidget::SlotToggleMusic);
    connect(act_opt_immediate,&QAction::toggled,pro_tree_widget,&ProTreeWidget::SetPlayImmediately);
    connect(act_opt_slide_play,&QAction::toggled,pro_tree_widget,&ProTreeWidget::SetAutoPlayOnSlideshow);
    connect(act_opt_slide_stop,&QAction::toggled,pro_tree_widget,&ProTreeWidget::SetAutoStopOnSlideshowExit);
    connect(pro_tree_widget,&ProTreeWidget::SigMusicStateChanged,this,[this,pro_tree_widget,act_toggle_music](bool playing){
        act_toggle_music->setText(playing ? tr("⏹️ 停止背景音乐") : tr("▶️ 播放背景音乐"));
        act_toggle_music->setIcon(QIcon(playing ? ":/icon/pause.png" : ":/icon/play.png"));
        if (playing) {
            statusBar()->showMessage(tr("🎵 正在播放背景音乐：%1").arg(QFileInfo(pro_tree_widget->GetMusicPath()).fileName()), 5000);
        } else {
            statusBar()->showMessage(tr("🔇 背景音乐已停止"), 3000);
        }
    }, Qt::QueuedConnection);
    connect(pro_tree_widget,&ProTreeWidget::SigMusicErrorOccurred,this,[this](const QString &err){
        statusBar()->showMessage(tr("⚠️ 背景音乐异常: %1").arg(err), 8000);
    }, Qt::QueuedConnection);
    connect(pro_tree_widget,&ProTreeWidget::SigMusicSettingsChanged,this,[pro_tree_widget,act_opt_immediate,act_opt_slide_play,act_opt_slide_stop](){
        act_opt_immediate->blockSignals(true);
        act_opt_slide_play->blockSignals(true);
        act_opt_slide_stop->blockSignals(true);
        act_opt_immediate->setChecked(pro_tree_widget->GetPlayImmediately());
        act_opt_slide_play->setChecked(pro_tree_widget->GetAutoPlayOnSlideshow());
        act_opt_slide_stop->setChecked(pro_tree_widget->GetAutoStopOnSlideshowExit());
        act_opt_immediate->blockSignals(false);
        act_opt_slide_play->blockSignals(false);
        act_opt_slide_stop->blockSignals(false);
    });
    connect(act_dhash,&QAction::triggered,pro_tree_widget,&ProTreeWidget::SlotDetectDuplicates);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    auto *pro_pic_show=dynamic_cast<PicShow*>(_picshow);
    pro_pic_show->ReloadPic();
    QMainWindow::resizeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    auto *pro_tree = dynamic_cast<ProTree*>(_protree);
    auto *pro_tree_widget = pro_tree ? dynamic_cast<ProTreeWidget*>(pro_tree->GetTreeWidget()) : nullptr;
    if (pro_tree_widget)
    {
        if (event->key() == Qt::Key_Left)
        {
            pro_tree_widget->SlotPreShow();
            event->accept();
            return;
        }
        else if (event->key() == Qt::Key_Right)
        {
            pro_tree_widget->SlotNextShow();
            event->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::SlotCreatePro(bool checked)
{
    qDebug()<<"solt creat pro triggered";
    Wizard wizard(this);
    wizard.setWindowTitle(tr("创建项目"));
    auto *page=wizard.page(0);
    page->setTitle(tr("设置项目配置"));
    //连接信号和槽,把项目配置传回来
    connect(&wizard,&Wizard::SigProSettings,dynamic_cast<ProTree*>(_protree),&ProTree::AddProToTree);
    wizard.show();
    wizard.exec();
}

void MainWindow::SlotOpenPro(bool)
{
    QFileDialog file_dialog;
    //设置文件对话框的形式是只能选择文件夹
    file_dialog.setFileMode(QFileDialog::Directory);//Directory:文件夹
    file_dialog.setWindowTitle(tr("选择导入的文件夹"));
    file_dialog.setDirectory(QDir::currentPath());
    file_dialog.setViewMode(QFileDialog::Detail);
    QStringList fileNames;
    if(file_dialog.exec())
    {
        fileNames=file_dialog.selectedFiles();
    }
    if(fileNames.length()<=0)
    {
        return;
    }
    QString import_path=fileNames.at(0);
    emit SigOpenPro(import_path);
}
