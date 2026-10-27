#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMenu>
#include <QDebug>
#include <QKeyEvent>
#include "wizard.h"
#include"protree.h"
#include "albumfolderpicker.h"
#include <QStandardPaths>
#include <QFileInfo>
#include"protreewidget.h"
#include"picshow.h"
#include "photowallwidget.h"
#include "albumexportdialog.h"
#include "albumpdfexporter.h"
#include "const.h"
#include <QStackedWidget>
#include <QToolBar>
#include <QProgressDialog>
#include <QMessageBox>
#include <QTimer>
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    //创建菜单
    QMenu*menu_file=menuBar()->addMenu(tr("文件(&F)"));

    //创建项目
    QAction*act_create_pro=new QAction(QIcon(":/icon/line/createpro.svg"),tr("创建项目"),this);
    act_create_pro->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    menu_file->addAction(act_create_pro);

    //打开项目动作
    QAction*act_open_pro=new QAction(QIcon(":/icon/line/openpro.svg"),tr("打开项目"),this);
    act_open_pro->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_O));
    menu_file->addAction(act_open_pro);

    //创建设置菜单
    QMenu*menu_set=menuBar()->addMenu(tr("设置(&S)"));
    //背景音乐控制与偏好设置中心
    QAction*act_music=new QAction(QIcon(":/icon/line/music.svg"),tr("背景音乐设置中心..."),this);
    act_music->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_M));
    menu_set->addAction(act_music);

    //快速选择音频文件
    QAction*act_select_music=new QAction(QIcon(":/icon/line/import.svg"),tr("快速更换音乐文件..."),this);
    menu_set->addAction(act_select_music);

    //一键播放/停止背景音乐
    QAction*act_toggle_music=new QAction(QIcon(":/icon/line/play.svg"),tr("播放背景音乐"),this);
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
    QAction*act_dhash=new QAction(QIcon(":/icon/line/duplicates.svg"),tr("感知哈希 (dHash) 相似照片查重"),this);
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
    _photo_pages = new QStackedWidget(this);
    _photo_wall = new PhotoWallWidget(_photo_pages);
    _photo_pages->addWidget(_photo_wall);
    _photo_pages->addWidget(_picshow);
    ui->picLayout->addWidget(_photo_pages);
    auto *viewToolbar = new QToolBar(tr("相册视图"), this);
    viewToolbar->setMovable(false);
    viewToolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addToolBar(viewToolbar);
    auto *wallAction = viewToolbar->addAction(QIcon(":/icon/line/album.svg"), tr("照片墙"));
    auto *detailAction = viewToolbar->addAction(QIcon(":/icon/line/core.svg"), tr("单图浏览"));
    connect(wallAction, &QAction::triggered, this, [this]() { _photo_pages->setCurrentWidget(_photo_wall); });
    connect(detailAction, &QAction::triggered, this, [this]() {
        _photo_pages->setCurrentWidget(_picshow);
        static_cast<PicShow*>(_picshow)->ReloadPic();
    });
    connect(pro_tree_widget, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (item && item->type() == TreeItemPic) {
            _photo_pages->setCurrentWidget(_picshow);
            static_cast<PicShow*>(_picshow)->ReloadPic();
        }
    });
    const auto refreshWall = [this, pro_tree_widget]() {
        _photo_wall->SetPhotos(pro_tree_widget->VisiblePhotos(pro_tree_widget->CurrentAlbumRootPath()));
    };
    connect(pro_tree_widget, &ProTreeWidget::SigPhotoSetChanged, this, refreshWall, Qt::QueuedConnection);
    connect(pro_tree_widget, &ProTreeWidget::SigAlbumOpened, this, [this, refreshWall](const QString &) {
        refreshWall();
        _photo_pages->setCurrentWidget(_photo_wall);
    });
    QTimer::singleShot(0, this, refreshWall);
    connect(_photo_wall, &PhotoWallWidget::SigOpenPhoto, this, [this, pro_tree_widget](const QString &path) {
        if (pro_tree_widget->SelectPhotoByPath(path)) {
            _photo_pages->setCurrentWidget(_picshow);
            static_cast<PicShow*>(_picshow)->ReloadPic();
        }
    });
    connect(_photo_wall, &PhotoWallWidget::SigExportRequested, this, [this]() {
        const QStringList paths = _photo_wall->SelectedPaths();
        if (paths.isEmpty()) return;
        AlbumExportDialog dialog(paths, this);
        if (dialog.exec() != QDialog::Accepted) return;
        const AlbumExportOptions options = dialog.Options();
        AlbumPdfExporter exporter(options);
        QProgressDialog progress(tr("正在生成 PDF 纪念相册…"), tr("取消"), 0, int(options.photos.size()), this);
        progress.setWindowTitle(tr("导出相册"));
        progress.setWindowModality(Qt::ApplicationModal);
        progress.setMinimumDuration(0);
        progress.setAutoClose(false);
        progress.setAutoReset(false);
        bool success = false;
        bool cancelled = false;
        QString message;
        connect(&progress, &QProgressDialog::canceled, &exporter, &AlbumPdfExporter::Cancel);
        connect(&exporter, &AlbumPdfExporter::SigProgress, &progress, [&progress](int completed, int) { progress.setValue(completed); });
        connect(&exporter, &AlbumPdfExporter::SigResult, &progress, [&](bool ok, bool wasCancelled, const QString &result) {
            success = ok;
            cancelled = wasCancelled;
            message = result;
        });
        connect(&exporter, &QThread::finished, &progress, &QDialog::accept);
        exporter.start();
        progress.exec();
        // Reject/close/cancel must finish the worker before its stack lifetime ends.
        if (exporter.isRunning()) exporter.Cancel();
        exporter.wait();
        if (success) QMessageBox::information(this, tr("导出完成"), tr("相册已保存：\n%1").arg(message));
        else if (!cancelled && !progress.wasCanceled() && !message.isEmpty())
            QMessageBox::warning(this, tr("导出失败"), message);
    });
    auto *pro_pic_show=dynamic_cast<PicShow*>(_picshow);
    const auto loadEditor = [pro_tree_widget, pro_pic_show](const QString &path) {
        pro_pic_show->SlotSelectItem(path);
        pro_pic_show->SetMaterialAlbumRoot(pro_tree_widget->AlbumRootForPhoto(path));
    };
    connect(pro_tree_widget, &ProTreeWidget::SigUpdateSelected, pro_pic_show, loadEditor);
    connect(pro_pic_show,&PicShow::SigNextClicked,pro_tree_widget,&ProTreeWidget::SlotNextShow);
    connect(pro_pic_show,&PicShow::SigPreClicked,pro_tree_widget,&ProTreeWidget::SlotPreShow);
    connect(pro_tree_widget, &ProTreeWidget::SigUpdatePic, pro_pic_show, loadEditor);
    connect(pro_pic_show, &PicShow::SigMaterialSaved, this,
            [this, pro_tree_widget](const QString &path, const QString &albumRoot) {
        QString registered, error;
        if (pro_tree_widget->RegisterMaterial(albumRoot, path, &registered, &error))
            QMessageBox::information(this, tr("素材已保存并加入相册"),
                tr("保存位置：%1\n相册素材：%2").arg(path, registered));
        else QMessageBox::warning(this, tr("素材已保存，加入相册失败"),
                tr("文件已保存在：%1\n%2").arg(path, error));
    });
    connect(act_music,&QAction::triggered,pro_tree_widget,&ProTreeWidget::SlotSetMusic);
    connect(act_select_music,&QAction::triggered,pro_tree_widget,&ProTreeWidget::SlotSelectMusicFile);
    connect(act_toggle_music,&QAction::triggered,pro_tree_widget,&ProTreeWidget::SlotToggleMusic);
    connect(act_opt_immediate,&QAction::toggled,pro_tree_widget,&ProTreeWidget::SetPlayImmediately);
    connect(act_opt_slide_play,&QAction::toggled,pro_tree_widget,&ProTreeWidget::SetAutoPlayOnSlideshow);
    connect(act_opt_slide_stop,&QAction::toggled,pro_tree_widget,&ProTreeWidget::SetAutoStopOnSlideshowExit);
    connect(pro_tree_widget,&ProTreeWidget::SigMusicStateChanged,this,[this,pro_tree_widget,act_toggle_music](bool playing){
        act_toggle_music->setText(playing ? tr("停止背景音乐") : tr("播放背景音乐"));
        act_toggle_music->setIcon(QIcon(playing ? ":/icon/line/stop.svg" : ":/icon/line/play.svg"));
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
    if (pro_tree_widget && _photo_pages->currentWidget() == _picshow)
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
    const auto selection = SelectAlbumFolder(
        this, tr("打开相册 — 选中文件夹后确认，或进入目录后选择当前文件夹"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    if (!selection.error.isEmpty())
        QMessageBox::warning(this, tr("打开相册"), selection.error);
    if (!selection.path.isEmpty())
        emit SigOpenPro(selection.path);
}
