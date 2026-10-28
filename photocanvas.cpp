#include "photocanvas.h"
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QtMath>

PhotoCanvas::PhotoCanvas(QWidget *parent) : QLabel(parent)
{
    installEventFilter(this);
    setMouseTracking(true);
}

void PhotoCanvas::SetFrame(const QImage &source, const QImage &preview, const QSize &outputSize,
                           bool modified, int rotation)
{
    _source = source;
    _preview = QPixmap::fromImage(preview);
    _output_size = outputSize;
    _modified = modified;
    _rotation = rotation;
    if (source.isNull()) {
        _show_original = false;
        _is_dragging = false;
        setCursor(Qt::ArrowCursor);
    }
    update();
}

void PhotoCanvas::SetOriginalComparison(bool enabled)
{
    _show_original = enabled && !_source.isNull();
    _is_dragging = false;
    setCursor(Qt::ArrowCursor);
    update();
}

void PhotoCanvas::ResetViewFit()
{
    _zoom_factor = 1.0;
    _is_actual_pixel = false;
    _pan_offset = QPointF(0.0, 0.0);
    _is_dragging = false;
    setCursor(Qt::ArrowCursor);
    emit SigZoomTextChanged("100% 自适应");
}

void PhotoCanvas::ZoomIn()
{
    if (_preview.isNull()) return;
    _is_actual_pixel = false;
    _zoom_factor = qBound(0.1, _zoom_factor * 1.2, 8.0);
    emit SigZoomTextChanged(QString("%1%").arg(qRound(_zoom_factor * 100)));
    update();
}

void PhotoCanvas::ZoomOut()
{
    if (_preview.isNull()) return;
    _is_actual_pixel = false;
    _zoom_factor = qBound(0.1, _zoom_factor / 1.2, 8.0);
    emit SigZoomTextChanged(QString("%1%").arg(qRound(_zoom_factor * 100)));
    update();
}

void PhotoCanvas::ZoomFit()
{
    ResetViewFit();
    update();
}

void PhotoCanvas::ZoomActual()
{
    if (_preview.isNull()) return;
    _is_actual_pixel = true;
    _pan_offset = QPointF(0.0, 0.0);
    _is_dragging = false;
    setCursor(Qt::ArrowCursor);
    if (width() > 0 && height() > 0)
    {
        QSize base_size = _output_size.scaled(this->size(), Qt::KeepAspectRatio);
        if (base_size.width() > 0)
        {
            _zoom_factor = qBound(0.1, static_cast<double>(_output_size.width()) / base_size.width(), 8.0);
        }
    }
    emit SigZoomTextChanged("1:1 原图");
    update();
}

bool PhotoCanvas::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this)
    {
        if (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate
            || event->type() == QEvent::FocusOut) {
            _is_dragging = false;
            setCursor(Qt::ArrowCursor);
        }
        if (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate)
            SetOriginalComparison(false);
        if (_show_original && event->type() != QEvent::Paint)
            return QLabel::eventFilter(watched, event);
        if (event->type() == QEvent::Wheel && !_preview.isNull())
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
                const QPointF labelCenter(this->width() / 2.0, this->height() / 2.0);
                const QPointF relCursor = cursorPos - labelCenter - _pan_offset;
                _pan_offset -= relCursor * ((_zoom_factor / oldZoom) - 1.0);

                emit SigZoomTextChanged(QString("%1%").arg(qRound(_zoom_factor * 100)));
                update();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonPress && !_preview.isNull())
        {
            emit SigFocusRequested();
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                _is_dragging = true;
                _drag_last_pos = mouseEvent->pos();
                this->setCursor(Qt::ClosedHandCursor);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove && _is_dragging)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint delta = mouseEvent->pos() - _drag_last_pos;
            _drag_last_pos = mouseEvent->pos();
            _pan_offset += QPointF(delta.x(), delta.y());
            update();
            return true;
        }
        else if (event->type() == QEvent::MouseButtonRelease && _is_dragging)
        {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                _is_dragging = false;
                this->setCursor(Qt::ArrowCursor);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonDblClick && !_preview.isNull())
        {
            if (_is_actual_pixel || qAbs(_zoom_factor - 1.0) > 0.01 || !_pan_offset.isNull())
            {
                ZoomFit();
            }
            else
            {
                ZoomActual();
            }
            return true;
        }
        else if (event->type() == QEvent::Paint)
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

            // 若当前未加载图片，绘制空状态引导提示
            if (_preview.isNull() || this->width() <= 0 || this->height() <= 0)
            {
                painter.setPen(QColor(135, 140, 148));
                QFont f = painter.font();
                f.setPointSize(11);
                painter.setFont(f);
                painter.drawText(this->rect(), Qt::AlignCenter,
                                 tr("🖼️ 暂无展示图片\n\n单击或双击左侧相册目录中的照片，或使用 Ctrl+O 打开项目开始浏览"));
                return true;
            }

            const QSize displaySize = _show_original ? _source.size() : _output_size;
            const int fullW = displaySize.width();
            const int fullH = displaySize.height();

            // 基于未损耗的高清原图尺寸计算等比例适应 label 尺寸的基准绘制大小
            QSize base_size = QSize(fullW, fullH).scaled(this->size(), Qt::KeepAspectRatio);
            QSizeF target_size;
            if (_show_original)
            {
                target_size = QSizeF(base_size);
            }
            else if (_is_actual_pixel)
            {
                target_size = QSizeF(fullW, fullH);
            }
            else
            {
                target_size = QSizeF(base_size) * _zoom_factor;
            }

            // 计算居中叠加鼠标拖拽平移后的目标矩形区域
            const QPointF displayPan = _show_original ? QPointF() : _pan_offset;
            double x = (this->width() - target_size.width()) / 2.0 + displayPan.x();
            double y = (this->height() - target_size.height()) / 2.0 + displayPan.y();
            QRectF target_rect(x, y, target_size.width(), target_size.height());

            // 将高清原图绘制到目标矩形区域中
            if (_show_original)
                painter.drawImage(target_rect, _source);
            else
                painter.drawPixmap(target_rect.toRect(), _preview);

            // 左上角绘制半透明 HUD 状态胶囊（显示当前缩放倍率、像素尺寸与旋转角度）
            QString hudText = QString(" %1  |  %2 × %3 px ")
                                  .arg(_show_original ? tr("原图对比 · 自适应") : (_is_actual_pixel ? "1:1 原图" : QString("%1%").arg(qRound(_zoom_factor * 100))))
                                  .arg(fullW)
                                  .arg(fullH);
            if (_modified) hudText += tr(" | 已修改 ");
            if (!_show_original && _rotation != 0)
            {
                hudText += QString(" | 旋转 %1° ").arg(_rotation);
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
    return QLabel::eventFilter(watched, event);
}
