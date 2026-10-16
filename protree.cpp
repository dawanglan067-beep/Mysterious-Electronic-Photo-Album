#include "protree.h"
#include "ui_protree.h"
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>

ProTree::ProTree(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ProTree)
{
    ui->setupUi(this);
    this->setMinimumWidth(378);
    this->setMaximumWidth(378);

    // 在目录树顶部插入智能搜索栏、星标收藏筛选与相似图查重快捷栏
    QWidget *filterBar = new QWidget(this);
    QHBoxLayout *filterLayout = new QHBoxLayout(filterBar);
    filterLayout->setContentsMargins(0, 2, 0, 4);
    filterLayout->setSpacing(5);

    QLineEdit *searchEdit = new QLineEdit(filterBar);
    searchEdit->setPlaceholderText(tr("🔍 搜索照片名称..."));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumHeight(26);
    searchEdit->setStyleSheet(
        "QLineEdit { background-color: rgb(58,60,65); color: rgb(230,233,238); "
        "border: 1px solid rgb(95,98,105); border-radius: 4px; padding: 2px 6px; }");

    QPushButton *btnFavFilter = new QPushButton(tr("⭐ 收藏"), filterBar);
    btnFavFilter->setObjectName("canvasToolBtn");
    btnFavFilter->setToolTip(tr("仅显示打上星标收藏的照片 (右键图片可收藏)"));
    btnFavFilter->setCheckable(true);
    btnFavFilter->setCursor(Qt::PointingHandCursor);
    btnFavFilter->setMinimumHeight(26);

    QPushButton *btnDupCheck = new QPushButton(tr("🧹 查重"), filterBar);
    btnDupCheck->setObjectName("canvasToolBtn");
    btnDupCheck->setToolTip(tr("基于 64-bit 差异感知哈希 (dHash) 算法检测相册中的相似/重复照片"));
    btnDupCheck->setCursor(Qt::PointingHandCursor);
    btnDupCheck->setMinimumHeight(26);

    QPushButton *btnMusicToggle = new QPushButton(tr("🎵 音乐"), filterBar);
    btnMusicToggle->setObjectName("canvasToolBtn");
    btnMusicToggle->setToolTip(tr("点击一键播放/停止背景音乐（按 Ctrl+M 打开背景音乐设置中心）"));
    btnMusicToggle->setCheckable(true);
    btnMusicToggle->setCursor(Qt::PointingHandCursor);
    btnMusicToggle->setMinimumHeight(26);

    filterLayout->addWidget(searchEdit, 1);
    filterLayout->addWidget(btnFavFilter);
    filterLayout->addWidget(btnDupCheck);
    filterLayout->addWidget(btnMusicToggle);

    ui->verticalLayout->insertWidget(1, filterBar);

    connect(searchEdit, &QLineEdit::textChanged, ui->treeWidget, &ProTreeWidget::SlotSearchTextChanged);
    connect(btnFavFilter, &QPushButton::toggled, ui->treeWidget, &ProTreeWidget::SlotSetOnlyFavorites);
    connect(btnDupCheck, &QPushButton::clicked, ui->treeWidget, &ProTreeWidget::SlotDetectDuplicates);
    connect(btnMusicToggle, &QPushButton::clicked, ui->treeWidget, &ProTreeWidget::SlotToggleMusic);
    connect(ui->treeWidget, &ProTreeWidget::SigMusicStateChanged, this, [btnMusicToggle](bool playing) {
        btnMusicToggle->blockSignals(true);
        btnMusicToggle->setChecked(playing);
        btnMusicToggle->setText(playing ? tr("🎵 播放中") : tr("🎵 音乐"));
        btnMusicToggle->blockSignals(false);
    });
}

ProTree::~ProTree()
{
    delete ui;
}

QTreeWidget *ProTree::GetTreeWidget()
{
    return ui->treeWidget;
}

void ProTree::AddProToTree(const QString name, const QString path)
{
    ui->treeWidget->AddProToTree(name,path);

}

