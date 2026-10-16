#ifndef PICSHOW_H
#define PICSHOW_H

#include <QDialog>
#include <QPropertyAnimation>
#include <QImage>
#include <QVector>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include "histogramwidget.h"
#include "imageprocessor.h"

namespace Ui {
class PicShow;
}

class PicShow : public QDialog
{
    Q_OBJECT

public:
    explicit PicShow(QWidget *parent = nullptr);
    ~PicShow();

    // 重新加载/刷新当前图片显示（触发 label 重新绘制）
    void ReloadPic();

    // 清空当前图片数据并重绘清空展示区
    void Clear();

protected:
    // 监听鼠标进入/离开事件，用于控制上一张/下一张按钮的淡入淡出动画
    bool event(QEvent *event) override;

    // 事件过滤器：拦截 ui->label 的 Paint、滚轮缩放、鼠标拖拽平移、双击切换事件
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void reject() override {}

private:
    // 播放按钮透明度动画：b_show 为 true 表示淡入显示，false 表示淡出隐藏
    void ShowPreNextBtns(bool b_show);

    // 初始化专业画布工具栏与右侧 RGB 直方图/元数据信息面板
    void InitStudioUI();

    // 根据原始图像与当前旋转/镜像状态重新生成显示图像并刷新直方图与信息面板
    void RebuildTransformedPixmap();

    // 更新右侧图像元数据详情与 RGB 直方图
    void UpdateImageMetadata();

    // 重置视图缩放与平移偏移（自适应窗口）
    void ResetViewFit();

    Ui::PicShow *ui;
    QPropertyAnimation* _animation_show_pre;   // 上一张按钮的透明度动画
    QPropertyAnimation* _animation_show_next;  // 下一张按钮的透明度动画
    bool _b_btnvisible;                       // 标记当前按钮是否处于可见状态
    QString _selected_path;                   // 当前正在展示的图片本地绝对路径
    QImage _src_image;                        // 原始未变换图像数据
    QPixmap Original_map;                     // 当前经过几何变换后的高清原图数据（避免多次缩放导致画质退化）

    // ===== 专业画布交互状态 =====
    double _zoom_factor;                      // 缩放倍率（1.0 表示自适应窗口基准下的 100%，支持 0.1 ~ 8.0）
    bool _is_actual_pixel;                    // 是否处于 1:1 原始像素模式
    QPointF _pan_offset;                      // 画布拖拽平移偏移量
    bool _is_dragging;                        // 是否正在按住鼠标左键拖拽画布
    QPoint _drag_last_pos;                    // 上一次鼠标拖拽坐标
    int _rotate_angle;                        // 旋转角度（0, 90, 180, 270）
    bool _flip_horizontal;                    // 水平镜像翻转标志
    bool _flip_vertical;                      // 垂直镜像翻转标志

    // ===== 底部工具栏与右侧直方图/算法工坊面板控件 =====
    QWidget *_toolbar_wid;
    QPushButton *_btn_zoom_reset;
    QWidget *_info_panel;
    HistogramWidget *_histogram_wid;
    QLabel *_lbl_meta_name;
    QLabel *_lbl_meta_dim;
    QLabel *_lbl_meta_size;
    QLabel *_lbl_meta_depth;
    QLabel *_lbl_meta_rgb;
    QLabel *_lbl_meta_time;
    QLabel *_lbl_meta_path;

    // ===== 内置图像算法工坊状态与控件 =====
    using FilterType = ImageProcessor::FilterType;
    int _brightness;                          // 亮度调节 (-100 ~ +100)
    int _contrast;                            // 对比度调节 (-100 ~ +100)
    int _saturation;                          // 饱和度调节 (-100 ~ +100)
    FilterType _filter_type;                  // 当前选中的数字图像滤镜算子
    class QSlider *_slider_bright;
    class QSlider *_slider_contrast;
    class QSlider *_slider_sat;
    QLabel *_lbl_bright_val;
    QLabel *_lbl_contrast_val;
    QLabel *_lbl_sat_val;
    class QComboBox *_combo_filter;

    QImage ApplyImageAlgorithms(const QImage &src) const;

private slots:
    void SlotZoomIn();
    void SlotZoomOut();
    void SlotZoomFit();
    void SlotZoomActual();
    void SlotRotateLeft();
    void SlotRotateRight();
    void SlotFlipH();
    void SlotFlipV();
    void SlotToggleInfoPanel();
    void SlotOpenInExplorer();
    void SlotAdjustParamsChanged();
    void SlotResetAlgorithms();
    void SlotSaveProcessedImage();

public slots:
    // 树节点被双击选中时的图片加载槽函数
    void SlotSelectItem(const QString &path);

    // 切换上一张/下一张时的图片更新槽函数
    void SlotUpdatePic(const QString &path);

    // 外部清空图片槽函数（如相册关闭/删除时调用）
    void SlotClear();

signals:
    void SigNextClicked(); // 点击下一张按钮时发射的信号
    void SigPreClicked();  // 点击上一张按钮时发射的信号
};

#endif // PICSHOW_H

