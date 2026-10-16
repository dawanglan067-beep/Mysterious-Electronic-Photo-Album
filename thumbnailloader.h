#ifndef THUMBNAILLOADER_H
#define THUMBNAILLOADER_H

#include <QObject>
#include <QImage>
#include <QSet>
#include <QThreadPool>
#include <atomic>
#include <memory>

class ThumbnailLoader : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailLoader(QObject *parent = nullptr);
    ~ThumbnailLoader() override;
    void Reset();
    void Request(const QString &path);
signals:
    void SigReady(const QString &path, const QImage &image);
private:
    QThreadPool _pool;
    QSet<QString> _pending;
    std::shared_ptr<std::atomic<quint64>> _generation;
};

#endif
