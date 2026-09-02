#include "picshow.h"
#include "ui_picshow.h"
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPainter>

PicShow::PicShow(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PicShow)
    , _b_btnvisible(false)
{
    ui->setupUi(this);

    // 核心优化：为 label 安装事件过滤器，拦截其 Paint 事件进行自主绘制
    // 彻底解决 QLabel::setPixmap 污染 sizeHint 导致窗口无法缩小以及布局挤压裁切图片的问题
    ui->label->installEventFilter(this);

    // 初始化上一张/下一张按钮的图标（常规状态、悬停状态、按下状态）
    ui->previousBtn->SetIcons(":/icon/previous.png",
                              ":/icon/previous_hover.png",
                              ":/icon/previous_press.png");
    ui->nextBtn->SetIcons(":/icon/next.png",
                          ":/icon/next_hover.png",
                          ":/icon/next_press.png");

    // 为上一张按钮设置图形透明度特效，初始透明度为 0（隐藏）
    QGraphicsOpacityEffect *opacity_pre = new QGraphicsOpacityEffect(this);
    opacity_pre->setOpacity(0);
    ui->previousBtn->setGraphicsEffect(opacity_pre);

    // 为下一张按钮设置图形透明度特效，初始透明度为 0（隐藏）
    QGraphicsOpacityEffect *opacity_next = new QGraphicsOpacityEffect(this);
    opacity_next->setOpacity(0);
    ui->nextBtn->setGraphicsEffect(opacity_next);

    // 创建上一张按钮的透明度渐变动画，时长 500ms
    _animation_show_pre = new QPropertyAnimation(opacity_pre, "opacity", this);
    _animation_show_pre->setEasingCurve(QEasingCurve::Linear);
    _animation_show_pre->setDuration(500);

    // 创建下一张按钮的透明度渐变动画，时长 500ms
    _animation_show_next = new QPropertyAnimation(opacity_next, "opacity", this);
    _animation_show_next->setEasingCurve(QEasingCurve::Linear);
    _animation_show_next->setDuration(500);

    // 绑定按钮点击事件到对应信号，供外部 ProTreeWidget 捕获并切换图片
    connect(ui->nextBtn, &QPushButton::clicked, this, &PicShow::SigNextClicked);
    connect(ui->previousBtn, &QPushButton::clicked, this, &PicShow::SigPreClicked);
}

PicShow::~PicShow()
{
    delete ui;
}

// 刷新图片显示区域（请求 label 重新触发 Paint 事件进行绘制）
void PicShow::ReloadPic()
{
    if (ui && ui->label)
    {
        ui->label->update();
    }
}

// 清空当前展示的图片数据与路径，并刷新重绘为空白
void PicShow::Clear()
{
    _selected_path.clear();
    Original_map = QPixmap();
    if (ui && ui->label)
    {
        ui->label->update();
    }
}

// 事件处理：捕获鼠标进入/离开 PicShow 区域事件，触发按钮淡入/淡出
bool PicShow::event(QEvent *event)
{
    switch(event->type())
    {
    case QEvent::Enter:
        // 鼠标进入展示区，淡入显示上一张/下一张按钮
        ShowPreNextBtns(true);
        break;
    case QEvent::Leave:
        // 鼠标离开展示区，淡出隐藏上一张/下一张按钮
        ShowPreNextBtns(false);
        break;
    default:
        break;
    }
    return QDialog::event(event);
}

// 事件过滤器：接管 ui->label 的 Paint 事件进行高清等比例自适应绘制
bool PicShow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->label && event->type() == QEvent::Paint)
    {
        // 确保图片有效且展示区域宽高大于 0
        if (!Original_map.isNull() && ui->label->width() > 0 && ui->label->height() > 0)
        {
            QPainter painter(ui->label);
            // 启用抗锯齿与双线性平滑插值算法，保证缩放后的图像最高画质
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

            // 基于未损耗的高清原图 Original_map 计算等比例适应 label 尺寸的最佳绘制大小
            QSize scaled_size = Original_map.size().scaled(ui->label->size(), Qt::KeepAspectRatio);

            // 计算居中对齐的左上角 (x, y) 坐标
            int x = (ui->label->width() - scaled_size.width()) / 2;
            int y = (ui->label->height() - scaled_size.height()) / 2;
            QRect target_rect(x, y, scaled_size.width(), scaled_size.height());

            // 将高清原图绘制到计算出的居中目标矩形区域中
            painter.drawPixmap(target_rect, Original_map);
            return true; // 返回 true 表示已完全处理绘制事件，无需 label 默认绘制
        }
    }
    return QDialog::eventFilter(watched, event);
}

// 控制上一张/下一张按钮的渐变动画逻辑
void PicShow::ShowPreNextBtns(bool b_show)
{
    // 需要隐藏且当前为显示状态：启动淡出动画（1 -> 0）
    if(!b_show && _b_btnvisible)
    {
        _animation_show_pre->stop();
        _animation_show_pre->setStartValue(1);
        _animation_show_pre->setEndValue(0);
        _animation_show_pre->start();

        _animation_show_next->stop();
        _animation_show_next->setStartValue(1);
        _animation_show_next->setEndValue(0);
        _animation_show_next->start();

        _b_btnvisible = false;
        return;
    }
    // 需要显示且当前为隐藏状态：启动淡入动画（0 -> 1）
    if(b_show && !_b_btnvisible)
    {
        _animation_show_pre->stop();
        _animation_show_pre->setStartValue(0);
        _animation_show_pre->setEndValue(1);
        _animation_show_pre->start();

        _animation_show_next->stop();
        _animation_show_next->setStartValue(0);
        _animation_show_next->setEndValue(1);
        _animation_show_next->start();

        _b_btnvisible = true;
        return;
    }
}

// 槽函数：选中/双击树节点时的图片加载逻辑
void PicShow::SlotSelectItem(const QString &path)
{
    _selected_path = path;
    if (path.isEmpty())
    {
        Clear();
        return;
    }
    // 读取原始图片文件到 Original_map 中保存完整分辨率
    Original_map.load(path);
    ReloadPic();
}

// 槽函数：上一张/下一张切换时的图片更新逻辑
void PicShow::SlotUpdatePic(const QString &path)
{
    _selected_path = path;
    if (path.isEmpty())
    {
        Clear();
        return;
    }
    Original_map.load(path);
    ReloadPic();
}

// 槽函数：供外部调用的清空逻辑
void PicShow::SlotClear()
{
    Clear();
}
