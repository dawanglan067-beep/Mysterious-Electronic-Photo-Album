#include "photowallwidget.h"
#include "photowallmodel.h"
#include <QListView>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QItemSelectionModel>
#include <QSet>
#include <algorithm>

PhotoWallWidget::PhotoWallWidget(QWidget *parent) : QWidget(parent)
{
    setObjectName("photoWall");
    auto *layout = new QVBoxLayout(this);
    auto *toolbar = new QHBoxLayout;
    auto *selectAll = new QPushButton(QIcon(":/icon/line/album.svg"), tr("全选"), this);
    auto *clear = new QPushButton(tr("取消选择"), this);
    _export = new QPushButton(QIcon(":/icon/line/save.svg"), tr("导出 PDF 相册"), this);
    _delete = new QPushButton(QIcon(":/icon/line/remove.svg"), tr("删除所选（回收站）"), this);
    _delete->setObjectName("photoWallDeleteButton");
    auto *size = new QSlider(Qt::Horizontal, this);
    size->setObjectName("thumbnailSize");
    size->setRange(96, 256);
    size->setValue(160);
    size->setMaximumWidth(150);
    toolbar->addWidget(selectAll);
    toolbar->addWidget(clear);
    toolbar->addStretch();
    toolbar->addWidget(new QLabel(tr("缩略图"), this));
    toolbar->addWidget(size);
    toolbar->addWidget(_export);
    toolbar->addWidget(_delete);
    layout->addLayout(toolbar);
    _model = new PhotoWallModel(this);
    _view = new QListView(this);
    _view->setObjectName("photoWallView");
    _view->setModel(_model);
    _view->setViewMode(QListView::IconMode);
    _view->setResizeMode(QListView::Adjust);
    _view->setMovement(QListView::Static);
    _view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _view->setUniformItemSizes(true);
    _view->setLayoutMode(QListView::Batched);
    _view->setSpacing(8);
    _view->setIconSize(QSize(160, 160));
    _view->setGridSize(QSize(184, 200));
    layout->addWidget(_view, 1);
    _count = new QLabel(this);
    layout->addWidget(_count);
    connect(size, &QSlider::valueChanged, this, [this](int value) {
        _view->setIconSize(QSize(value, value));
        _view->setGridSize(QSize(value + 24, value + 40));
    });
    connect(selectAll, &QPushButton::clicked, _view, &QListView::selectAll);
    connect(clear, &QPushButton::clicked, _view, &QListView::clearSelection);
    connect(_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() { UpdateSelectionLabel(); });
    connect(_view, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        emit SigOpenPhoto(_model->PathAt(index.row()));
    });
    connect(_export, &QPushButton::clicked, this, &PhotoWallWidget::SigExportRequested);
    connect(_delete, &QPushButton::clicked, this, [this]() {
        const QStringList paths = SelectedPaths();
        if (!paths.isEmpty()) emit SigDeletePhotosRequested(paths);
    });
    UpdateSelectionLabel();
}

QStringList PhotoWallWidget::SelectedPaths() const
{
    auto selected = _view->selectionModel()->selectedIndexes();
    std::sort(selected.begin(), selected.end(), [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
    QStringList paths;
    for (const auto &index : selected) paths.append(_model->PathAt(index.row()));
    return paths;
}

void PhotoWallWidget::SetPhotos(const QList<PhotoEntry> &photos)
{
    const auto &old = _model->Photos();
    if (old.size() == photos.size() && std::equal(old.begin(), old.end(), photos.begin(),
        [](const PhotoEntry &a, const PhotoEntry &b) { return a.path == b.path; })) return;
    const QStringList paths = SelectedPaths();
    const QSet<QString> selected(paths.begin(), paths.end());
    _model->SetPhotos(photos);
    QItemSelection selection;
    for (int row = 0; row < _model->rowCount(); ++row) {
        if (selected.contains(_model->PathAt(row))) selection.select(_model->index(row), _model->index(row));
    }
    _view->selectionModel()->select(selection, QItemSelectionModel::Select);
    UpdateSelectionLabel();
}

void PhotoWallWidget::UpdateSelectionLabel()
{
    const int count = _view->selectionModel()->selectedIndexes().size();
    _count->setText(_model->rowCount() == 0 ? tr("当前相册没有符合筛选条件的照片，请打开或导入相册。")
        : tr("共 %1 张，已选 %2 张 · Ctrl 多选，Shift 连选，双击查看").arg(_model->rowCount()).arg(count));
    _export->setEnabled(count > 0);
    _delete->setEnabled(count > 0);
}
