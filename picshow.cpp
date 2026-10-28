#include "photoadjustmentpanel.h"
#include "picshow.h"
#include "ui_picshow.h"
#include "cropdialog.h"
#include "imageexportservice.h"
#include "materialsavedialog.h"
#include <QInputDialog>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QUndoCommand>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QtMath>
#include <algorithm>

// 历史只保存编辑参数，不复制原图或预览像素。
class PhotoEditCommand : public QUndoCommand
{
public:
    PhotoEditCommand(PicShow *viewer, const PhotoEditDocument::EditState &before,
                     const PhotoEditDocument::EditState &after, const QString &text)
        : QUndoCommand(text), _viewer(viewer), _before(before), _after(after) {}
    void undo() override { _viewer->ApplyEditState(_before); }
    void redo() override { _viewer->ApplyEditState(_after); }
private:
    PicShow *_viewer;
    PhotoEditDocument::EditState _before, _after;
};

PicShow::PicShow(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PicShow)
    , _b_btnvisible(false)
    , _toolbar_wid(nullptr)
    , _btn_zoom_reset(nullptr)
    , _info_panel(nullptr)
    , _histogram_wid(nullptr)
{
    ui->setupUi(this);
    _undo_stack = new QUndoStack(this);
    _undo_stack->setObjectName("photoUndoStack");

    connect(ui->label, &PhotoCanvas::SigFocusRequested, this, [this]() { setFocus(); });
    // 初始化上一张/下一张按钮的图标（常规状态、悬停状态、按下状态）
    ui->previousBtn->SetIcons(":/icon/line/previous.svg",
                              ":/icon/line/previous_hover.svg",
                              ":/icon/line/previous_press.svg");
    ui->nextBtn->SetIcons(":/icon/line/next.svg",
                          ":/icon/line/next_hover.svg",
                          ":/icon/line/next_press.svg");

    // 为上一张按钮设置图形透明度特效，初始透明度为 0（隐藏）
    QGraphicsOpacityEffect *opacity_pre = new QGraphicsOpacityEffect(this);
    opacity_pre->setOpacity(0);
    ui->previousBtn->setGraphicsEffect(opacity_pre);

    // 为下一张按钮设置图形透明度特效，初始透明度为 0（隐藏）
    QGraphicsOpacityEffect *opacity_next = new QGraphicsOpacityEffect(this);
    opacity_next->setOpacity(0);
    ui->nextBtn->setGraphicsEffect(opacity_next);

    // 创建上一张按钮的透明度渐变动画，时长 500ms
    _animation_show_pre = new QPropertyAnimation(opacity_pre, "opacity", this);
    _animation_show_pre->setEasingCurve(QEasingCurve::Linear);
    _animation_show_pre->setDuration(500);

    // 创建下一张按钮的透明度渐变动画，时长 500ms
    _animation_show_next = new QPropertyAnimation(opacity_next, "opacity", this);
    _animation_show_next->setEasingCurve(QEasingCurve::Linear);
    _animation_show_next->setDuration(500);

    // 绑定按钮点击事件到对应信号，供外部 ProTreeWidget 捕获并切换图片
    connect(ui->nextBtn, &QPushButton::clicked, this, &PicShow::SigNextClicked);
    connect(ui->previousBtn, &QPushButton::clicked, this, &PicShow::SigPreClicked);

    // 构建底部专业画布工具栏与右侧直方图/元数据/算法工坊侧边栏
    InitStudioUI();
    connect(ui->label, &PhotoCanvas::SigZoomTextChanged, _btn_zoom_reset, &QPushButton::setText);
}

PicShow::~PicShow()
{
    delete ui;
}

void PicShow::InitStudioUI()
{
    // 1. 将原有的 ui->label 包装进中央垂直布局（上方画布 + 下方专业悬浮工具栏）
    int labelIdx = ui->horizontalLayout->indexOf(ui->label);
    ui->horizontalLayout->removeWidget(ui->label);

    QWidget *centerContainer = new QWidget(this);
    QVBoxLayout *centerLayout = new QVBoxLayout(centerContainer);
    centerLayout->setContentsMargins(0, 0, 0, 4);
    centerLayout->setSpacing(6);
    centerLayout->addWidget(ui->label, 1);

    // 创建底部操控工具栏
    _toolbar_wid = new QWidget(centerContainer);
    _toolbar_wid->setObjectName("canvasToolbar");
    QHBoxLayout *tbLayout = new QHBoxLayout(_toolbar_wid);
    tbLayout->setContentsMargins(12, 5, 12, 5);
    tbLayout->setSpacing(6);
    tbLayout->addStretch();

    auto makeToolBtn = [this](const QString &text, const QString &tip) -> QPushButton* {
        QPushButton *btn = new QPushButton(text, _toolbar_wid);
        btn->setObjectName("canvasToolBtn");
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setMinimumHeight(28);
        return btn;
    };

    QPushButton *btnZoomOut = makeToolBtn("缩小", tr("缩小画布 (鼠标滚轮向下)"));
    btnZoomOut->setIcon(QIcon(":/icon/line/zoomout.svg"));
    btnZoomOut->setIconSize(QSize(20, 20));
    _btn_zoom_reset = makeToolBtn("100% 自适应", tr("点击重置为自适应窗口大小 (双击画布可切换)"));
    _btn_zoom_reset->setIcon(QIcon(":/icon/line/fit.svg"));
    _btn_zoom_reset->setIconSize(QSize(20, 20));
    QPushButton *btnZoomIn = makeToolBtn("放大", tr("放大画布 (鼠标滚轮向上)"));
    btnZoomIn->setIcon(QIcon(":/icon/line/zoomin.svg"));
    btnZoomIn->setIconSize(QSize(20, 20));
    QPushButton *btnActual = makeToolBtn("1:1 原图", tr("按原始像素 1:1 比例显示"));
    btnActual->setIcon(QIcon(":/icon/line/fit.svg"));
    btnActual->setIconSize(QSize(20, 20));
    QPushButton *btnRotL = makeToolBtn("左旋", tr("逆时针旋转 90°"));
    btnRotL->setIcon(QIcon(":/icon/line/rotateleft.svg"));
    btnRotL->setIconSize(QSize(20, 20));
    QPushButton *btnRotR = makeToolBtn("右旋", tr("顺时针旋转 90°"));
    btnRotR->setIcon(QIcon(":/icon/line/rotateright.svg"));
    btnRotR->setIconSize(QSize(20, 20));
    QPushButton *btnFlipH = makeToolBtn("水平镜像", tr("水平左右镜像翻转"));
    btnFlipH->setIcon(QIcon(":/icon/line/fliph.svg"));
    btnFlipH->setIconSize(QSize(20, 20));
    QPushButton *btnFlipV = makeToolBtn("垂直镜像", tr("垂直上下镜像翻转"));
    btnFlipV->setIcon(QIcon(":/icon/line/flipv.svg"));
    btnFlipV->setIconSize(QSize(20, 20));
    QPushButton *btnInfo = makeToolBtn("直方图/算法工坊", tr("展开/收起右侧 RGB 直方图、图像元数据与算法滤镜工坊"));
    btnInfo->setIcon(QIcon(":/icon/line/histogram.svg"));
    btnInfo->setIconSize(QSize(20, 20));
    btnInfo->setCheckable(true);
    btnInfo->setChecked(true);

    tbLayout->addWidget(btnZoomOut);
    tbLayout->addWidget(_btn_zoom_reset);
    tbLayout->addWidget(btnZoomIn);
    tbLayout->addWidget(btnActual);
    tbLayout->addSpacing(8);
    tbLayout->addWidget(btnRotL);
    tbLayout->addWidget(btnRotR);
    tbLayout->addWidget(btnFlipH);
    tbLayout->addWidget(btnFlipV);
    tbLayout->addSpacing(8);
    tbLayout->addWidget(btnInfo);
    tbLayout->addStretch();

    centerLayout->addWidget(_toolbar_wid, 0);
    ui->horizontalLayout->insertWidget(labelIdx, centerContainer, 1);

    connect(btnZoomIn, &QPushButton::clicked, this, &PicShow::SlotZoomIn);
    connect(btnZoomOut, &QPushButton::clicked, this, &PicShow::SlotZoomOut);
    connect(_btn_zoom_reset, &QPushButton::clicked, this, &PicShow::SlotZoomFit);
    connect(btnActual, &QPushButton::clicked, this, &PicShow::SlotZoomActual);
    connect(btnRotL, &QPushButton::clicked, this, &PicShow::SlotRotateLeft);
    connect(btnRotR, &QPushButton::clicked, this, &PicShow::SlotRotateRight);
    connect(btnFlipH, &QPushButton::clicked, this, &PicShow::SlotFlipH);
    connect(btnFlipV, &QPushButton::clicked, this, &PicShow::SlotFlipV);
    connect(btnInfo, &QPushButton::clicked, this, &PicShow::SlotToggleInfoPanel);

    // 2. 创建右侧可折叠的“RGB 色彩直方图 + 图像元数据 + 数字图像算法工坊”面板
    _info_panel = new QWidget(this);
    _info_panel->setObjectName("inspectorPanel");
    _info_panel->setFixedWidth(268);

    QVBoxLayout *infoLayout = new QVBoxLayout(_info_panel);
    infoLayout->setContentsMargins(12, 8, 12, 8);
    infoLayout->setSpacing(6);

    QLabel *titleHist = new QLabel(tr("📈 实时 RGB 色彩直方图"), _info_panel);
    titleHist->setObjectName("inspectorSectionTitle");
    infoLayout->addWidget(titleHist);

    _histogram_wid = new HistogramWidget(_info_panel);
    infoLayout->addWidget(_histogram_wid);

    QFrame *sep1 = new QFrame(_info_panel);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setStyleSheet("color: rgb(75, 78, 82);");
    infoLayout->addWidget(sep1);

    _adjustments = new PhotoAdjustmentPanel(_info_panel);
    infoLayout->addWidget(_adjustments);
    connect(_adjustments, &PhotoAdjustmentPanel::SigParametersChanged, this, &PicShow::SlotAdjustParamsChanged);
    connect(_adjustments, &PhotoAdjustmentPanel::SigGestureStarted, this, [this]() {
        FinishParameterDrag();
        _drag_start_state = _document.state();
        _parameter_dragging = true;
    });
    connect(_adjustments, &PhotoAdjustmentPanel::SigGestureFinished, this, &PicShow::FinishParameterDrag);

    QHBoxLayout *algoBtnRow = new QHBoxLayout();
    algoBtnRow->setSpacing(6);
    QPushButton *btnResetAlgo = new QPushButton(tr("还原参数"), _info_panel);
    btnResetAlgo->setIcon(QIcon(":/icon/line/reset.svg"));
    btnResetAlgo->setIconSize(QSize(20, 20));
    btnResetAlgo->setObjectName("canvasToolBtn");
    btnResetAlgo->setCursor(Qt::PointingHandCursor);
    btnResetAlgo->setMinimumHeight(26);
    QPushButton *btnSaveImg = new QPushButton(tr("导出图像"), _info_panel);
    btnSaveImg->setIcon(QIcon(":/icon/line/save.svg"));
    btnSaveImg->setIconSize(QSize(20, 20));
    btnSaveImg->setObjectName("canvasToolBtn");
    btnSaveImg->setCursor(Qt::PointingHandCursor);
    btnSaveImg->setMinimumHeight(26);
    algoBtnRow->addWidget(btnResetAlgo);
    algoBtnRow->addWidget(btnSaveImg);
    infoLayout->addLayout(algoBtnRow);
    _compare_button = new QPushButton(tr("按住查看原图"), _info_panel);
    _compare_button->setObjectName("photoCompareButton");
    _compare_button->setToolTip(tr("按住显示完整原图（自适应窗口），释放恢复编辑结果及原有视图"));
    _compare_button->setEnabled(false);
    infoLayout->addWidget(_compare_button);
    connect(_compare_button, &QPushButton::pressed, this, [this]() { SetOriginalComparison(true); });
    connect(_compare_button, &QPushButton::released, this, [this]() { SetOriginalComparison(false); });
    _restore_original_button = new QPushButton(tr("恢复全部原图"), _info_panel);
    _restore_original_button->setObjectName("photoRestoreOriginalButton");
    _restore_original_button->setToolTip(tr("清除裁剪、旋转、镜像、调色和滤镜；可以撤销"));
    _restore_original_button->setEnabled(false);
    infoLayout->addWidget(_restore_original_button);
    connect(_restore_original_button, &QPushButton::clicked, this, &PicShow::SlotRestoreOriginal);
    auto *cropButton = new QPushButton(tr("裁剪…"), _info_panel);
    cropButton->setObjectName("photoCropButton");
    infoLayout->addWidget(cropButton);
    connect(cropButton, &QPushButton::clicked, this, &PicShow::SlotCrop);
    _save_material_button = new QPushButton(tr("另存为素材…"), _info_panel);
    _save_material_button->setObjectName("photoSaveMaterialButton");
    _save_material_button->setToolTip(tr("将当前裁剪结果另存为素材，保留原照片与编辑历史"));
    _save_material_button->setEnabled(false);
    infoLayout->addWidget(_save_material_button);
    connect(_save_material_button, &QPushButton::clicked, this, &PicShow::SlotSaveMaterial);
    QHBoxLayout *historyRow = new QHBoxLayout();
    auto *undoButton = new QPushButton(tr("撤销"), _info_panel);
    auto *redoButton = new QPushButton(tr("重做"), _info_panel);
    undoButton->setObjectName("photoUndoButton");
    redoButton->setObjectName("photoRedoButton");
    undoButton->setEnabled(false);
    redoButton->setEnabled(false);
    historyRow->addWidget(undoButton);
    historyRow->addWidget(redoButton);
    infoLayout->addLayout(historyRow);
    connect(undoButton, &QPushButton::clicked, this, &PicShow::SlotUndo);
    connect(redoButton, &QPushButton::clicked, this, &PicShow::SlotRedo);
    connect(_undo_stack, &QUndoStack::canUndoChanged, undoButton, &QPushButton::setEnabled);
    connect(_undo_stack, &QUndoStack::canRedoChanged, redoButton, &QPushButton::setEnabled);
    auto *undoAction = new QAction(tr("撤销"), this);
    undoAction->setShortcut(QKeySequence::Undo);
    undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(undoAction);
    connect(undoAction, &QAction::triggered, this, &PicShow::SlotUndo);
    auto *redoAction = new QAction(tr("重做"), this);
    redoAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y),
                              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(redoAction);
    connect(redoAction, &QAction::triggered, this, &PicShow::SlotRedo);


    connect(btnResetAlgo, &QPushButton::clicked, this, &PicShow::SlotResetAlgorithms);
    connect(btnSaveImg, &QPushButton::clicked, this, &PicShow::SlotSaveProcessedImage);

    QFrame *sep2 = new QFrame(_info_panel);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet("color: rgb(75, 78, 82);");
    infoLayout->addWidget(sep2);

    // ===== 图像元数据区块 =====
    QLabel *titleMeta = new QLabel(tr("📋 图像属性与元数据"), _info_panel);
    titleMeta->setObjectName("inspectorSectionTitle");
    infoLayout->addWidget(titleMeta);

    QGridLayout *metaGrid = new QGridLayout();
    metaGrid->setHorizontalSpacing(8);
    metaGrid->setVerticalSpacing(4);

    auto addMetaRow = [&](int row, const QString &labelText, QLabel* &valueLabel) {
        QLabel *keyLbl = new QLabel(labelText, _info_panel);
        keyLbl->setStyleSheet("color: rgb(155, 160, 168); font-size: 11px;");
        valueLabel = new QLabel("-", _info_panel);
        valueLabel->setStyleSheet("color: rgb(230, 233, 238); font-size: 11px;");
        valueLabel->setWordWrap(true);
        valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        metaGrid->addWidget(keyLbl, row, 0, Qt::AlignTop);
        metaGrid->addWidget(valueLabel, row, 1, Qt::AlignTop);
    };

    addMetaRow(0, tr("文件名称:"), _lbl_meta_name);
    addMetaRow(1, tr("像素尺寸:"), _lbl_meta_dim);
    addMetaRow(2, tr("文件大小:"), _lbl_meta_size);
    addMetaRow(3, tr("色彩通道:"), _lbl_meta_depth);
    addMetaRow(4, tr("RGB 均值:"), _lbl_meta_rgb);
    addMetaRow(5, tr("修改时间:"), _lbl_meta_time);
    addMetaRow(6, tr("本地路径:"), _lbl_meta_path);
    metaGrid->setColumnStretch(1, 1);
    infoLayout->addLayout(metaGrid);

    QPushButton *btnLocate = new QPushButton(tr("在资源管理器中定位"), _info_panel);
    btnLocate->setIcon(QIcon(":/icon/line/openpro.svg"));
    btnLocate->setIconSize(QSize(20, 20));
    btnLocate->setObjectName("canvasToolBtn");
    btnLocate->setCursor(Qt::PointingHandCursor);
    btnLocate->setMinimumHeight(28);
    connect(btnLocate, &QPushButton::clicked, this, &PicShow::SlotOpenInExplorer);
    infoLayout->addWidget(btnLocate);
    infoLayout->addStretch();

    ui->horizontalLayout->addWidget(_info_panel);
}

void PicShow::ResetViewFit()
{
    ui->label->ResetViewFit();
}

void PicShow::RebuildTransformedPixmap()
{
    if (_document.sourceImage().isNull())
    {
        ui->label->SetFrame({}, {}, {}, false, 0);
        _histogram_wid->ClearHistogram();
        UpdateImageMetadata();
        ReloadPic();
        return;
    }

    const bool dragging = _adjustments->IsSliderDown();
    const QImage transformed = _document.Render(dragging ? 1600 : 0);

    ui->label->SetFrame(_document.sourceImage(), transformed, _document.outputSize(),
                        _document.isModified(), _document.state().rotation);
    _histogram_wid->UpdateHistogram(transformed);
    UpdateImageMetadata();
    ReloadPic();
}

void PicShow::UpdateImageMetadata()
{
    if (_document.sourcepath().isEmpty() || _document.sourceImage().isNull())
    {
        _lbl_meta_name->setText("-");
        _lbl_meta_dim->setText("-");
        _lbl_meta_size->setText("-");
        _lbl_meta_depth->setText("-");
        _lbl_meta_rgb->setText("-");
        _lbl_meta_time->setText("-");
        _lbl_meta_path->setText("-");
        return;
    }

    QFileInfo fi(_document.sourcepath());
    _lbl_meta_name->setText(fi.fileName());

    const int w = _document.outputSize().width();
    const int h = _document.outputSize().height();
    const double mp = (w * h) / 1000000.0;
    _lbl_meta_dim->setText(QString("%1 × %2 px (%3 MP)")
                               .arg(w)
                               .arg(h)
                               .arg(mp, 0, 'f', 2));

    qint64 bytes = fi.size();
    QString sizeStr;
    if (bytes >= 1024 * 1024)
        sizeStr = QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    else
        sizeStr = QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    _lbl_meta_size->setText(QString("%1 (%2)").arg(sizeStr, fi.suffix().toUpper()));

    _lbl_meta_depth->setText(QString("%1-bit (%2)")
                                 .arg(_document.sourceImage().depth())
                                 .arg(_document.sourceImage().hasAlphaChannel() ? "RGBA" : "RGB"));

    // 计算当前处理后图像的 RGB 通道平均亮度
    QImage sampleSrc = ui->label->PreviewImage();
    if (sampleSrc.isNull()) sampleSrc = _document.sourceImage();
    QImage small = sampleSrc.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                       .convertToFormat(QImage::Format_RGB32);
    qint64 sumR = 0, sumG = 0, sumB = 0;
    int count = small.width() * small.height();
    for (int y = 0; y < small.height(); ++y)
    {
        const QRgb *line = reinterpret_cast<const QRgb*>(small.constScanLine(y));
        for (int x = 0; x < small.width(); ++x)
        {
            sumR += qRed(line[x]);
            sumG += qGreen(line[x]);
            sumB += qBlue(line[x]);
        }
    }
    if (count > 0)
    {
        _lbl_meta_rgb->setText(QString("R:%1  G:%2  B:%3")
                                   .arg(sumR / count)
                                   .arg(sumG / count)
                                   .arg(sumB / count));
    }

    _lbl_meta_time->setText(fi.lastModified().toString("yyyy-MM-dd HH:mm"));
    _lbl_meta_path->setText(QDir::toNativeSeparators(fi.absoluteFilePath()));
}

// 刷新图片显示区域（请求 label 重新触发 Paint 事件进行绘制）
void PicShow::ReloadPic()
{
    if (ui && ui->label)
    {
        ui->label->update();
    }
}

// 清空当前展示的图片数据与路径，并刷新重绘为空白
void PicShow::Clear()
{
    _material_album_root.clear();
    SetOriginalComparison(false);
    _parameter_dragging = false;
    _undo_stack->clear();
    _document.clear();
    ui->label->SetFrame({}, {}, {}, false, 0);
    _document.setParameters({});
    SyncControlsFromDocument();
    ResetViewFit();
    if (_histogram_wid)
    {
        _histogram_wid->ClearHistogram();
    }
    UpdateImageMetadata();
    if (ui && ui->label)
    {
        ui->label->update();
    }
}

// 事件处理：捕获鼠标进入/离开 PicShow 区域事件，触发按钮淡入/淡出
bool PicShow::event(QEvent *event)
{
    switch(event->type())
    {
    case QEvent::Hide:
    case QEvent::WindowDeactivate:
        SetOriginalComparison(false);
        break;
    case QEvent::Enter:
        // 鼠标进入展示区，淡入显示上一张/下一张按钮
        ShowPreNextBtns(true);
        break;
    case QEvent::Leave:
        // 鼠标离开展示区，淡出隐藏上一张/下一张按钮
        ShowPreNextBtns(false);
        break;
    default:
        break;
    }
    return QDialog::event(event);
}

void PicShow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Left)
    {
        emit SigPreClicked();
        event->accept();
        return;
    }
    else if (event->key() == Qt::Key_Right)
    {
        emit SigNextClicked();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void PicShow::SlotZoomIn()
{
    ui->label->ZoomIn();
}

void PicShow::SlotZoomOut()
{
    ui->label->ZoomOut();
}

void PicShow::SlotZoomFit()
{
    ui->label->ZoomFit();
}

void PicShow::SlotZoomActual()
{
    ui->label->ZoomActual();
}

void PicShow::SlotRotateLeft()
{
    if (_document.sourceImage().isNull()) return;
    FinishParameterDrag();
    const auto before = _document.state();
    auto after = before;
    after.rotation = (after.rotation + 270) % 360;
    PushEdit(before, after, tr("左旋"));
}

void PicShow::SlotRotateRight()
{
    if (_document.sourceImage().isNull()) return;
    FinishParameterDrag();
    const auto before = _document.state();
    auto after = before;
    after.rotation = (after.rotation + 90) % 360;
    PushEdit(before, after, tr("右旋"));
}

void PicShow::SlotFlipH()
{
    if (_document.sourceImage().isNull()) return;
    FinishParameterDrag();
    const auto before = _document.state();
    auto after = before;
    after.flipHorizontal = !after.flipHorizontal;
    PushEdit(before, after, tr("水平镜像"));
}

void PicShow::SlotFlipV()
{
    if (_document.sourceImage().isNull()) return;
    FinishParameterDrag();
    const auto before = _document.state();
    auto after = before;
    after.flipVertical = !after.flipVertical;
    PushEdit(before, after, tr("垂直镜像"));
}

void PicShow::SlotToggleInfoPanel()
{
    if (_info_panel)
    {
        _info_panel->setVisible(!_info_panel->isVisible());
        ReloadPic();
    }
}

void PicShow::SlotOpenInExplorer()
{
    if (_document.sourcepath().isEmpty()) return;
    QFileInfo fi(_document.sourcepath());
    if (!fi.exists()) return;
#ifdef Q_OS_WIN
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(fi.absoluteFilePath())});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
#endif
}

void PicShow::SlotAdjustParamsChanged()
{
    if (_document.sourceImage().isNull()) { SyncControlsFromDocument(); return; }
    const auto before = _document.state();
    const auto parameters = _adjustments->Parameters();
    if (!_parameter_dragging) {
        auto after = before;
        after.parameters = parameters;
        PushEdit(before, after, tr("调整颜色/滤镜"));
        return;
    }
    _document.setParameters(parameters);
    SyncControlsFromDocument();
    if (!_document.sourceImage().isNull())
    {
        RebuildTransformedPixmap();
    }
}

void PicShow::SlotResetAlgorithms()
{
    if (_document.sourceImage().isNull()) return;
    FinishParameterDrag();
    const auto before = _document.state();
    auto after = before;
    after.parameters = {};
    PushEdit(before, after, tr("还原参数"));
}

void PicShow::SlotRestoreOriginal()
{
    if (_document.sourceImage().isNull()) return;
    SetOriginalComparison(false);
    FinishParameterDrag();
    // 恢复本身是一条历史记录，保留恢复前的完整编辑状态。
    PushEdit(_document.state(), PhotoEditDocument::EditState{}, tr("恢复全部原图"));
}

void PicShow::SlotSaveProcessedImage()
{
    ExportProcessedImage();
}

bool PicShow::ExportProcessedImage(const QStringList &protectedPaths)
{
    if (_document.sourceImage().isNull())
    {
        return false;
    }
    SetOriginalComparison(false);
    FinishParameterDrag();
    const QFileInfo fi(_document.sourcepath());
    const auto extensions = ImageExportService::supportedExtensions();
    QStringList filters;
    if (extensions.contains("png")) filters << tr("PNG 图像 (*.png)");
    if (extensions.contains("jpg")) filters << tr("JPEG 图像 (*.jpg *.jpeg)");
    if (extensions.contains("bmp")) filters << tr("BMP 图像 (*.bmp)");
    if (filters.isEmpty()) {
        QMessageBox::warning(this, tr("导出失败"), tr("当前环境没有可用的图片编码器。"));
        return false;
    }
    const QString defaultExtension = extensions.first();
    const QString defaultPath = fi.dir().absoluteFilePath(fi.completeBaseName() + "_edited." + defaultExtension);
    QString selectedFilter = filters.first();
    QString savePath = QFileDialog::getSaveFileName(
        this,
        tr("导出处理后的图像"),
        defaultPath,
        filters.join(";;"), &selectedFilter);
    if (savePath.isEmpty())
    {
        return false;
    }
    if (QFileInfo(savePath).suffix().isEmpty()) {
        const QString extension = selectedFilter == tr("JPEG 图像 (*.jpg *.jpeg)") ? "jpg"
            : (selectedFilter == tr("BMP 图像 (*.bmp)") ? "bmp" : "png");
        savePath += "." + extension;
        if (QFileInfo::exists(savePath)
            && QMessageBox::question(this, tr("覆盖输出文件"), tr("文件已存在，是否替换？\n%1").arg(savePath)) != QMessageBox::Yes)
            return false;
    }
    ImageExportService::Options options;
    const auto suffix = QFileInfo(savePath).suffix().toLower();
    if (suffix == "jpg" || suffix == "jpeg") {
        bool accepted = false;
        options.jpegQuality = QInputDialog::getInt(this, tr("JPEG 导出"),
            tr("质量（0–100，透明区域使用白色背景）："), 95, 0, 100, 1, &accepted);
        if (!accepted) return false;
    }
    // 删除前导出的副本不能落在同一批即将删除的文件上。
    for (const auto &protectedPath : protectedPaths) {
        const QFileInfo output(savePath), protectedFile(protectedPath);
#ifdef Q_OS_WIN
        const auto sensitivity = Qt::CaseInsensitive;
#else
        const auto sensitivity = Qt::CaseSensitive;
#endif
        if (QDir::cleanPath(output.absoluteFilePath()).compare(
                QDir::cleanPath(protectedFile.absoluteFilePath()), sensitivity) == 0
            || (!output.canonicalFilePath().isEmpty() && output.canonicalFilePath().compare(
                    protectedFile.canonicalFilePath(), sensitivity) == 0)) {
            QMessageBox::warning(this, tr("导出未完成"), tr("副本不能保存到本次准备删除的文件，请选择其他位置。"));
            return false;
        }
        if (protectedFile.isDir()) {
            const auto inside = [sensitivity](const QString &file, const QString &directory) {
                if (file.isEmpty() || directory.isEmpty()) return false;
                return file.startsWith(directory.endsWith('/') ? directory : directory + '/', sensitivity);
            };
            // 即使输出文件还不存在，也验证其父目录的实际路径，防止经目录别名写回待删除文件夹。
            const QString canonicalOutput = QDir(QFileInfo(output.absolutePath()).canonicalFilePath())
                .filePath(output.fileName());
            if (inside(QDir::cleanPath(output.absoluteFilePath()), QDir::cleanPath(protectedFile.absoluteFilePath()))
                || inside(canonicalOutput, protectedFile.canonicalFilePath())) {
                QMessageBox::warning(this, tr("导出未完成"), tr("副本不能保存在本次准备删除的文件夹内，请选择其他位置。"));
                return false;
            }
        }
    }
    const auto result = ImageExportService::exportDocument(_document, savePath, options);
    if (result.success)
    {
        QMessageBox::information(this, tr("导出成功"), tr("处理后的图像已成功保存至：\n%1").arg(savePath));
    }
    else
    {
        QMessageBox::warning(this, tr("导出失败"), result.error);
    }
    return result.success;
}

bool PicShow::IsEditingPath(const QString &path) const
{
    if (path.isEmpty() || _document.sourcepath().isEmpty()) return false;
    const auto normalized = [](const QString &file) {
        const QFileInfo info(file);
        return info.canonicalFilePath().isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : info.canonicalFilePath();
    };
#ifdef Q_OS_WIN
    return normalized(path).compare(normalized(_document.sourcepath()), Qt::CaseInsensitive) == 0;
#else
    return normalized(path) == normalized(_document.sourcepath());
#endif
}

bool PicShow::ConfirmDeletionOfEditedPhoto(const QStringList &deletionPaths)
{
    if (!_document.isModified()) return true;
    QMessageBox prompt(QMessageBox::Warning, tr("当前图片有编辑内容"),
        tr("当前图片有修改。删除后将无法继续使用该源文件；可以先导出副本。\n%1").arg(_document.sourcepath()),
        QMessageBox::NoButton, this);
    prompt.setObjectName("photoDeleteEditPrompt");
    auto *exportButton = prompt.addButton(tr("导出副本后删除"), QMessageBox::AcceptRole);
    exportButton->setObjectName("deleteExportButton");
    auto *discardButton = prompt.addButton(tr("不导出，继续删除"), QMessageBox::DestructiveRole);
    discardButton->setObjectName("deleteDiscardButton");
    auto *cancel = prompt.addButton(QMessageBox::Cancel);
    cancel->setText(tr("取消"));
    prompt.setDefaultButton(cancel);
    prompt.exec();
    if (prompt.clickedButton() == exportButton) return ExportProcessedImage(deletionPaths);
    // 不在这里清空文档或历史，回收站操作失败仍要保留当前编辑。
    return prompt.clickedButton() == discardButton;
}

// 控制上一张/下一张按钮的渐变动画逻辑
void PicShow::ShowPreNextBtns(bool b_show)
{
    // 需要隐藏且当前为显示状态：启动淡出动画（1 -> 0）
    if(!b_show && _b_btnvisible)
    {
        _animation_show_pre->stop();
        _animation_show_pre->setStartValue(1);
        _animation_show_pre->setEndValue(0);
        _animation_show_pre->start();

        _animation_show_next->stop();
        _animation_show_next->setStartValue(1);
        _animation_show_next->setEndValue(0);
        _animation_show_next->start();

        _b_btnvisible = false;
        return;
    }
    // 需要显示且当前为隐藏状态：启动淡入动画（0 -> 1）
    if(b_show && !_b_btnvisible)
    {
        _animation_show_pre->stop();
        _animation_show_pre->setStartValue(0);
        _animation_show_pre->setEndValue(1);
        _animation_show_pre->start();

        _animation_show_next->stop();
        _animation_show_next->setStartValue(0);
        _animation_show_next->setEndValue(1);
        _animation_show_next->start();

        _b_btnvisible = true;
        return;
    }
}

// 槽函数：选中/双击树节点时的图片加载逻辑
void PicShow::SlotSelectItem(const QString &path)
{
    _material_album_root.clear();
    SetOriginalComparison(false);
    if (path.isEmpty())
    {
        Clear();
        return;
    }
    _parameter_dragging = false;
    _undo_stack->clear();
    // 读取原始图片文件并重置视图与滤镜状态
    if (!_document.load(path))
    {
        Clear();
        qWarning() << "[PhotoEditor] Cannot load image:" << path;
        return;
    }
    _document.setParameters({});
    SyncControlsFromDocument();
    ResetViewFit();
    RebuildTransformedPixmap();
}
// 槽函数：上一张/下一张切换时的图片更新逻辑
void PicShow::SlotUpdatePic(const QString &path)
{
    SlotSelectItem(path);
}


void PicShow::SyncControlsFromDocument()
{
    if (_compare_button) _compare_button->setEnabled(!_document.sourceImage().isNull());
    if (_restore_original_button) _restore_original_button->setEnabled(!_document.sourceImage().isNull());
    if (_save_material_button) _save_material_button->setEnabled(!_document.sourceImage().isNull() && !_document.state().crop.isEmpty());
    setProperty("photoModified", _document.isModified());
    setWindowTitle(_document.isModified() ? tr("照片编辑 * 已修改") : tr("照片编辑"));
    _adjustments->SetParameters(_document.parameters());
}

void PicShow::ApplyEditState(const PhotoEditDocument::EditState &state)
{
    _document.setState(state);
    SyncControlsFromDocument();
    RebuildTransformedPixmap();
}

void PicShow::PushEdit(const PhotoEditDocument::EditState &before,
                       const PhotoEditDocument::EditState &after, const QString &text)
{
    if (!(before == after))
        _undo_stack->push(new PhotoEditCommand(this, before, after, text));
}

void PicShow::FinishParameterDrag()
{
    if (!_parameter_dragging) return;
    _parameter_dragging = false;
    const auto after = _document.state();
    PushEdit(_drag_start_state, after, tr("拖动调色滑块"));
    // 松开后即使数值回到起点，也要恢复全分辨率预览。
    RebuildTransformedPixmap();
}

void PicShow::SlotUndo()
{
    FinishParameterDrag();
    _undo_stack->undo();
}

void PicShow::SlotRedo()
{
    FinishParameterDrag();
    _undo_stack->redo();
}

void PicShow::SlotCrop()
{
    if (_document.sourceImage().isNull()) return;
    FinishParameterDrag();
    const auto before = _document.state();
    QSize originalSize = _document.sourceImage().size();
    if (before.rotation % 180 != 0) originalSize.transpose();
    CropDialog dialog(_document.Render(), originalSize, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QRect sourceCrop = _document.sourceCropFromOrientedRect(dialog.selection());
    if (sourceCrop.isEmpty()) return;
    auto after = before;
    after.crop = sourceCrop == _document.sourceImage().rect() ? QRect() : sourceCrop;
    PushEdit(before, after, tr("裁剪"));
}

void PicShow::SetOriginalComparison(bool enabled)
{
    ui->label->SetOriginalComparison(enabled);
    if (!ui->label->IsComparing() && _compare_button) _compare_button->setDown(false);
}

void PicShow::SlotSaveMaterial()
{
    if (_document.sourceImage().isNull() || _document.state().crop.isEmpty()) return;
    SetOriginalComparison(false);
    FinishParameterDrag();
    const PhotoEditDocument snapshot = _document;
    const QString albumRoot = _material_album_root;
    MaterialSaveDialog dialog(snapshot.sourcepath(), snapshot.outputSize(), albumRoot, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString path = dialog.outputPath();
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += ".png";
    if (QFileInfo::exists(path)
        && QMessageBox::question(this, tr("覆盖素材文件"), tr("文件已存在，是否替换？\n%1").arg(path),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    ImageExportService::Options options;
    options.jpegQuality = dialog.jpegQuality();
    const auto result = ImageExportService::exportDocument(snapshot, path, options);
    if (!result.success) {
        QMessageBox::warning(this, tr("素材保存失败"), result.error);
        return;
    }
    if (dialog.addToAlbum()) emit SigMaterialSaved(QFileInfo(path).absoluteFilePath(), albumRoot);
    else QMessageBox::information(this, tr("素材已保存"), tr("素材已保存，原照片和编辑历史保持不变：\n%1").arg(path));
}
