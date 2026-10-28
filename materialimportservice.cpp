#include "materialimportservice.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>

namespace {
QString message(const char *text) { return QCoreApplication::translate("ProTreeWidget", text); }
bool inside(const QString &path, const QString &root)
{
#ifdef Q_OS_WIN
    const auto sensitivity = Qt::CaseInsensitive;
#else
    const auto sensitivity = Qt::CaseSensitive;
#endif
    return !path.isEmpty() && !root.isEmpty()
        && path.startsWith(root.endsWith('/') ? root : root + '/', sensitivity);
}
QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? hash.result() : QByteArray();
}
}

MaterialImportService::Result MaterialImportService::prepare(const QString &savedPath, const QString &albumRoot)
{
    const QFileInfo root(albumRoot), source(savedPath);
    if (albumRoot.isEmpty() || !root.isDir())
        return {false, {}, message("目标相册目录不存在。")};
    QImageReader reader(savedPath);
    if (!source.isFile() || !reader.canRead())
        return {false, {}, message("保存的素材不存在或不是可读取的图片。")};
    const QString canonicalRoot = root.canonicalFilePath();
    if (inside(source.canonicalFilePath(), canonicalRoot))
        return {true, source.canonicalFilePath(), {}};

    const QString folder = QDir(root.absoluteFilePath()).filePath(message("素材"));
    if (!QDir().mkpath(folder) || !inside(QFileInfo(folder).canonicalFilePath(), canonicalRoot))
        return {false, {}, message("无法在当前相册内创建素材目录。")};
    const QByteArray sourceHash = fileHash(savedPath);
    if (sourceHash.isEmpty()) return {false, {}, message("无法读取素材文件。")};
    for (int number = 0; ; ++number) {
        const QString name = source.completeBaseName()
            + (number ? QString("_%1").arg(number, 3, 10, QLatin1Char('0')) : QString())
            + "." + source.suffix();
        const QString candidate = QDir(folder).filePath(name);
        const QFileInfo existing(candidate);
        // 断开的链接也占用文件名，不能尝试覆盖。
        if (existing.exists() || existing.isSymLink() || existing.isShortcut()) {
            if (!existing.isSymLink() && !existing.isShortcut() && existing.isFile()
                && inside(existing.canonicalFilePath(), canonicalRoot) && fileHash(candidate) == sourceHash)
                return {true, existing.canonicalFilePath(), {}};
            continue;
        }
        if (!QFile::copy(savedPath, candidate))
            return {false, {}, message("素材已保存，但无法复制到相册目录。")};
        return {true, QFileInfo(candidate).canonicalFilePath(), {}};
    }
}
