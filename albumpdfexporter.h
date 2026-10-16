#ifndef ALBUMPDFEXPORTER_H
#define ALBUMPDFEXPORTER_H

#include <QThread>
#include <atomic>
#include "photoentry.h"

class AlbumPdfExporter : public QThread
{
    Q_OBJECT
public:
    explicit AlbumPdfExporter(const AlbumExportOptions &options, QObject *parent = nullptr);
    ~AlbumPdfExporter() override;
    void Cancel() { _cancelled.store(true); }
signals:
    void SigProgress(int completed, int total);
    void SigResult(bool success, bool cancelled, const QString &message);
protected:
    void run() override;
private:
    AlbumExportOptions _options;
    std::atomic_bool _cancelled{false};
};

#endif
