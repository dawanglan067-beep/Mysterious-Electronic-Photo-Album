#pragma once
#include <QWidget>
#include <QImage>
#include <QVector>
// 实时 RGB 色彩直方图绘制组件
class HistogramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HistogramWidget(QWidget *parent = nullptr);
    void UpdateHistogram(const QImage &img);
    void ClearHistogram();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<int> _hist_r;
    QVector<int> _hist_g;
    QVector<int> _hist_b;
    QVector<int> _hist_l;
    int _max_count;
    bool _has_data;
};
