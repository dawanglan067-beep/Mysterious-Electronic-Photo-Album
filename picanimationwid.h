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

    // 幻灯片转场特效枚举
    enum TransitionEffect {
        EffectFade = 0,       // 经典 Alpha 淡入淡出
        EffectSlidePush = 1,  // 左右平滑推拉滑动
        EffectBlinds = 2,     // 百叶窗条带切换
        EffectIrisCircle = 3, // 圆形光圈扩散
        EffectKenBurns = 4    // Ken Burns 电影感推拉缩放
    };

    void SetFirstItem(QTreeWidgetItem *first_item) { _first_item = first_item; _preload_cursor = first_item; }
    void SetLastItem(QTreeWidgetItem *last_item) { _last_item = last_item; }
    void SetTransitionEffect(TransitionEffect effect);
    void SetSpeedMultiplier(float mult);
    void SetLoopMode(bool loop);

protected:
    // 核心绘制事件：根据 _factor 与当前转场特效混合绘制旧图与新图
    void paintEvent(QPaintEvent *event) override;

private:
    void UpSelectPixmap(QTreeWidgetItem *item); // 更新当前选中的图片
    class ProTreeItem* GetNextVisibleItem(class ProTreeItem *item) const;
    class ProTreeItem* GetPreVisibleItem(class ProTreeItem *item) const;
    void EnsureItemInPreList(QTreeWidgetItem *target_item);
    void PreloadNextThumbnailStep();

    float _factor;                              // 动画过渡因子（0.0 ~ 1.0），控制两张图片的过渡进度
    float _hold_ms;                             // 当前照片静态停留展示已累计的毫秒数
    float _speed_mult;                          // 动画播放倍速（0.5x ~ 2.0x）
    TransitionEffect _effect_mode;              // 当前选中的转场特效
    bool _b_loop;                               // 是否开启循环轮播
    QTimer *_timer;                             // 动画驱动及轮播翻页定时器
    QPixmap _pixmap1;                           // 当前/上一张图片
    QPixmap _pixmap2;                           // 目标/下一张图片
    QPixmap _scaled1;                           // 缓存的等比例缩放后图片1（避免每帧重复CPU缩放）
    QPixmap _scaled2;                           // 缓存的等比例缩放后图片2（避免每帧重复CPU缩放）
    QSize _last_size;                           // 上次缩放时的窗口尺寸
    QTreeWidgetItem *_cur_item;                 // 记录当前正在播放的相册节点指针
    QTreeWidgetItem *_first_item;               // 记录相册第一张图片节点（用于循环轮播）
    QTreeWidgetItem *_last_item;                // 记录相册最后一张图片节点（用于反向循环翻页）
    QTreeWidgetItem *_preload_cursor;           // 按相册顺序渐进预加载缩略图的游标指针
    QMap<QString, QTreeWidgetItem*> _map_items; // 路径与树节点映射表（便于快速根据路径定位节点）
    bool _b_start;                              // 播放状态标志（true: 正在播放；false: 暂停）

private slots:
    // 定时器触发的槽函数：每帧递增 _factor 并触发界面重绘
    void TimeOut();

signals:
    void SigUpPreList(QTreeWidgetItem *item);  // 通知底部缩略图列表更新
    void SigSelectItem(QTreeWidgetItem *item); // 通知目录树选中对应节点
    void SigStart();                           // 通知播放开始
    void SigStop();                            // 通知播放停止
    void SigStartMusic();                      // 通知播放背景音乐
    void SigStopMusic();                       // 通知停止背景音乐

public slots:
    void SlotUpSelectShow(const QString &path); // 响应缩略图选中切换图片
    void SlotStartOrStop();                     // 响应播放/暂停按钮切换
};

#endif // PICANIMATIONWID_H
