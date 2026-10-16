#include "photowallmodel.h"
#include "thumbnailloader.h"
#include <QFileInfo>
#include <QIcon>
#include <QPainter>

PhotoWallModel::PhotoWallModel(QObject *parent)
    : QAbstractListModel(parent), _cache(48 * 1024), _loader(new ThumbnailLoader(this)),
      _placeholder(160, 120)
{
    _placeholder.fill(QColor(40, 46, 58));
    QPainter painter(&_placeholder);
    painter.setPen(QColor(150, 160, 178));
    painter.drawText(_placeholder.rect(), Qt::AlignCenter, tr("照片"));
    connect(_loader, &ThumbnailLoader::SigReady, this, [this](const QString &path, const QImage &image) {
        const auto it = _rows.constFind(path);
        if (it == _rows.cend()) return;
        QPixmap pixmap = image.isNull() ? _placeholder : QPixmap::fromImage(image);
        const int cost = qMax(1, int(pixmap.width() * pixmap.height() * 4 / 1024));
        _cache.insert(path, new QPixmap(pixmap), cost);
        const auto changed = index(it.value());
        emit dataChanged(changed, changed, {Qt::DecorationRole});
    });
}

int PhotoWallModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(_photos.size());
}

QVariant PhotoWallModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= _photos.size()) return {};
    const QString &path = _photos.at(index.row()).path;
    if (role == Qt::DisplayRole) return QFileInfo(path).fileName();
    if (role == Qt::ToolTipRole || role == Qt::UserRole) return path;
    if (role == Qt::DecorationRole) {
        if (auto *pixmap = _cache.object(path)) return QIcon(*pixmap);
        _loader->Request(path);
        return QIcon(_placeholder);
    }
    return {};
}

void PhotoWallModel::SetPhotos(const QList<PhotoEntry> &photos)
{
    _loader->Reset();
    beginResetModel();
    _photos = photos;
    _rows.clear();
    _cache.clear();
    for (int i = 0; i < _photos.size(); ++i) _rows.insert(_photos.at(i).path, i);
    endResetModel();
}

QString PhotoWallModel::PathAt(int row) const
{
    return row >= 0 && row < _photos.size() ? _photos.at(row).path : QString();
}
