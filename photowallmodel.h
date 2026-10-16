#ifndef PHOTOWALLMODEL_H
#define PHOTOWALLMODEL_H

#include <QAbstractListModel>
#include <QCache>
#include <QPixmap>
#include <QHash>
#include "photoentry.h"
class ThumbnailLoader;

class PhotoWallModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit PhotoWallModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    void SetPhotos(const QList<PhotoEntry> &photos);
    QString PathAt(int row) const;
    const QList<PhotoEntry> &Photos() const { return _photos; }
private:
    QList<PhotoEntry> _photos;
    QHash<QString, int> _rows;
    mutable QCache<QString, QPixmap> _cache;
    ThumbnailLoader *_loader;
    QPixmap _placeholder;
};

#endif
