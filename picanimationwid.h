#ifndef PICANIMATIONWID_H
#define PICANIMATIONWID_H

#include <QWidget>
#include <QTreeWidgetItem>
#include <QTimer>
#include <QMap>
#include <QPixmap>
#include <QPaintEvent>

class PicAnimationWid : public QWidget
{
    Q_OBJECT
public:
    explicit PicAnimationWid(QWidget *parent = nullptr);
    ~PicAnimationWid();

    // 设置并加载指定节点的图片数据（用于初始显示或选定展示）
    void SetPixmap(QTreeWidgetItem *item);

    // 启动幻灯片自动轮播
    void Start();

    // 停止/暂停幻灯片自动轮播
    void Stop();

    // 手动切换到下一张
    void SlidNext();

    // 手动切换到上一张
    void SlidPre();

protected:
    // 核心绘制事件：根据 _factor 混合绘制旧图淡出与新图淡入
    void paintEvent(QPaintEvent *event) override;

private:
    void UpSelectPixmap(QTreeWidgetItem *item); // 更新当前选中的图片

    float _factor;                              // 动画过渡因子（0.0 ~ 1.0），控制两张图片的淡入淡出透明度比例
    QTimer *_timer;                             // 动画驱动及轮播翻页定时器
    QPixmap _pixmap1;                           // 当前/上一张图片（动画中逐渐淡出，透明度 1 - _factor）
    QPixmap _pixmap2;                           // 目标/下一张图片（动画中逐渐淡入，透明度 _factor）
    QTreeWidgetItem *_cur_item;                 // 记录当前正在播放的相册节点指针
    QMap<QString, QTreeWidgetItem*> _map_items; // 路径与树节点映射表（便于快速根据路径定位节点）
    bool _b_start;                              // 播放状态标志（true: 正在播放；false: 暂停）

private slots:
    // 定时器触发的槽函数：每帧递增 _factor 并触发界面重绘
    void TimeOut();
public slots:
    void SlotSelectShow(QString &path);
signals:
    void SigUpPreList(QTreeWidgetItem *item);  // 通知底部缩略图列表更新
    void SigSelectItem(QTreeWidgetItem *item); // 通知目录树选中对应节点
    void SigStart();                           // 通知播放开始
    void SigStop();                            // 通知播放停止
    void SigStartMusic();                      // 通知播放背景音乐
    void SigStopMusic();                       // 通知停止背景音乐

public slots:
    void SlotUpSelectShow(QString path);       // 响应缩略图选中切换图片
    void SlotStartOrStop();                    // 响应播放/暂停按钮切换
};

#endif // PICANIMATIONWID_H
