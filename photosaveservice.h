#pragma once
#include "imageexportservice.h"
#include <QByteArray>

class PhotoEditDocument;

namespace PhotoSaveService {
struct Options {
    QString backupRoot; // Empty selects AppDataLocation/backups.
    QStringList albumRoots; // Backups must never be inside a scanned album.
    ImageExportService::Options encoding;
};
struct Result {
    bool success = false;
    bool committed = false;
    QString error;
    QString backupPath;
};
QString backupRoot();
QStringList backups(const QString &sourcePath, const QString &root = {});
Result overwrite(PhotoEditDocument &document, const Options &options = {});
// Restore exact original bytes to a new file, never silently replace a file.
ImageExportService::Result restoreCopy(const QString &backupPath, const QString &destination);
}
