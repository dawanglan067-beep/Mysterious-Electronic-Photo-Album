#pragma once
#include <QWidget>
#include "imageprocessor.h"
class QSlider;
class QComboBox;
class QLabel;

// 控件展示参数并发出交互信号；编辑状态和手势历史由编辑器统一提交。
class PhotoAdjustmentPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PhotoAdjustmentPanel(QWidget *parent = nullptr);
    ImageProcessor::Parameters Parameters() const;
    void SetParameters(const ImageProcessor::Parameters &parameters);
    bool IsSliderDown() const;
signals:
    void SigParametersChanged();
    void SigGestureStarted();
    void SigGestureFinished();
private:
    QSlider *_slider_bright;
    QSlider *_slider_contrast;
    QSlider *_slider_sat;
    QComboBox *_combo_filter;
    QLabel *_lbl_bright_val;
    QLabel *_lbl_contrast_val;
    QLabel *_lbl_sat_val;
};
