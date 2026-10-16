#ifndef PHOTOWALLWIDGET_H
#define PHOTOWALLWIDGET_H

#include <QWidget>
#include "photoentry.h"
class QListView;
class QLabel;
class QPushButton;
class PhotoWallModel;

class PhotoWallWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PhotoWallWidget(QWidget *parent = nullptr);
    void SetPhotos(const QList<PhotoEntry> &photos);
    QStringList SelectedPaths() const;
signals:
    void SigOpenPhoto(const QString &path);
    void SigExportRequested();
private:
    void UpdateSelectionLabel();
    PhotoWallModel *_model;
    QListView *_view;
    QLabel *_count;
    QPushButton *_export;
};

#endif
