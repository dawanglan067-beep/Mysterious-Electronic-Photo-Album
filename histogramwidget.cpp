#include "histogramwidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>
HistogramWidget::HistogramWidget(QWidget *parent)
    : QWidget(parent)
    , _hist_r(256, 0)
    , _hist_g(256, 0)
    , _hist_b(256, 0)
    , _hist_l(256, 0)
    , _max_count(1)
    , _has_data(false)
{
    setMinimumHeight(130);
    setMaximumHeight(140);
}

void HistogramWidget::ClearHistogram()
{
    _hist_r.fill(0);
    _hist_g.fill(0);
    _hist_b.fill(0);
    _hist_l.fill(0);
    _max_count = 1;
    _has_data = false;
    update();
}

void HistogramWidget::UpdateHistogram(const QImage &img)
{
    if (img.isNull() || img.width() <= 0 || img.height() <= 0)
    {
        ClearHistogram();
        return;
    }

    _hist_r.fill(0);
    _hist_g.fill(0);
    _hist_b.fill(0);
    _hist_l.fill(0);

    QImage rgbImg = img.Format_RGB32 == img.format() || img.Format_ARGB32 == img.format()
                        ? img
                        : img.convertToFormat(QImage::Format_RGB32);

    const int w = rgbImg.width();
    const int h = rgbImg.height();
    // 对超大分辨率图像采用步长采样，保证直方图实时计算耗时 < 2ms
    const int step = std::max(1, static_cast<int>(std::sqrt((w * h) / 120000.0)));

    for (int y = 0; y < h; y += step)
    {
        const QRgb *line = reinterpret_cast<const QRgb*>(rgbImg.constScanLine(y));
        for (int x = 0; x < w; x += step)
        {
            const QRgb pixel = line[x];
            const int r = qRed(pixel);
            const int g = qGreen(pixel);
            const int b = qBlue(pixel);
            const int l = qBound(0, static_cast<int>(0.299 * r + 0.587 * g + 0.114 * b), 255);
            _hist_r[r]++;
            _hist_g[g]++;
            _hist_b[b]++;
            _hist_l[l]++;
        }
    }

    // 排除两端极值 0 和 255 的过曝/死黑尖峰干扰，使直方图主体分布清晰饱满
    _max_count = 1;
    for (int i = 2; i < 254; ++i)
    {
        _max_count = std::max({_max_count, _hist_r[i], _hist_g[i], _hist_b[i]});
    }
    if (_max_count <= 1)
    {
        for (int i = 0; i < 256; ++i)
        {
            _max_count = std::max({_max_count, _hist_r[i], _hist_g[i], _hist_b[i]});
        }
    }

    _has_data = true;
    update();
}

void HistogramWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect().adjusted(2, 2, -2, -2);
    // 绘制深色直方图背景框
    painter.setPen(QPen(QColor(80, 82, 86), 1));
    painter.setBrush(QColor(32, 34, 37));
    painter.drawRoundedRect(r, 6, 6);

    // 绘制内部参考网格线（四等分）
    painter.setPen(QPen(QColor(55, 58, 62), 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i)
    {
        int gx = r.left() + (r.width() * i) / 4;
        int gy = r.top() + (r.height() * i) / 4;
        painter.drawLine(gx, r.top(), gx, r.bottom());
        painter.drawLine(r.left(), gy, r.right(), gy);
    }

    if (!_has_data)
    {
        painter.setPen(QColor(130, 134, 140));
        painter.drawText(r, Qt::AlignCenter, tr("暂无直方图数据"));
        return;
    }

    auto drawChannelCurve = [&](const QVector<int> &hist, const QColor &strokeColor, const QColor &fillColor) {
        QPainterPath path;
        const double plotW = r.width() - 8.0;
        const double plotH = r.height() - 12.0;
        const double baseX = r.left() + 4.0;
        const double baseY = r.bottom() - 4.0;

        path.moveTo(baseX, baseY);
        for (int i = 0; i < 256; ++i)
        {
            double x = baseX + (i / 255.0) * plotW;
            // 平滑邻域均值滤波让曲线更自然
            double val = hist[i];
            if (i > 0 && i < 255)
            {
                val = (hist[i - 1] + hist[i] * 2.0 + hist[i + 1]) / 4.0;
            }
            double ratio = std::min(1.0, val / static_cast<double>(_max_count));
            double y = baseY - ratio * plotH;
            path.lineTo(x, y);
        }
        path.lineTo(baseX + plotW, baseY);
        path.closeSubpath();

        painter.setPen(Qt::NoPen);
        painter.setBrush(fillColor);
        painter.drawPath(path);

        painter.setPen(QPen(strokeColor, 1.3));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    };

    // 依次叠加绘制 B、G、R 三通道直方图与亮度参考线
    drawChannelCurve(_hist_b, QColor(68, 138, 255, 220), QColor(68, 138, 255, 65));
    drawChannelCurve(_hist_g, QColor(105, 240, 174, 220), QColor(105, 240, 174, 65));
    drawChannelCurve(_hist_r, QColor(255, 82, 82, 220), QColor(255, 82, 82, 65));
    drawChannelCurve(_hist_l, QColor(230, 230, 230, 130), QColor(255, 255, 255, 18));

    // 右上角绘制 RGB 通道图例标识
    painter.setPen(QColor(180, 184, 190));
    QFont f = painter.font();
    f.setPointSize(8);
    f.setBold(true);
    painter.setFont(f);
    painter.drawText(r.adjusted(8, 5, -8, -4), Qt::AlignTop | Qt::AlignRight, "RGB / Luma");
}

// ======================== PicShow 实现 ========================
