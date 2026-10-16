#include "picanimationwid.h"
#include "protreeitem.h"
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

PicAnimationWid::PicAnimationWid(QWidget *parent)
    : QWidget{parent}
    , _factor(0.0f)
    , _hold_ms(0.0f)
    , _speed_mult(1.0f)
    , _effect_mode(EffectFade)
    , _b_loop(true)
    , _timer(nullptr)
    , _cur_item(nullptr)
    , _first_item(nullptr)
    , _last_item(nullptr)
    , _preload_cursor(nullptr)
    , _b_start(false)
{
    // 1. 创建动画驱动定时器
    _timer = new QTimer(this);

    // 2. 关键连接：定时器每到时间触发 timeout 信号，调用 TimeOut 槽函数推进一帧动画
    connect(_timer, &QTimer::timeout, this, &PicAnimationWid::TimeOut);
}

PicAnimationWid::~PicAnimationWid()
{
    if (_timer)
    {
        _timer->stop();
    }
}

void PicAnimationWid::SetTransitionEffect(TransitionEffect effect)
{
    _effect_mode = effect;
    update();
}

void PicAnimationWid::SetSpeedMultiplier(float mult)
{
    _speed_mult = qBound(0.25f, mult, 4.0f);
}

void PicAnimationWid::SetLoopMode(bool loop)
{
    _b_loop = loop;
}

ProTreeItem* PicAnimationWid::GetNextVisibleItem(ProTreeItem *item) const
{
    if (!item) return nullptr;
    ProTreeItem *cur = item->GetNextItem();
    while (cur && cur->isHidden())
    {
        cur = cur->GetNextItem();
    }
    return cur;
}

ProTreeItem* PicAnimationWid::GetPreVisibleItem(ProTreeItem *item) const
{
    if (!item) return nullptr;
    ProTreeItem *cur = item->GetPreItem();
    while (cur && cur->isHidden())
    {
        cur = cur->GetPreItem();
    }
    return cur;
}

void PicAnimationWid::PreloadNextThumbnailStep()
{
    if (!_preload_cursor) return;
    auto *pro_cur = dynamic_cast<ProTreeItem*>(_preload_cursor);
    while (pro_cur && pro_cur->isHidden())
    {
        pro_cur = pro_cur->GetNextItem();
    }
    if (!pro_cur)
    {
        _preload_cursor = nullptr;
        return;
    }
    const QString path = pro_cur->GetPath();
    if (_map_items.find(path) == _map_items.end())
    {
        _map_items[path] = pro_cur;
        emit SigUpPreList(pro_cur);
    }
    _preload_cursor = GetNextVisibleItem(pro_cur);
}

void PicAnimationWid::EnsureItemInPreList(QTreeWidgetItem *target_item)
{
    if (!target_item) return;
    auto *target_pro = dynamic_cast<ProTreeItem*>(target_item);
    if (!target_pro) return;

    const QString target_path = target_pro->GetPath();
    if (_map_items.find(target_path) != _map_items.end())
    {
        return;
    }

    // 按相册先后顺序推进缩略图加载游标直至覆盖目标节点，保证底部列表顺序与相册严格一致
    while (_preload_cursor)
    {
        QTreeWidgetItem *just_loaded = _preload_cursor;
        PreloadNextThumbnailStep();
        if (just_loaded == target_item || _map_items.find(target_path) != _map_items.end())
        {
            return;
        }
    }

    if (_map_items.find(target_path) == _map_items.end())
    {
        _map_items[target_path] = target_pro;
        emit SigUpPreList(target_item);
    }
}

// 设置并加载指定节点的图片数据（用于初始显示或选定展示）
void PicAnimationWid::SetPixmap(QTreeWidgetItem *item)
{
    if (!item)
    {
        return;
    }

    auto *tree_item = dynamic_cast<ProTreeItem*>(item);
    if (!tree_item)
    {
        return;
    }

    _factor = 0.0f;
    _hold_ms = 0.0f;

    // 1. 获取当前图片路径并加载为旧图片 _pixmap1
    auto path = tree_item->GetPath();
    //存储图片的内存
    _pixmap1.load(path);
    _scaled1 = QPixmap();
    //把当前展示的图片记录下来
    _cur_item = tree_item;

    // 2. 按相册顺序缓存当前节点到映射表中，并通知底部预览缩略图列表
    EnsureItemInPreList(item);

    // 通知目录树同步高亮选中当前节点
    emit SigSelectItem(item);

    // 3. 预加载下一张可见图片为 _pixmap2，为转场动画做准备
    auto *next_item = GetNextVisibleItem(tree_item);
    if (!next_item && _b_loop && _first_item && _first_item != item)
    {
        next_item = dynamic_cast<ProTreeItem*>(_first_item);
    }
    if (!next_item)
    {
        _pixmap2 = QPixmap();
        _scaled2 = QPixmap();
        return;
    }

    auto next_path = next_item->GetPath();
    _pixmap2.load(next_path);
    _scaled2 = QPixmap();
    EnsureItemInPreList(next_item);

    // 首次进入轮播时预先填充首屏缩略图栏（最多18张），方便用户直接点击底部任意缩略图跳转
    while (_preload_cursor && _map_items.size() < 18)
    {
        PreloadNextThumbnailStep();
    }
}

// 启动幻灯片自动轮播
void PicAnimationWid::Start()
{
    emit SigStart();
    emit SigStartMusic(); // 伴随播放背景音乐
    _factor = 0.0f;
    _hold_ms = 0.0f;
    _b_start = true;
    if (_timer)
    {
        _timer->start(15); // 每 15ms 触发一次帧刷新，提供 60fps 丝滑动画
    }
}

// 停止/暂停幻灯片自动轮播
void PicAnimationWid::Stop()
{
    emit SigStop();
    emit SigStopMusic(); // 停止背景音乐
    if (_timer)
    {
        _timer->stop();
    }
    _factor = 0.0f;
    _hold_ms = 0.0f;
    _b_start = false;
    update();
}

// 手动切换到下一张
void PicAnimationWid::SlidNext()
{
    if (!_cur_item)
    {
        return;
    }

    auto *cur_pro_item = dynamic_cast<ProTreeItem*>(_cur_item);
    if (!cur_pro_item)
    {
        return;
    }

    auto *next_item = GetNextVisibleItem(cur_pro_item);
    if (!next_item && _b_loop && _first_item)
    {
        next_item = dynamic_cast<ProTreeItem*>(_first_item);
    }
    if (!next_item)
    {
        return;
    }

    _factor = 0.0f;
    _hold_ms = 0.0f;
    SetPixmap(next_item);
    update();
}

// 手动切换到上一张
void PicAnimationWid::SlidPre()
{
    if (!_cur_item)
    {
        return;
    }

    auto *cur_pro_item = dynamic_cast<ProTreeItem*>(_cur_item);
    if (!cur_pro_item)
    {
        return;
    }

    auto *pre_item = GetPreVisibleItem(cur_pro_item);
    if (!pre_item && _b_loop && _last_item)
    {
        pre_item = dynamic_cast<ProTreeItem*>(_last_item);
    }
    if (!pre_item)
    {
        return;
    }

    _factor = 0.0f;
    _hold_ms = 0.0f;
    SetPixmap(pre_item);
    update();
}

// 定时器触发的槽函数：每 15ms 执行一次，推进一帧动画
void PicAnimationWid::TimeOut()
{
    if (!_cur_item)
    {
        Stop();
        update();
        return;
    }

    // 渐进式预加载底部缩略图（每 tick 最多加载 1 张，不阻塞界面）
    PreloadNextThumbnailStep();

    // 1. 静态停留展示阶段：1.0x 标准速度下每张照片先清晰静止停留 2000ms，再启动转场过渡
    const float kHoldDurationMs = 2000.0f;
    if (_hold_ms < kHoldDurationMs)
    {
        _hold_ms += 15.0f * _speed_mult;
        return;
    }

    // 2. 转场过渡动画阶段：每次递增 0.015 * _speed_mult（1.0x 标准速度下约 1.0 秒完成转场过渡）
    _factor += 0.015f * _speed_mult;

    // 当 _factor >= 1.0 时，说明当前过渡已完成，切换到下一张图片
    if (_factor >= 1.0f)
    {
        _factor = 0.0f;
        _hold_ms = 0.0f;
        //父类转换子类，这样子才可以调用子类自己构造的函数
        auto *cur_pro_item = dynamic_cast<ProTreeItem*>(_cur_item);
        if (!cur_pro_item)
        {
            Stop();
            update();
            return;
        }
        //查找下一张可见图片
        auto *next_pro_item = GetNextVisibleItem(cur_pro_item);
        if (!next_pro_item && _b_loop && _first_item)
        {
            next_pro_item = dynamic_cast<ProTreeItem*>(_first_item);
        }
        if (!next_pro_item)
        {
            Stop(); // 已到最后一张且未开启循环，停止轮播
            update();
            return;
        }

        if (next_pro_item != _cur_item)
        {
            SetPixmap(next_pro_item); // 加载下一张
        }
        update();
        return;
    }

    // 请求重绘当前帧
    update();
}

// 核心多转场特效绘制事件
void PicAnimationWid::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    //判断:如果第一张图片没有绘制好，直接返回
    if (_pixmap1.isNull())
    {
        return;
    }
    //绑定当前窗口(this)
    QPainter painter(this);
    //开启抗锯齿
    painter.setRenderHint(QPainter::Antialiasing, true);
    // 开启双线性平滑插值
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    //获取当前动画窗口自身的矩形几何信息
    QRect rect = this->rect();
    int w = rect.width();
    int h = rect.height();
    if (w <= 0 || h <= 0)
    {
        return;
    }

    // 仅当窗口尺寸变化或新图片刚加载时才重新计算高清缩放，避免每 15ms 重复缩放卡顿
    if (_last_size != rect.size())
    {
        _last_size = rect.size();
        _scaled1 = QPixmap();
        _scaled2 = QPixmap();
    }

    // 1. 等比例缩放 _pixmap1（使用缓存）
    if (_scaled1.isNull())
    {
        _scaled1 = _pixmap1.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    const QPixmap &scaled1 = _scaled1;
    int x1 = (w - scaled1.width()) / 2;
    int y1 = (h - scaled1.height()) / 2;

    if (!_pixmap2.isNull() && _scaled2.isNull())
    {
        _scaled2 = _pixmap2.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    const QPixmap &scaled2 = _scaled2;
    int x2 = (w - scaled2.width()) / 2;
    int y2 = (h - scaled2.height()) / 2;

    // 若当前处于暂停态（_factor == 0）或没有下一张图，直接居中绘制当前图
    if (_pixmap2.isNull() || _factor <= 0.0001f)
    {
        painter.drawPixmap(x1, y1, scaled1);
        return;
    }

    // 根据当前选中的转场特效模式进行渲染
    if (_effect_mode == EffectSlidePush)
    {
        // 特效 2：平滑左右推拉滑动（旧图向左滑出，新图从右侧滑入）
        int offset = static_cast<int>(w * _factor);
        painter.drawPixmap(x1 - offset, y1, scaled1);
        painter.drawPixmap(x2 + (w - offset), y2, scaled2);
        return;
    }
    else if (_effect_mode == EffectBlinds)
    {
        // 特效 3：百叶窗条带切换（将画布分为 12 个垂直条带逐步展开新图）
        painter.drawPixmap(x1, y1, scaled1);
        const int numStrips = 12;
        const double stripW = static_cast<double>(w) / numStrips;
        QPainterPath clipPath;
        for (int i = 0; i < numStrips; ++i)
        {
            double sx = i * stripW;
            double sw = stripW * _factor;
            clipPath.addRect(sx, 0, sw, h);
        }
        painter.save();
        painter.setClipPath(clipPath);
        painter.fillRect(rect, QColor(46, 47, 48));
        painter.drawPixmap(x2, y2, scaled2);
        painter.restore();
        return;
    }
    else if (_effect_mode == EffectIrisCircle)
    {
        // 特效 4：圆形光圈扩散（从画布中心向外圆形裁剪揭示新图）
        painter.drawPixmap(x1, y1, scaled1);
        double maxRadius = std::hypot(w / 2.0, h / 2.0);
        double curRadius = maxRadius * _factor;
        QPainterPath circlePath;
        circlePath.addEllipse(QPointF(w / 2.0, h / 2.0), curRadius, curRadius);
        painter.save();
        painter.setClipPath(circlePath);
        painter.fillRect(rect, QColor(46, 47, 48));
        painter.drawPixmap(x2, y2, scaled2);
        painter.restore();
        return;
    }
    else if (_effect_mode == EffectKenBurns)
    {
        // 特效 5：Ken Burns 电影感推拉缩放镜头（旧图缓慢放大淡出，新图从微放大拉回正常并淡入）
        painter.save();
        double scale1 = 1.0 + 0.14 * _factor;
        QSizeF s1(scaled1.width() * scale1, scaled1.height() * scale1);
        QRectF r1((w - s1.width()) / 2.0, (h - s1.height()) / 2.0, s1.width(), s1.height());
        painter.setOpacity(1.0 - _factor);
        painter.drawPixmap(r1.toRect(), scaled1);

        double scale2 = 1.12 - 0.12 * _factor;
        QSizeF s2(scaled2.width() * scale2, scaled2.height() * scale2);
        QRectF r2((w - s2.width()) / 2.0, (h - s2.height()) / 2.0, s2.width(), s2.height());
        painter.setOpacity(_factor);
        painter.drawPixmap(r2.toRect(), scaled2);
        painter.restore();
        return;
    }

    // 默认特效 1（EffectFade）：经典双图 Alpha 蒙版混合淡入淡出
    //创建一张尺寸与缩放后图片（scaled1）一模一样大的空白画板
    QPixmap alphaPixmap1(scaled1.size());
    //将整张画板全部填充为“纯透明色”（擦得干干净净）
    alphaPixmap1.fill(Qt::transparent);
    //拿起画笔，对着透明的画板
    QPainter p1(&alphaPixmap1);
    //将scaled1的像素原封不动的画到alphaPixmap1上
    p1.setCompositionMode(QPainter::CompositionMode_Source);
    //从坐标(0，0)开始作画
    p1.drawPixmap(0, 0, scaled1);
    //保留原图的颜色和细节，但把原图的透明度强制改成接下来我要涂的透明度
    p1.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    //计算当前瞬间的透明度数值，static_cast<int>转换整数
    int alpha1 = static_cast<int>(255 * (1.0f - _factor));
    //刷上透明度蒙版
    p1.fillRect(alphaPixmap1.rect(), QColor(0, 0, 0, alpha1));
    //绘制结束
    p1.end();

    // 居中绘制淡出图片
    painter.drawPixmap(x1, y1, alphaPixmap1);

    // 计算透明度图 2（逐渐淡入，透明度 = _factor）
    QPixmap alphaPixmap2(scaled2.size());
    alphaPixmap2.fill(Qt::transparent);

    QPainter p2(&alphaPixmap2);
    p2.setCompositionMode(QPainter::CompositionMode_Source);
    p2.drawPixmap(0, 0, scaled2);
    p2.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    //旧图越来越淡，新图越来越清晰
    int alpha2 = static_cast<int>(255 * _factor);
    p2.fillRect(alphaPixmap2.rect(), QColor(0, 0, 0, alpha2));
    p2.end();

    // 居中绘制淡入图片
    painter.drawPixmap(x2, y2, alphaPixmap2);
}

// 供底部预览缩略图点击时切换大图
void PicAnimationWid::UpSelectPixmap(QTreeWidgetItem *item)
{
    if (!item)
    {
        return;
    }

    auto *tree_item = dynamic_cast<ProTreeItem*>(item);
    if (!tree_item)
    {
        return;
    }

    _factor = 0.0f;
    _hold_ms = 0.0f;
    auto path = tree_item->GetPath();
    _pixmap1.load(path);
    _scaled1 = QPixmap();
    _cur_item = tree_item;

    EnsureItemInPreList(item);
    emit SigSelectItem(item);

    auto *next_item = GetNextVisibleItem(tree_item);
    if (!next_item && _b_loop && _first_item && _first_item != item)
    {
        next_item = dynamic_cast<ProTreeItem*>(_first_item);
    }
    if (!next_item)
    {
        _pixmap2 = QPixmap();
        _scaled2 = QPixmap();
        return;
    }

    auto next_path = next_item->GetPath();
    _pixmap2.load(next_path);
    _scaled2 = QPixmap();
    EnsureItemInPreList(next_item);
}

// 响应缩略图选中切换
void PicAnimationWid::SlotUpSelectShow(const QString &path)
{
    auto it = _map_items.find(path);
    if (it == _map_items.end())
    {
        return;
    }
    UpSelectPixmap(it.value());
    update();
}

// 响应播放/暂停切换
void PicAnimationWid::SlotStartOrStop()
{
    if (!_b_start)
    {
        Start();
    }
    else
    {
        Stop();
    }
}
