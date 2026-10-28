#pragma once
#include <QLabel>
#include <QImage>
#include <QPixmap>

// 只拥有显示缓存和视图状态，不拥有可编辑文档或撤销历史。
class PhotoCanvas : public QLabel
{
    Q_OBJECT
public:
    explicit PhotoCanvas(QWidget *parent = nullptr);
    void SetFrame(const QImage &source, const QImage &preview, const QSize &outputSize,
                  bool modified, int rotation);
    QImage PreviewImage() const { return _preview.toImage(); }
    bool IsComparing() const { return _show_original; }
    void SetOriginalComparison(bool enabled);
    void ResetViewFit();
    void ZoomIn();
    void ZoomOut();
    void ZoomFit();
    void ZoomActual();
signals:
    void SigZoomTextChanged(const QString &text);
    void SigFocusRequested();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    QImage _source;
    QPixmap _preview;
    QSize _output_size;
    bool _modified = false;
    int _rotation = 0;
    bool _show_original = false;
    double _zoom_factor = 1.0;
    bool _is_actual_pixel = false;
    QPointF _pan_offset;
    bool _is_dragging = false;
    QPoint _drag_last_pos;
};
