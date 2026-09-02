#include "picanimationwid.h"
#include "protreeitem.h"
#include <QTimer>
#include <QPainter>

PicAnimationWid::PicAnimationWid(QWidget *parent)
    : QWidget{parent}
    , _factor(0.0f)
    , _timer(nullptr)
    , _cur_item(nullptr)
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

    // 1. 获取当前图片路径并加载为旧图片 _pixmap1
    auto path = tree_item->GetPath();
    //存储图片的内存
    _pixmap1.load(path);
    //把当前展示的图片记录下来
    _cur_item = tree_item;

    // 2. 缓存当前节点到映射表中，并通知底部预览缩略图列表
    if (_map_items.find(path) == _map_items.end())
    {
        _map_items[path] = tree_item;
        emit SigUpPreList(item); // 发送信号通知底部列表添加缩略图
    }

    // 通知目录树同步高亮选中当前节点
    emit SigSelectItem(item);

    // 3. 预加载下一张图片为 _pixmap2，为淡入淡出动画做准备
    auto *next_item = tree_item->GetNextItem();
    if (!next_item)
    {
        return;
    }

    auto next_path = next_item->GetPath();
    _pixmap2.load(next_path);
    if (_map_items.find(next_path) == _map_items.end())
    {
        _map_items[next_path] = next_item;
        emit SigUpPreList(next_item); // 发送信号通知底部列表添加缩略图
    }
}

// 启动幻灯片自动轮播
void PicAnimationWid::Start()
{
    emit SigStart();
    emit SigStartMusic(); // 伴随播放背景音乐
    _factor = 0.0f;
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
    _b_start = false;
}

// 手动切换到下一张
void PicAnimationWid::SlidNext()
{
    Stop();
    if (!_cur_item)
    {
        return;
    }

    auto *cur_pro_item = dynamic_cast<ProTreeItem*>(_cur_item);
    if (!cur_pro_item)
    {
        return;
    }

    auto *next_item = cur_pro_item->GetNextItem();
    if (!next_item)
    {
        return;
    }

    SetPixmap(next_item);
    update();
}

// 手动切换到上一张
void PicAnimationWid::SlidPre()
{
    Stop();
    if (!_cur_item)
    {
        return;
    }

    auto *cur_pro_item = dynamic_cast<ProTreeItem*>(_cur_item);
    if (!cur_pro_item)
    {
        return;
    }

    auto *pre_item = cur_pro_item->GetPreItem();
    if (!pre_item)
    {
        return;
    }

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

    // 每次递增 0.01（大约经过 1.5 秒完成淡入淡出全过程）
    _factor += 0.01f;

    // 当 _factor >= 1.0 时，说明当前过渡已完成，切换到下一张图片
    if (_factor >= 1.0f)
    {
        _factor = 0.0f;
        //父类转换子类，这样子才可以调用子类自己构造的函数
        auto *cur_pro_item = dynamic_cast<ProTreeItem*>(_cur_item);
        if (!cur_pro_item)
        {
            Stop();
            update();
            return;
        }
        //查找下一张图片
        auto *next_pro_item = cur_pro_item->GetNextItem();
        if (!next_pro_item)
        {
            Stop(); // 已到最后一张，停止轮播
            update();
            return;
        }

        SetPixmap(next_pro_item); // 加载下一张
        update();
        return;
    }

    // 请求重绘当前帧
    update();
}

void PicAnimationWid::SlotSelectShow(QString &path)
{
    auto iter=_map_items.find(path);
    if(iter==_map_items.end())
    {
        return;
    }
    UpSelectPixmap(iter.value());
    update();
}

// 核心淡入淡出绘制事件
void PicAnimationWid::paintEvent(QPaintEvent *event)
{
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
    //获取当前动画窗口在屏幕/父容器中的矩形几何信息
    QRect rect = geometry();
    int w = rect.width();
    int h = rect.height();

    // 1. 等比例缩放 _pixmap1
    QPixmap scaled1 = _pixmap1.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);

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
    int x1 = (w - scaled1.width()) / 2;
    int y1 = (h - scaled1.height()) / 2;
    painter.drawPixmap(x1, y1, alphaPixmap1);

    if (_pixmap2.isNull())
    {
        return;
    }

    // 2. 等比例缩放 _pixmap2
    QPixmap scaled2 = _pixmap2.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);

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
    int x2 = (w - scaled2.width()) / 2;
    int y2 = (h - scaled2.height()) / 2;
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

    auto path = tree_item->GetPath();
    _pixmap1.load(path);
    _cur_item = tree_item;

    if (_map_items.find(path) == _map_items.end())
    {
        _map_items[path] = tree_item;
    }

    auto *next_item = tree_item->GetNextItem();
    if (!next_item)
    {
        return;
    }

    auto next_path = next_item->GetPath();
    _pixmap2.load(next_path);
    //查重防止重复添加成立是第一次出现，不成立说明出现过了
    if (_map_items.find(next_path) == _map_items.end())
    {
        _map_items[next_path] = next_item;
    }
}

// 响应缩略图选中切换
void PicAnimationWid::SlotUpSelectShow(QString path)
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
