#include "thumbnailloader.h"
#include <QImageReader>
#include <QRunnable>

ThumbnailLoader::ThumbnailLoader(QObject *parent)
    : QObject(parent), _generation(std::make_shared<std::atomic<quint64>>(0))
{
    _pool.setMaxThreadCount(2);
}

ThumbnailLoader::~ThumbnailLoader()
{
    Reset();
    _pool.waitForDone();
}

void ThumbnailLoader::Reset()
{
    ++(*_generation);
    _pool.clear();
    _pending.clear();
}

void ThumbnailLoader::Request(const QString &path)
{
    if (_pending.contains(path)) return;
    _pending.insert(path);
    const auto generation = _generation;
    const quint64 batch = generation->load();
    _pool.start(QRunnable::create([this, path, generation, batch]() {
        if (generation->load() != batch) return;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        if (size.isValid()) reader.setScaledSize(size.scaled(320, 320, Qt::KeepAspectRatio));
        const QImage image = reader.read();
        if (generation->load() != batch) return;
        QMetaObject::invokeMethod(this, [this, path, image, generation, batch]() {
            if (generation->load() != batch) return;
            _pending.remove(path);
            emit SigReady(path, image);
        }, Qt::QueuedConnection);
    }));
}
