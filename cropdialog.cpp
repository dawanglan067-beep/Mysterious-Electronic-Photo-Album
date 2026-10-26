#include "cropdialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineF>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>
#include <numeric>

CropCanvas::CropCanvas(const QImage &image, QWidget *parent)
    : QWidget(parent), _image(image), _selection(image.rect())
{
    setObjectName("cropCanvas");
    setMinimumSize(320, 240);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

QRectF CropCanvas::imageRect() const
{
    if (_image.isNull()) return {};
    QSizeF size = _image.size();
    size.scale(QSizeF(qMax(1, width() - 32), qMax(1, height() - 32)), Qt::KeepAspectRatio);
    size *= _zoom;
    return QRectF(QPointF((width() - size.width()) / 2.0,
                          (height() - size.height()) / 2.0) + _pan, size);
}

QPointF CropCanvas::toImage(const QPointF &point) const
{
    const QRectF display = imageRect();
    if (display.isEmpty()) return {};
    return QPointF((point.x() - display.x()) * _image.width() / display.width(),
                   (point.y() - display.y()) * _image.height() / display.height());
}

void CropCanvas::setAspectRatio(const QSize &ratio)
{
    _ratio = ratio;
    if (_ratio.width() > 0 && _ratio.height() > 0) {
        const int divisor = std::gcd(_ratio.width(), _ratio.height());
        _ratio = QSize(_ratio.width() / divisor, _ratio.height() / divisor);
    } else {
        _ratio = {};
    }
    setSelection(_image.rect());
}

void CropCanvas::setSelection(const QRect &rect)
{
    QRect selection = rect.normalized().intersected(_image.rect());
    if (!_ratio.isEmpty()) {
        const int steps = qMin(selection.width() / _ratio.width(),
                               selection.height() / _ratio.height());
        QSize size(_ratio.width() * steps, _ratio.height() * steps);
        // 原图比例可能是互质的大整数，例如 4001:3001；此时允许
        // 不超过一个像素的比例取整误差，不能要求裁剪尺寸是整组倍数。
        if (qMax(_ratio.width(), _ratio.height()) > 16 && !selection.isEmpty()) {
            const double aspect = double(_ratio.width()) / _ratio.height();
            const int width = qMax(1, qMin(selection.width(), qFloor(selection.height() * aspect)));
            size = QSize(width, qMax(1, qMin(selection.height(), qRound(width / aspect))));
        }
        selection = QRect(QPoint(selection.x() + (selection.width() - size.width()) / 2,
                                  selection.y() + (selection.height() - size.height()) / 2), size);
    }
    _selection = selection;
    emit selectionChanged(_selection);
    update();
}

QVector<QPointF> CropCanvas::handles() const
{
    const QRectF rect(_selection);
    return {rect.topLeft(), QPointF(rect.center().x(), rect.top()), rect.topRight(),
            QPointF(rect.right(), rect.center().y()), rect.bottomRight(),
            QPointF(rect.center().x(), rect.bottom()), rect.bottomLeft(),
            QPointF(rect.left(), rect.center().y())};
}

void CropCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(30, 32, 35));
    const QRectF display = imageRect();
    if (display.isEmpty()) return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(display, _image);
    const double sx = display.width() / _image.width(), sy = display.height() / _image.height();
    const QRectF selected(display.x() + _selection.x() * sx, display.y() + _selection.y() * sy,
                          _selection.width() * sx, _selection.height() * sy);
    QPainterPath shade; shade.addRect(display); shade.addRect(selected);
    shade.setFillRule(Qt::OddEvenFill);
    painter.fillPath(shade, QColor(0, 0, 0, 150));
    painter.setPen(QPen(Qt::white, 1));
    painter.drawRect(selected);
    for (int i = 1; i < 3; ++i) {
        painter.drawLine(QPointF(selected.left() + selected.width() * i / 3, selected.top()),
                         QPointF(selected.left() + selected.width() * i / 3, selected.bottom()));
        painter.drawLine(QPointF(selected.left(), selected.top() + selected.height() * i / 3),
                         QPointF(selected.right(), selected.top() + selected.height() * i / 3));
    }
    painter.setBrush(Qt::white);
    for (const auto &handle : handles()) {
        const QPointF point(display.x() + handle.x() * sx, display.y() + handle.y() * sy);
        painter.drawRect(QRectF(point - QPointF(4, 4), QSizeF(8, 8)));
    }
}

void CropCanvas::mousePressEvent(QMouseEvent *event)
{
    if (_image.isNull()) return;
    if (_panning || _moving || _handle >= 0) return;
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton
        || (event->button() == Qt::LeftButton && _spaceHeld)) {
        _panning = true;
        _panButton = event->button();
        _lastMouse = event->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    _start = toImage(event->position());
    _startRect = _selection;
    _handle = -1;
    const auto points = handles();
    const double scale = imageRect().width() / _image.width();
    for (int i = 0; i < points.size(); ++i) {
        if (QLineF(_start, points[i]).length() * scale <= 10) {
            _handle = i;
            break;
        }
    }
    _moving = _handle < 0 && QRectF(_selection).contains(_start);
    if (_handle < 0 && !_moving) {
        _panning = true;
        _panButton = Qt::LeftButton;
        _lastMouse = event->position();
        setCursor(Qt::ClosedHandCursor);
    }
}

void CropCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (_panning) {
        _pan += event->position() - _lastMouse;
        _lastMouse = event->position();
        update();
        return;
    }
    if (!(event->buttons() & Qt::LeftButton)) return;
    const QPointF current = toImage(event->position());
    if (_moving) {
        QRect moved = _startRect.translated(qRound(current.x() - _start.x()),
                                            qRound(current.y() - _start.y()));
        moved.moveLeft(qBound(0, moved.x(), _image.width() - moved.width()));
        moved.moveTop(qBound(0, moved.y(), _image.height() - moved.height()));
        // 移动不重复执行比例取整，保持选区尺寸不变。
        _selection = moved;
        emit selectionChanged(_selection);
        update();
        return;
    }
    if (_handle < 0) return;
    const QRectF initial(_startRect);
    const bool left = _handle == 0 || _handle == 6 || _handle == 7;
    const bool right = _handle == 2 || _handle == 3 || _handle == 4;
    const bool top = _handle == 0 || _handle == 1 || _handle == 2;
    const bool bottom = _handle == 4 || _handle == 5 || _handle == 6;
    const QPointF anchor(left ? initial.right() : (right ? initial.left() : initial.center().x()),
                          top ? initial.bottom() : (bottom ? initial.top() : initial.center().y()));
    int width = _startRect.width(), height = _startRect.height();
    if (left || right) width = qMax(1, qRound(left ? anchor.x() - current.x() : current.x() - anchor.x()));
    if (top || bottom) height = qMax(1, qRound(top ? anchor.y() - current.y() : current.y() - anchor.y()));
    const int maxWidth = qMax(1, qFloor(left ? anchor.x() : (right ? _image.width() - anchor.x()
                                             : 2 * qMin(anchor.x(), _image.width() - anchor.x()))));
    const int maxHeight = qMax(1, qFloor(top ? anchor.y() : (bottom ? _image.height() - anchor.y()
                                             : 2 * qMin(anchor.y(), _image.height() - anchor.y()))));
    if (!_ratio.isEmpty()) {
        double steps = (left || right) ? double(width) / _ratio.width() : double(height) / _ratio.height();
        if ((left || right) && (top || bottom))
            steps = qMin(steps, double(height) / _ratio.height());
        const int maxSteps = qMin(maxWidth / _ratio.width(), maxHeight / _ratio.height());
        if (qMax(_ratio.width(), _ratio.height()) > 16) {
            const double aspect = double(_ratio.width()) / _ratio.height();
            width = qMax(1, qMin(qRound(steps * _ratio.width()),
                                 qMin(maxWidth, qFloor(maxHeight * aspect))));
            height = qMax(1, qMin(maxHeight, qRound(width / aspect)));
        } else {
            if (maxSteps < 1) return;
            const int units = qBound(1, qRound(steps), maxSteps);
            width = units * _ratio.width(); height = units * _ratio.height();
        }
    } else {
        width = qMin(width, maxWidth); height = qMin(height, maxHeight);
    }
    const int x = qRound(left ? anchor.x() - width : (right ? anchor.x() : anchor.x() - width / 2.0));
    const int y = qRound(top ? anchor.y() - height : (bottom ? anchor.y() : anchor.y() - height / 2.0));
    QRect selection(QPoint(x, y), QSize(width, height));
    selection.moveLeft(qBound(0, selection.x(), _image.width() - width));
    selection.moveTop(qBound(0, selection.y(), _image.height() - height));
    _selection = selection;
    emit selectionChanged(_selection);
    update();
}

void CropCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) { _moving = false; _handle = -1; }
    if (event->button() == _panButton) {
        _panning = false;
        _panButton = Qt::NoButton;
        if (_spaceHeld) setCursor(Qt::OpenHandCursor);
        else unsetCursor();
    }
}

void CropCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) {
        _spaceHeld = true;
        if (!_panning) setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CropCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) {
            _spaceHeld = false;
            if (!_panning) unsetCursor();
        }
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void CropCanvas::focusOutEvent(QFocusEvent *event)
{
    _spaceHeld = false;
    _panning = false;
    _panButton = Qt::NoButton;
    _moving = false;
    _handle = -1;
    unsetCursor();
    QWidget::focusOutEvent(event);
}

void CropCanvas::wheelEvent(QWheelEvent *event)
{
    if (_image.isNull() || event->angleDelta().y() == 0) return;
    const QPointF before = toImage(event->position());
    _zoom = qBound(0.25, _zoom * (event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15), 8.0);
    const QRectF display = imageRect();
    const QPointF after(display.x() + before.x() * display.width() / _image.width(),
                         display.y() + before.y() * display.height() / _image.height());
    _pan += event->position() - after;
    update();
    event->accept();
}

CropDialog::CropDialog(const QImage &image, const QSize &originalSize, QWidget *parent)
    : QDialog(parent), _canvas(new CropCanvas(image, this))
{
    setObjectName("photoCropDialog");
    setWindowTitle(tr("裁剪照片"));
    resize(900, 650);
    auto *layout = new QVBoxLayout(this);
    auto *ratio = new QComboBox(this);
    ratio->setObjectName("cropAspectRatio");
    ratio->addItem(tr("自由比例"), QSize());
    ratio->addItem(tr("原图比例"), originalSize);
    for (const auto &size : {QSize(1, 1), QSize(4, 3), QSize(3, 2), QSize(16, 9)})
        ratio->addItem(QString("%1:%2").arg(size.width()).arg(size.height()), size);
    layout->addWidget(ratio);
    auto *hint = new QLabel(tr("左键：拖控制点调整大小，拖框内移动选区，拖框外移动画布；滚轮缩放。Enter 应用，Esc 取消。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addWidget(_canvas, 1);
    auto *dimensions = new QLabel(this);
    dimensions->setObjectName("cropDimensions");
    layout->addWidget(dimensions);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("应用裁剪"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);
    const auto updateDimensions = [=](const QRect &rect) {
        dimensions->setText(tr("裁剪后：%1 × %2 像素").arg(rect.width()).arg(rect.height()));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!rect.isEmpty());
    };
    connect(_canvas, &CropCanvas::selectionChanged, this, updateDimensions);
    connect(ratio, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int) {
        _canvas->setAspectRatio(ratio->currentData().toSize());
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateDimensions(_canvas->selection());
    _canvas->setFocus();
}

QRect CropDialog::selection() const { return _canvas->selection(); }
