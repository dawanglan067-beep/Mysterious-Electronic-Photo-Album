#include "picshow.h"
#include "ui_picshow.h"
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
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
#include <QTransform>
#include <QSlider>
#include <QComboBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QtMath>
#include <algorithm>

// ======================== HistogramWidget 实现 ========================

PicShow::PicShow(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PicShow)
    , _b_btnvisible(false)
    , _zoom_factor(1.0)
    , _is_actual_pixel(false)
    , _pan_offset(0.0, 0.0)
    , _is_dragging(false)
    , _rotate_angle(0)
    , _flip_horizontal(false)
    , _flip_vertical(false)
    , _toolbar_wid(nullptr)
    , _btn_zoom_reset(nullptr)
    , _info_panel(nullptr)
    , _histogram_wid(nullptr)
    , _brightness(0)
    , _contrast(0)
    , _saturation(0)
    , _filter_type(ImageProcessor::FilterNone)
    , _slider_bright(nullptr)
    , _slider_contrast(nullptr)
    , _slider_sat(nullptr)
    , _lbl_bright_val(nullptr)
    , _lbl_contrast_val(nullptr)
    , _lbl_sat_val(nullptr)
    , _combo_filter(nullptr)
{
    ui->setupUi(this);

    // 核心优化：为 label 安装事件过滤器，拦截其 Paint / 滚轮 / 鼠标拖拽事件进行自主交互绘制
    // 彻底解决 QLabel::setPixmap 污染 sizeHint 导致窗口无法缩小以及布局挤压裁切图片的问题
    ui->label->installEventFilter(this);
    ui->label->setMouseTracking(true);

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

    // ===== 数字图像算法工坊区块 =====
    QLabel *titleAlgo = new QLabel(tr("🎨 数字图像算法工坊"), _info_panel);
    titleAlgo->setObjectName("inspectorSectionTitle");
    infoLayout->addWidget(titleAlgo);

    QHBoxLayout *filterRow = new QHBoxLayout();
    QLabel *lblFilter = new QLabel(tr("算子滤镜:"), _info_panel);
    lblFilter->setStyleSheet("color: rgb(175, 180, 188); font-size: 12px;");
    _combo_filter = new QComboBox(_info_panel);
    _combo_filter->setObjectName("studioCombo");
    _combo_filter->addItem(tr("原图 (无算子)"), ImageProcessor::FilterNone);
    _combo_filter->addItem(tr("加权灰度化 (Grayscale)"), ImageProcessor::FilterGrayscale);
    _combo_filter->addItem(tr("复古怀旧矩阵 (Sepia)"), ImageProcessor::FilterSepia);
    _combo_filter->addItem(tr("反色负片 (Invert)"), ImageProcessor::FilterInvert);
    _combo_filter->addItem(tr("高斯平滑滤波 (3×3 Gaussian)"), ImageProcessor::FilterGaussianBlur);
    _combo_filter->addItem(tr("拉普拉斯锐化 (Laplacian)"), ImageProcessor::FilterLaplacianSharpen);
    _combo_filter->addItem(tr("Sobel 梯度边缘检测"), ImageProcessor::FilterSobelEdge);
    _combo_filter->addItem(tr("浮雕卷积特征 (Emboss)"), ImageProcessor::FilterEmboss);
    filterRow->addWidget(lblFilter);
    filterRow->addWidget(_combo_filter, 1);
    infoLayout->addLayout(filterRow);

    QGridLayout *sliderGrid = new QGridLayout();
    sliderGrid->setHorizontalSpacing(6);
    sliderGrid->setVerticalSpacing(4);

    auto addSliderRow = [&](int row, const QString &name, QSlider* &slider, QLabel* &valLbl) {
        QLabel *lbl = new QLabel(name, _info_panel);
        lbl->setStyleSheet("color: rgb(175, 180, 188); font-size: 12px;");
        slider = new QSlider(Qt::Horizontal, _info_panel);
        slider->setRange(-100, 100);
        slider->setValue(0);
        valLbl = new QLabel("0", _info_panel);
        valLbl->setFixedWidth(28);
        valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valLbl->setStyleSheet("color: rgb(220, 224, 230); font-size: 11px;");
        sliderGrid->addWidget(lbl, row, 0);
        sliderGrid->addWidget(slider, row, 1);
        sliderGrid->addWidget(valLbl, row, 2);
        connect(slider, &QSlider::valueChanged, this, &PicShow::SlotAdjustParamsChanged);
        connect(slider, &QSlider::sliderReleased, this, &PicShow::SlotAdjustParamsChanged);
    };

    addSliderRow(0, tr("亮度:"), _slider_bright, _lbl_bright_val);
    addSliderRow(1, tr("对比度:"), _slider_contrast, _lbl_contrast_val);
    addSliderRow(2, tr("饱和度:"), _slider_sat, _lbl_sat_val);
    infoLayout->addLayout(sliderGrid);

    connect(_combo_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PicShow::SlotAdjustParamsChanged);

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
    _zoom_factor = 1.0;
    _is_actual_pixel = false;
    _pan_offset = QPointF(0.0, 0.0);
    if (_btn_zoom_reset)
    {
        _btn_zoom_reset->setText("100% 自适应");
    }
}

void PicShow::RebuildTransformedPixmap()
{
    if (_src_image.isNull())
    {
        Original_map = QPixmap();
        _histogram_wid->ClearHistogram();
        UpdateImageMetadata();
        ReloadPic();
        return;
    }

    QImage transformed = _src_image;
    if (_flip_horizontal || _flip_vertical)
    {
        transformed = transformed.mirrored(_flip_horizontal, _flip_vertical);
    }
    if (_rotate_angle != 0)
    {
        QTransform trans;
        trans.rotate(_rotate_angle);
        transformed = transformed.transformed(trans, Qt::SmoothTransformation);
    }

    // 应用数字图像算法管线（色彩空间调节 + 卷积/点运算算子）
    if (_brightness != 0 || _contrast != 0 || _saturation != 0 || _filter_type != ImageProcessor::FilterNone)
    {
        // 当鼠标正在按住拖拽滑块且原图分辨率较高（> 1600px）时，使用降采样预览保证 60fps 丝滑跟手，松开滑块后立即渲染全分辨率原图
        const bool isSliderDragging = (_slider_bright && _slider_bright->isSliderDown()) ||
                                      (_slider_contrast && _slider_contrast->isSliderDown()) ||
                                      (_slider_sat && _slider_sat->isSliderDown());
        if (isSliderDragging && (transformed.width() > 1600 || transformed.height() > 1600))
        {
            transformed = transformed.scaled(1600, 1600, Qt::KeepAspectRatio, Qt::FastTransformation);
        }
        transformed = ApplyImageAlgorithms(transformed);
    }

    Original_map = QPixmap::fromImage(transformed);
    _histogram_wid->UpdateHistogram(transformed);
    UpdateImageMetadata();
    ReloadPic();
}

void PicShow::UpdateImageMetadata()
{
    if (_selected_path.isEmpty() || _src_image.isNull())
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

    QFileInfo fi(_selected_path);
    _lbl_meta_name->setText(fi.fileName());

    const int w = (_rotate_angle % 180 != 0) ? _src_image.height() : _src_image.width();
    const int h = (_rotate_angle % 180 != 0) ? _src_image.width() : _src_image.height();
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
                                 .arg(_src_image.depth())
                                 .arg(_src_image.hasAlphaChannel() ? "RGBA" : "RGB"));

    // 计算当前处理后图像的 RGB 通道平均亮度
    QImage sampleSrc = !Original_map.isNull() ? Original_map.toImage() : _src_image;
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
    _selected_path.clear();
    _src_image = QImage();
    Original_map = QPixmap();
    _rotate_angle = 0;
    _flip_horizontal = false;
    _flip_vertical = false;
    if (_slider_bright) { _slider_bright->blockSignals(true); _slider_bright->setValue(0); _slider_bright->blockSignals(false); }
    if (_slider_contrast) { _slider_contrast->blockSignals(true); _slider_contrast->setValue(0); _slider_contrast->blockSignals(false); }
    if (_slider_sat) { _slider_sat->blockSignals(true); _slider_sat->setValue(0); _slider_sat->blockSignals(false); }
    if (_combo_filter) { _combo_filter->blockSignals(true); _combo_filter->setCurrentIndex(0); _combo_filter->blockSignals(false); }
    _brightness = 0;
    _contrast = 0;
    _saturation = 0;
    _filter_type = ImageProcessor::FilterNone;
    if (_lbl_bright_val) _lbl_bright_val->setText("0");
    if (_lbl_contrast_val) _lbl_contrast_val->setText("0");
    if (_lbl_sat_val) _lbl_sat_val->setText("0");
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

// 事件过滤器：接管 ui->label 的 Paint、滚轮缩放、鼠标拖拽平移、双击切换事件
bool PicShow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->label)
    {
        if (event->type() == QEvent::Wheel && !Original_map.isNull())
        {
            QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);
            const int delta = wheelEvent->angleDelta().y();
            if (delta != 0)
            {
                const double factor = (delta > 0) ? 1.15 : (1.0 / 1.15);
                const double oldZoom = _zoom_factor;
                _zoom_factor = qBound(0.1, _zoom_factor * factor, 8.0);
                _is_actual_pixel = false;

                // 以鼠标光标所在点为中心平滑缩放平移偏移
                const QPointF cursorPos = wheelEvent->position();
                const QPointF labelCenter(ui->label->width() / 2.0, ui->label->height() / 2.0);
                const QPointF relCursor = cursorPos - labelCenter - _pan_offset;
                _pan_offset -= relCursor * ((_zoom_factor / oldZoom) - 1.0);

                if (_btn_zoom_reset)
                {
                    _btn_zoom_reset->setText(QString("%1%").arg(qRound(_zoom_factor * 100)));
                }
                ReloadPic();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonPress && !Original_map.isNull())
        {
            this->setFocus();
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                _is_dragging = true;
                _drag_last_pos = mouseEvent->pos();
                ui->label->setCursor(Qt::ClosedHandCursor);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove && _is_dragging)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint delta = mouseEvent->pos() - _drag_last_pos;
            _drag_last_pos = mouseEvent->pos();
            _pan_offset += QPointF(delta.x(), delta.y());
            ReloadPic();
            return true;
        }
        else if (event->type() == QEvent::MouseButtonRelease && _is_dragging)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                _is_dragging = false;
                ui->label->setCursor(Qt::ArrowCursor);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonDblClick && !Original_map.isNull())
        {
            if (_is_actual_pixel || qAbs(_zoom_factor - 1.0) > 0.01 || !_pan_offset.isNull())
            {
                SlotZoomFit();
            }
            else
            {
                SlotZoomActual();
            }
            return true;
        }
        else if (event->type() == QEvent::Paint)
        {
            QPainter painter(ui->label);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

            // 若当前未加载图片，绘制空状态引导提示
            if (Original_map.isNull() || ui->label->width() <= 0 || ui->label->height() <= 0)
            {
                painter.setPen(QColor(135, 140, 148));
                QFont f = painter.font();
                f.setPointSize(11);
                painter.setFont(f);
                painter.drawText(ui->label->rect(), Qt::AlignCenter,
                                 tr("🖼️ 暂无展示图片\n\n单击或双击左侧相册目录中的照片，或使用 Ctrl+O 打开项目开始浏览"));
                return true;
            }

            const int fullW = (_rotate_angle % 180 != 0) ? _src_image.height() : _src_image.width();
            const int fullH = (_rotate_angle % 180 != 0) ? _src_image.width() : _src_image.height();

            // 基于未损耗的高清原图尺寸计算等比例适应 label 尺寸的基准绘制大小
            QSize base_size = QSize(fullW, fullH).scaled(ui->label->size(), Qt::KeepAspectRatio);
            QSizeF target_size;
            if (_is_actual_pixel)
            {
                target_size = QSizeF(fullW, fullH);
            }
            else
            {
                target_size = QSizeF(base_size) * _zoom_factor;
            }

            // 计算居中叠加鼠标拖拽平移后的目标矩形区域
            double x = (ui->label->width() - target_size.width()) / 2.0 + _pan_offset.x();
            double y = (ui->label->height() - target_size.height()) / 2.0 + _pan_offset.y();
            QRectF target_rect(x, y, target_size.width(), target_size.height());

            // 将高清原图绘制到目标矩形区域中
            painter.drawPixmap(target_rect.toRect(), Original_map);

            // 左上角绘制半透明 HUD 状态胶囊（显示当前缩放倍率、像素尺寸与旋转角度）
            QString hudText = QString(" %1  |  %2 × %3 px ")
                                  .arg(_is_actual_pixel ? "1:1 原图" : QString("%1%").arg(qRound(_zoom_factor * 100)))
                                  .arg(fullW)
                                  .arg(fullH);
            if (_rotate_angle != 0)
            {
                hudText += QString(" | 旋转 %1° ").arg(_rotate_angle);
            }
            QFont hudFont = painter.font();
            hudFont.setPointSize(9);
            painter.setFont(hudFont);
            QRect fmRect = painter.fontMetrics().boundingRect(hudText);
            QRect hudBox(12, 12, fmRect.width() + 16, fmRect.height() + 10);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(25, 27, 30, 175));
            painter.drawRoundedRect(hudBox, 6, 6);
            painter.setPen(QColor(225, 228, 232));
            painter.drawText(hudBox, Qt::AlignCenter, hudText);

            return true; // 返回 true 表示已完全处理绘制事件，无需 label 默认绘制
        }
    }
    return QDialog::eventFilter(watched, event);
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
    if (Original_map.isNull()) return;
    _is_actual_pixel = false;
    _zoom_factor = qBound(0.1, _zoom_factor * 1.2, 8.0);
    if (_btn_zoom_reset)
        _btn_zoom_reset->setText(QString("%1%").arg(qRound(_zoom_factor * 100)));
    ReloadPic();
}

void PicShow::SlotZoomOut()
{
    if (Original_map.isNull()) return;
    _is_actual_pixel = false;
    _zoom_factor = qBound(0.1, _zoom_factor / 1.2, 8.0);
    if (_btn_zoom_reset)
        _btn_zoom_reset->setText(QString("%1%").arg(qRound(_zoom_factor * 100)));
    ReloadPic();
}

void PicShow::SlotZoomFit()
{
    ResetViewFit();
    ReloadPic();
}

void PicShow::SlotZoomActual()
{
    if (Original_map.isNull()) return;
    _is_actual_pixel = true;
    _pan_offset = QPointF(0.0, 0.0);
    if (ui && ui->label && ui->label->width() > 0 && ui->label->height() > 0)
    {
        QSize base_size = Original_map.size().scaled(ui->label->size(), Qt::KeepAspectRatio);
        if (base_size.width() > 0)
        {
            _zoom_factor = qBound(0.1, static_cast<double>(Original_map.width()) / base_size.width(), 8.0);
        }
    }
    if (_btn_zoom_reset)
        _btn_zoom_reset->setText("1:1 原图");
    ReloadPic();
}

void PicShow::SlotRotateLeft()
{
    if (_src_image.isNull()) return;
    _rotate_angle = (_rotate_angle + 270) % 360;
    RebuildTransformedPixmap();
}

void PicShow::SlotRotateRight()
{
    if (_src_image.isNull()) return;
    _rotate_angle = (_rotate_angle + 90) % 360;
    RebuildTransformedPixmap();
}

void PicShow::SlotFlipH()
{
    if (_src_image.isNull()) return;
    _flip_horizontal = !_flip_horizontal;
    RebuildTransformedPixmap();
}

void PicShow::SlotFlipV()
{
    if (_src_image.isNull()) return;
    _flip_vertical = !_flip_vertical;
    RebuildTransformedPixmap();
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
    if (_selected_path.isEmpty()) return;
    QFileInfo fi(_selected_path);
    if (!fi.exists()) return;
#ifdef Q_OS_WIN
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(fi.absoluteFilePath())});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
#endif
}

void PicShow::SlotAdjustParamsChanged()
{
    if (_slider_bright)
    {
        _brightness = _slider_bright->value();
        _lbl_bright_val->setText(QString::number(_brightness));
    }
    if (_slider_contrast)
    {
        _contrast = _slider_contrast->value();
        _lbl_contrast_val->setText(QString::number(_contrast));
    }
    if (_slider_sat)
    {
        _saturation = _slider_sat->value();
        _lbl_sat_val->setText(QString::number(_saturation));
    }
    if (_combo_filter)
    {
        _filter_type = static_cast<ImageProcessor::FilterType>(_combo_filter->currentData().toInt());
    }
    if (!_src_image.isNull())
    {
        RebuildTransformedPixmap();
    }
}

void PicShow::SlotResetAlgorithms()
{
    if (_slider_bright) { _slider_bright->blockSignals(true); _slider_bright->setValue(0); _slider_bright->blockSignals(false); }
    if (_slider_contrast) { _slider_contrast->blockSignals(true); _slider_contrast->setValue(0); _slider_contrast->blockSignals(false); }
    if (_slider_sat) { _slider_sat->blockSignals(true); _slider_sat->setValue(0); _slider_sat->blockSignals(false); }
    if (_combo_filter) { _combo_filter->blockSignals(true); _combo_filter->setCurrentIndex(0); _combo_filter->blockSignals(false); }
    _brightness = 0;
    _contrast = 0;
    _saturation = 0;
    _filter_type = ImageProcessor::FilterNone;
    if (_lbl_bright_val) _lbl_bright_val->setText("0");
    if (_lbl_contrast_val) _lbl_contrast_val->setText("0");
    if (_lbl_sat_val) _lbl_sat_val->setText("0");
    RebuildTransformedPixmap();
}

void PicShow::SlotSaveProcessedImage()
{
    if (Original_map.isNull())
    {
        return;
    }
    QFileInfo fi(_selected_path);
    QString defaultPath = fi.dir().absoluteFilePath(fi.completeBaseName() + "_edited." + (fi.suffix().isEmpty() ? "png" : fi.suffix()));
    QString savePath = QFileDialog::getSaveFileName(
        this,
        tr("导出处理后的图像"),
        defaultPath,
        tr("PNG 图像 (*.png);;JPEG 图像 (*.jpg *.jpeg);;BMP 图像 (*.bmp)"));
    if (savePath.isEmpty())
    {
        return;
    }
    if (Original_map.save(savePath))
    {
        QMessageBox::information(this, tr("导出成功"), tr("处理后的图像已成功保存至：\n%1").arg(savePath));
    }
    else
    {
        QMessageBox::warning(this, tr("导出失败"), tr("无法写入目标图像文件，请检查路径权限。"));
    }
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
    _selected_path = path;
    if (path.isEmpty())
    {
        Clear();
        return;
    }
    // 读取原始图片文件并重置视图与滤镜状态
    _src_image.load(path);
    _rotate_angle = 0;
    _flip_horizontal = false;
    _flip_vertical = false;
    if (_slider_bright) { _slider_bright->blockSignals(true); _slider_bright->setValue(0); _slider_bright->blockSignals(false); }
    if (_slider_contrast) { _slider_contrast->blockSignals(true); _slider_contrast->setValue(0); _slider_contrast->blockSignals(false); }
    if (_slider_sat) { _slider_sat->blockSignals(true); _slider_sat->setValue(0); _slider_sat->blockSignals(false); }
    if (_combo_filter) { _combo_filter->blockSignals(true); _combo_filter->setCurrentIndex(0); _combo_filter->blockSignals(false); }
    _brightness = 0;
    _contrast = 0;
    _saturation = 0;
    _filter_type = ImageProcessor::FilterNone;
    if (_lbl_bright_val) _lbl_bright_val->setText("0");
    if (_lbl_contrast_val) _lbl_contrast_val->setText("0");
    if (_lbl_sat_val) _lbl_sat_val->setText("0");
    ResetViewFit();
    RebuildTransformedPixmap();
}

// 槽函数：上一张/下一张切换时的图片更新逻辑
void PicShow::SlotUpdatePic(const QString &path)
{
    SlotSelectItem(path);
}

// 槽函数：供外部调用的清空逻辑
void PicShow::SlotClear()
{
    Clear();
}



QImage PicShow::ApplyImageAlgorithms(const QImage &src) const
{
    return ImageProcessor::Apply(src, {_brightness, _contrast, _saturation, _filter_type});
}
