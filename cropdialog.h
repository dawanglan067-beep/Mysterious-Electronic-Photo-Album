#pragma once
#include <QDialog>
#include <QImage>
#include <QRect>
#include <QWidget>
#include <QVector>

// 所有选区使用图像像素；显示缩放和平移仅影响坐标映射。
class CropCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit CropCanvas(const QImage &image, QWidget *parent = nullptr);
    QRect selection() const { return _selection; }
    void setSelection(const QRect &rect);
    void setAspectRatio(const QSize &ratio);
    QRectF imageRect() const;
signals:
    void selectionChanged(const QRect &rect);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
private:
    QPointF toImage(const QPointF &point) const;
    QVector<QPointF> handles() const;
    QImage _image;
    QRect _selection;
    QSize _ratio;
    int _handle = -1;
    bool _moving = false;
    bool _panning = false;
    Qt::MouseButton _panButton = Qt::NoButton;
    bool _spaceHeld = false;
    QPointF _start;
    QRect _startRect;
    QPointF _pan;
    QPointF _lastMouse;
    double _zoom = 1.0;
};

class CropDialog : public QDialog
{
    Q_OBJECT
public:
    CropDialog(const QImage &image, const QSize &originalSize, QWidget *parent = nullptr);
    QRect selection() const;
private:
    CropCanvas *_canvas;
};
