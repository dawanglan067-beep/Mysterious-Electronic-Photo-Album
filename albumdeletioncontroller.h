#pragma once
#include <QObject>
#include <QStringList>

class QWidget;
class ProTreeWidget;
class PicShow;

// 协调确认、编辑保护与删除结果。只使用树和编辑器的公开接口，不拥有节点或编辑状态。
class AlbumDeletionController : public QObject
{
    Q_OBJECT
public:
    AlbumDeletionController(ProTreeWidget *tree, PicShow *editor, QWidget *window);
public slots:
    void DeletePhotos(const QStringList &paths);
    void DeleteFolder(const QString &path);
private:
    void DeletePaths(QStringList paths, const QString &folder);
    ProTreeWidget *_tree;
    PicShow *_editor;
    QWidget *_window;
};
