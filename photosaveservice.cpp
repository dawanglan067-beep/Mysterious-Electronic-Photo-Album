#include "photosaveservice.h"
#include "photoeditdocument.h"
#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {
QString Canonical(const QString &path)
{
    QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty()) return QDir::cleanPath(canonical);
    // Also resolve the existing ancestor of a not-yet-created backup directory.
    const QString parent = info.absolutePath();
    if (parent == info.absoluteFilePath()) return QDir::cleanPath(parent);
    return QDir(Canonical(parent)).filePath(info.fileName());
}
QString Identity(const QString &path)
{
    QString key = Canonical(path);
#ifdef Q_OS_WIN
    key = key.toLower();
#endif
    return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QByteArray Hash(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
bool Read(const QString &path, QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    bytes = file.readAll();
    return file.error() == QFileDevice::NoError;
}
bool Inside(const QString &path, const QString &root)
{
#ifdef Q_OS_WIN
    const auto cs = Qt::CaseInsensitive;
#else
    const auto cs = Qt::CaseSensitive;
#endif
    const QString parent = Canonical(root);
    const QString child = Canonical(path);
    return child.compare(parent, cs) == 0
        || child.startsWith(parent.endsWith('/') ? parent : parent + '/', cs);
}
bool ReadBackup(const QString &path, QByteArray &bytes, QString &original)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024LL * 1024 * 1024) return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_5);
    QByteArray magic(8, '\0');
    if (stream.readRawData(magic.data(), 8) != 8 || magic != "ALBUMB01") return false;
    quint32 jsonLength = 0;
    stream >> jsonLength;
    if (jsonLength > 64 * 1024 || jsonLength > file.bytesAvailable()) return false;
    QByteArray json(jsonLength, '\0');
    if (stream.readRawData(json.data(), json.size()) != json.size()) return false;
    const auto metadata = QJsonDocument::fromJson(json).object();
    bytes = file.readAll();
    original = metadata.value("source").toString();
    return file.error() == QFileDevice::NoError && !original.isEmpty()
        && metadata.value("sha256").toString().toLatin1() == Hash(bytes).toHex()
        && !QImage::fromData(bytes).isNull();
}
}

namespace PhotoSaveService {
QString backupRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/backups";
}

QStringList backups(const QString &sourcePath, const QString &root)
{
    QDir dir(QDir(root.isEmpty() ? backupRoot() : root).filePath(Identity(sourcePath)));
    QStringList result;
    for (const auto &name : dir.entryList({"*.albumbackup"}, QDir::Files, QDir::Name | QDir::Reversed))
        result.append(dir.filePath(name));
    return result;
}

Result overwrite(PhotoEditDocument &document, const Options &options)
{
    Result result;
    const auto fail = [&](const QString &message) { result.error = message; return result; };
    const QString source = document.sourcepath();
    if (source.isEmpty() || document.sourceImage().isNull() || document.sourceHash().isEmpty())
        return fail(QObject::tr("没有可覆盖的照片，请重新加载。"));
    const QFileInfo info(source);
    if (info.isSymLink()) return fail(QObject::tr("符号链接照片请另存副本。"));
    if (info.size() > 512LL * 1024 * 1024)
        return fail(QObject::tr("源文件超过 512 MiB，暂不支持覆盖，请另存副本。"));
    const QString extension = info.suffix().toLower();
    if (!ImageExportService::supportedExtensions().contains(extension))
        return fail(QObject::tr("覆盖仅支持 PNG、JPEG、BMP，请另存副本。"));
    QByteArray before;
    if (!Read(source, before) || Hash(before) != document.sourceHash())
        return fail(QObject::tr("源照片已被外部修改或无法读取，请重新加载或另存副本。"));
    const QByteArray format = QImageReader::imageFormat(source).toLower();
    const QByteArray expected = extension == "jpg" || extension == "jpeg" ? QByteArray("jpeg") : extension.toLatin1();
    if (format != expected)
        return fail(QObject::tr("扩展名与实际图像格式不一致，请另存副本。"));
    const QImage original = QImage::fromData(before);
    if (original.depth() > 32 || original.format() == QImage::Format_CMYK8888)
        return fail(QObject::tr("高位深或 CMYK 源图暂不支持覆盖，请另存副本。"));
    QImage rendered = document.Render();
    if (rendered.isNull()) return fail(QObject::tr("完整尺寸渲染失败。"));
    const bool jpeg = expected == "jpeg";
    if (jpeg) {
        if (options.encoding.jpegQuality < 0 || options.encoding.jpegQuality > 100
            || !options.encoding.jpegBackground.isValid() || options.encoding.jpegBackground.alpha() != 255)
            return fail(QObject::tr("JPEG 质量或背景设置无效。"));
        QImage flat(rendered.size(), QImage::Format_RGB32);
        if (flat.isNull()) return fail(QObject::tr("图像内存分配失败。"));
        flat.fill(options.encoding.jpegBackground);
        QPainter painter(&flat);
        painter.drawImage(0, 0, rendered);
        painter.end();
        flat.setColorSpace(rendered.colorSpace());
        rendered = flat;
    }
    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, expected);
    if (jpeg) writer.setQuality(options.encoding.jpegQuality);
    if (!writer.write(rendered)) return fail(writer.errorString());
    const QImage encodedImage = QImage::fromData(encoded);
    if (encodedImage.isNull() || encodedImage.size() != rendered.size())
        return fail(QObject::tr("编码结果验证失败，源文件未改动。"));

    const QString root = options.backupRoot.isEmpty() ? backupRoot() : options.backupRoot;
    for (const auto &album : options.albumRoots)
        if (!album.isEmpty() && Inside(root, album))
            return fail(QObject::tr("备份目录位于受扫描相册中，请使用相册外的备份目录。"));
    const QString folder = QDir(root).filePath(Identity(source));
    if (!QDir().mkpath(folder)) return fail(QObject::tr("无法创建备份目录，源文件未改动。"));
    const QString backup = QDir(folder).filePath(
        QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz") + "-"
        + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".albumbackup");
    QJsonObject metadata{{"source", Canonical(source)}, {"format", QString::fromLatin1(format)},
                         {"sha256", QString::fromLatin1(Hash(before).toHex())},
                         {"createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    const QByteArray json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
    // A backup has a unique, new-only destination and is verified before source
    // replacement. Avoid rename-based commits across redirected AppData volumes.
    QFile backupFile(backup);
    if (!backupFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail(backupFile.errorString());
    QDataStream stream(&backupFile);
    stream.setVersion(QDataStream::Qt_6_5);
    stream.writeRawData("ALBUMB01", 8);
    stream << quint32(json.size());
    stream.writeRawData(json.constData(), json.size());
    const bool backupWritten = stream.status() == QDataStream::Ok
        && backupFile.write(before) == before.size() && backupFile.flush();
    backupFile.close();
    if (!backupWritten) {
        backupFile.remove();
        return fail(QObject::tr("备份写入失败，源文件未改动。"));
    }
    result.backupPath = backup;
    QByteArray verified;
    QString originalPath;
    if (!ReadBackup(backup, verified, originalPath) || verified != before)
        return fail(QObject::tr("备份校验失败，源文件未改动。"));

    QSaveFile target(source);
    target.setDirectWriteFallback(false);
    if (!target.open(QIODevice::WriteOnly)) return fail(target.errorString());
    if (target.write(encoded) != encoded.size()) return fail(target.errorString());
    QByteArray current;
    if (!Read(source, current) || Hash(current) != document.sourceHash())
        return fail(QObject::tr("保存期间源照片发生变化，已停止覆盖；请重新加载或另存。"));
    if (!target.commit()) return fail(target.errorString());
    result.committed = true;

    PhotoEditDocument reloaded;
    if (!reloaded.load(source) || reloaded.sourceHash() != Hash(encoded)
        || reloaded.sourceImage().size() != encodedImage.size())
        return fail(QObject::tr("磁盘文件已替换，但读取验证失败。内存编辑和备份仍保留，请另存或恢复备份。"));
    document = reloaded;
    result.success = true;
    return result;
}

ImageExportService::Result restoreCopy(const QString &backupPath, const QString &destination)
{
    QByteArray bytes;
    QString original;
    if (!ReadBackup(backupPath, bytes, original))
        return {false, QObject::tr("备份损坏、版本不支持或无法读取。")};
    const QString targetExtension = QFileInfo(destination).suffix().toLower();
    const QString originalExtension = QFileInfo(original).suffix().toLower();
    const bool jpeg = (targetExtension == "jpg" || targetExtension == "jpeg")
        && (originalExtension == "jpg" || originalExtension == "jpeg");
    if (destination.trimmed().isEmpty() || QFileInfo::exists(destination)
        || (!jpeg && targetExtension != originalExtension))
        return {false, QObject::tr("请选择尚不存在且与原图扩展名一致的新文件。")};
    // NewOnly prevents a file created after the dialog from being overwritten.
    QFile file(destination);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return {false, file.errorString()};
    const bool ok = file.write(bytes) == bytes.size() && file.flush();
    file.close();
    if (!ok) { file.remove(); return {false, QObject::tr("恢复副本写入失败。")}; }
    return {true, {}};
}
}
