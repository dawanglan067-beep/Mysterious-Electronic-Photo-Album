#ifndef ALBUMLOADCONTROLLER_H
#define ALBUMLOADCONTROLLER_H

#include <QObject>
#include <QStringList>

class QTreeWidget;
class QTreeWidgetItem;

// Owns task scheduling and modal progress, never owns committed album nodes.
class AlbumLoadController : public QObject
{
    Q_OBJECT
public:
    explicit AlbumLoadController(QTreeWidget *tree);
    bool IsBusy() const { return _processing; }
    void Open(const QString &path);
    bool Import(const QString &source, QTreeWidgetItem *album, QString *error = nullptr);

signals:
    void AlbumOpened(QTreeWidgetItem *root);
    void ImportCompleted();

private:
    void DrainOpenQueue();
    QTreeWidget *_tree;
    bool _processing = false;
    QStringList _pendingOpenPaths;
};

#endif
