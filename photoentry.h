#ifndef PHOTOENTRY_H
#define PHOTOENTRY_H

#include <QList>
#include <QString>

struct PhotoEntry
{
    QString path;
    QString caption;
};

struct AlbumExportOptions
{
    QString title;
    QString outputPath;
    QList<PhotoEntry> photos;
    int photosPerPage = 2;
};

#endif
