#include "duplicatereviewdialog.h"
#include <QFileInfo>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QPixmap>
#include <QIcon>

DuplicateReviewDialog::DuplicateReviewDialog(const QVector<DuplicatePhotoFinder::PhotoHash> &photos,
    const QVector<DuplicatePhotoFinder::Match> &matches, QWidget *parent) : QDialog(parent)
{
    setObjectName("duplicateReviewDialog");
    this->setWindowTitle(tr("🧹 感知哈希 (dHash) 相似/重复照片智能检测报告"));
    this->resize(720, 480);
    this->setStyleSheet("QDialog { background-color: rgb(43,45,48); color: rgb(230,233,238); }");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QLabel *summaryLbl = new QLabel(
        tr("共提取 <b>%1</b> 张图像的 64-bit dHash 特征指纹，发现 <b>%2</b> 组重复/相似照片（勾选代表待清理的冗余副本）：")
            .arg(photos.size())
            .arg(matches.size()), this);
    summaryLbl->setStyleSheet("color: rgb(225,228,235); font-size: 13px; padding: 4px;");
    mainLayout->addWidget(summaryLbl);

    _list = new QListWidget(this);
    auto *listWid = _list;
    _list->setObjectName("duplicateReviewList");
    listWid->setIconSize(QSize(72, 72));
    listWid->setSpacing(6);
    listWid->setStyleSheet(
        "QListWidget { background-color: rgb(54,56,60); color: rgb(230,233,238); border: 1px solid rgb(85,88,94); border-radius: 5px; }"
        "QListWidget::item { padding: 6px; border-bottom: 1px solid rgb(70,73,78); }");

    for (int i = 0; i < matches.size(); ++i)
    {
        const auto &pair = matches[i];
        const auto &keep = photos[pair.keepIndex];
        const auto &duplicate = photos[pair.duplicateIndex];
        QFileInfo fiKeep(keep.path);
        QFileInfo fiDup(duplicate.path);

        QString text = QString(
            "【相似组 #%1】 相似度: %2%  (汉明距离: %3 / 64)\n"
            "  ├─ 保留基准: %4  [dHash: 0x%5]\n"
            "  └─ 待清副本: %6  [dHash: 0x%7]  (%8)")
            .arg(i + 1)
            .arg(pair.similarity, 0, 'f', 1)
            .arg(pair.distance)
            .arg(fiKeep.fileName())
            .arg(keep.hash, 16, 16, QChar('0'))
            .arg(fiDup.fileName())
            .arg(duplicate.hash, 16, 16, QChar('0'))
            .arg(fiDup.absoluteFilePath());

        QPixmap thumb(duplicate.path);
        QListWidgetItem *lItem = new QListWidgetItem(
            QIcon(thumb.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation)),
            text, listWid);
        lItem->setFlags(lItem->flags() | Qt::ItemIsUserCheckable);
        lItem->setCheckState(Qt::Checked);
        lItem->setData(Qt::UserRole, duplicate.path);
    }
    mainLayout->addWidget(listWid, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton *btnClean = new QPushButton(tr("移到回收站…"), this);
    btnClean->setIcon(QIcon(":/icon/line/remove.svg"));
    btnClean->setIconSize(QSize(20, 20));
    btnClean->setObjectName("duplicateTrashButton");
    btnClean->setMinimumHeight(32);
    QPushButton *btnClose = new QPushButton(tr("关闭"), this);
    btnClose->setObjectName("canvasToolBtn");
    btnClose->setMinimumHeight(32);
    btnLayout->addWidget(btnClean);
    btnLayout->addWidget(btnClose);
    mainLayout->addLayout(btnLayout);

    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);
    btnClean->setEnabled(!SelectedPaths().isEmpty());
    connect(_list, &QListWidget::itemChanged, this, [this, btnClean]() {
        btnClean->setEnabled(!SelectedPaths().isEmpty());
    });
    connect(btnClean, &QPushButton::clicked, this, [this]() {
        if (!SelectedPaths().isEmpty()) accept();
    });
}

QStringList DuplicateReviewDialog::SelectedPaths() const
{
    QStringList paths;
    for (int row = 0; row < _list->count(); ++row) {
        const auto *item = _list->item(row);
        if (item->checkState() == Qt::Checked) paths.append(item->data(Qt::UserRole).toString());
    }
    paths.removeDuplicates();
    return paths;
}
