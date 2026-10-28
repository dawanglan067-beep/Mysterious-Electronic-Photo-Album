#include "imageexportservice.h"
#include "photoeditdocument.h"
#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QColorSpace>

namespace ImageExportService {
QStringList supportedExtensions()
{
    const auto formats = QImageWriter::supportedImageFormats();
    QStringList extensions;
    if (formats.contains("png")) extensions << "png";
    if (formats.contains("jpeg") || formats.contains("jpg")) extensions << "jpg" << "jpeg";
    if (formats.contains("bmp")) extensions << "bmp";
    return extensions;
}

Result writeImage(const QImage &image, const QString &path,
                  const QStringList &sourcePaths, const Options &options)
{
    if (image.isNull()) return {false, QStringLiteral("没有可导出的图像。")};
    if (path.trimmed().isEmpty()) return {false, QStringLiteral("请选择输出路径。")};
    const QFileInfo target(path);
    for (const auto &sourcePath : sourcePaths) {
        if (sourcePath.isEmpty()) continue;
        const QFileInfo source(sourcePath);
#ifdef Q_OS_WIN
        const auto sensitivity = Qt::CaseInsensitive;
#else
        const auto sensitivity = Qt::CaseSensitive;
#endif
        if (QDir::cleanPath(source.absoluteFilePath()).compare(
                QDir::cleanPath(target.absoluteFilePath()), sensitivity) == 0
            || (!source.canonicalFilePath().isEmpty() && source.canonicalFilePath().compare(
                    target.canonicalFilePath(), sensitivity) == 0))
            return {false, QStringLiteral("输出文件不能覆盖源照片，请另选文件名。")};
    }
    const QString extension = target.suffix().toLower();
    if (!supportedExtensions().contains(extension))
        return {false, QStringLiteral("不支持此输出扩展名，请使用 PNG、JPEG 或 BMP。")};
    const bool jpeg = extension == "jpg" || extension == "jpeg";
    if (jpeg && (options.jpegQuality < 0 || options.jpegQuality > 100
                 || !options.jpegBackground.isValid() || options.jpegBackground.alpha() != 255))
        return {false, QStringLiteral("JPEG 质量必须为 0–100，背景色必须不透明。")};
    QImage output = image;
    if (output.colorSpace().isValid()) output.convertToColorSpace(QColorSpace::SRgb);
    else output.setColorSpace(QColorSpace::SRgb);
    if (jpeg) {
        const QImage normalized = output;
        output = QImage(image.size(), QImage::Format_RGB32);
        if (output.isNull()) return {false, QStringLiteral("无法分配导出图像内存。")};
        output.fill(options.jpegBackground);
        QPainter painter(&output);
        painter.drawImage(0, 0, normalized);
        painter.end();
        output.setColorSpace(QColorSpace::SRgb);
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return {false, file.errorString()};
    {
        QImageWriter writer(&file, jpeg ? QByteArray("jpeg") : extension.toLatin1());
        if (jpeg) writer.setQuality(options.jpegQuality);
        if (!writer.write(output)) {
            file.cancelWriting();
            return {false, writer.errorString()};
        }
    }
    if (!file.commit()) return {false, file.errorString()};
    return {true, {}};
}

Result exportDocument(const PhotoEditDocument &document, const QString &path, const Options &options)
{
    // 不读取界面的降采样预览缓存，始终按当前编辑状态生成完整像素。
    return writeImage(document.Render(), path, {document.sourcepath()}, options);
}
}
