#pragma once
#include <QColor>
#include <QImage>
#include <QStringList>

class PhotoEditDocument;

namespace ImageExportService {
struct Options {
    int jpegQuality = 95;
    QColor jpegBackground = Qt::white;
};
struct Result {
    bool success = false;
    QString error;
};
QStringList supportedExtensions();
// 所有源路径都必须传入；未来多图合成可传入多张源图。
Result writeImage(const QImage &image, const QString &path,
                  const QStringList &sourcePaths, const Options &options = {});
Result exportDocument(const PhotoEditDocument &document, const QString &path,
                      const Options &options = {});
}
