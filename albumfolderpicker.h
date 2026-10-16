#ifndef ALBUMFOLDERPICKER_H
#define ALBUMFOLDERPICKER_H

#include <QString>
class QWidget;

struct AlbumFolderSelection
{
    QString path;
    QString error;
};

// An empty path with no error means the user cancelled.
AlbumFolderSelection SelectAlbumFolder(QWidget *parent, const QString &title,
                                      const QString &initialDirectory);

#endif
