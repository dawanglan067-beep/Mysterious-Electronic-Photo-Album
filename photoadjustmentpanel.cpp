#include "photoadjustmentpanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QSignalBlocker>

PhotoAdjustmentPanel::PhotoAdjustmentPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("photoAdjustmentPanel");
    auto *infoLayout = new QVBoxLayout(this);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(6);
    // ===== 数字图像算法工坊区块 =====
    QLabel *titleAlgo = new QLabel(tr("🎨 数字图像算法工坊"), this);
    titleAlgo->setObjectName("inspectorSectionTitle");
    infoLayout->addWidget(titleAlgo);

    QHBoxLayout *filterRow = new QHBoxLayout();
    QLabel *lblFilter = new QLabel(tr("算子滤镜:"), this);
    lblFilter->setStyleSheet("color: rgb(175, 180, 188); font-size: 12px;");
    _combo_filter = new QComboBox(this);
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
        QLabel *lbl = new QLabel(name, this);
        lbl->setStyleSheet("color: rgb(175, 180, 188); font-size: 12px;");
        slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(-100, 100);
        slider->setValue(0);
        valLbl = new QLabel("0", this);
        valLbl->setFixedWidth(28);
        valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valLbl->setStyleSheet("color: rgb(220, 224, 230); font-size: 11px;");
        sliderGrid->addWidget(lbl, row, 0);
        sliderGrid->addWidget(slider, row, 1);
        sliderGrid->addWidget(valLbl, row, 2);
        connect(slider, &QSlider::valueChanged, this, [this, valLbl](int value) {
            valLbl->setText(QString::number(value));
            emit SigParametersChanged();
        });
        connect(slider, &QSlider::sliderPressed, this, &PhotoAdjustmentPanel::SigGestureStarted);
        connect(slider, &QSlider::sliderReleased, this, &PhotoAdjustmentPanel::SigGestureFinished);
    };

    addSliderRow(0, tr("亮度:"), _slider_bright, _lbl_bright_val);
    addSliderRow(1, tr("对比度:"), _slider_contrast, _lbl_contrast_val);
    addSliderRow(2, tr("饱和度:"), _slider_sat, _lbl_sat_val);
    infoLayout->addLayout(sliderGrid);

    connect(_combo_filter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PhotoAdjustmentPanel::SigParametersChanged);

}

ImageProcessor::Parameters PhotoAdjustmentPanel::Parameters() const
{
    ImageProcessor::Parameters parameters;
    if (_slider_bright)
    {
        parameters.brightness = _slider_bright->value();
    }
    if (_slider_contrast)
    {
        parameters.contrast = _slider_contrast->value();
    }
    if (_slider_sat)
    {
        parameters.saturation = _slider_sat->value();
    }
    if (_combo_filter)
    {
        parameters.filter = static_cast<ImageProcessor::FilterType>
            (_combo_filter->currentData().toInt());
    }
    return parameters;
}

void PhotoAdjustmentPanel::SetParameters(const ImageProcessor::Parameters &parameters)
{
    if (_slider_bright) {
        const QSignalBlocker blocker(_slider_bright);
        _slider_bright->setValue(parameters.brightness);
    }
    if (_slider_contrast) {
        const QSignalBlocker blocker(_slider_contrast);
        _slider_contrast->setValue(parameters.contrast);
    }
    if (_slider_sat) {
        const QSignalBlocker blocker(_slider_sat);
        _slider_sat->setValue(parameters.saturation);
    }
    if (_combo_filter) {
        const QSignalBlocker blocker(_combo_filter);
        _combo_filter->setCurrentIndex(_combo_filter->findData(parameters.filter));
    }
    if (_lbl_bright_val) _lbl_bright_val->setText(QString::number(parameters.brightness));
    if (_lbl_contrast_val) _lbl_contrast_val->setText(QString::number(parameters.contrast));
    if (_lbl_sat_val) _lbl_sat_val->setText(QString::number(parameters.saturation));
}

bool PhotoAdjustmentPanel::IsSliderDown() const
{
    return _slider_bright->isSliderDown() || _slider_contrast->isSliderDown() || _slider_sat->isSliderDown();
}
