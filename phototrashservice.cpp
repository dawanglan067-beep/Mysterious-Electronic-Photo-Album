#include "phototrashservice.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>

namespace {
PhotoTrashService::Result recycle(const QString &path, const QString &albumRoot, bool folder)
{
    const QFileInfo file(path), root(albumRoot);
    if (path.isEmpty() || albumRoot.isEmpty() || !root.isDir() || (folder ? !file.isDir() : !file.isFile()))
        return {false, QStringLiteral("目标图片、文件夹或相册目录不存在。"), {}};
    if (file.isSymLink() || file.isShortcut())
        return {false, QStringLiteral("不能通过符号链接或快捷方式删除图片，请操作相册内的实际文件。"), {}};
#ifdef Q_OS_WIN
    const auto sensitivity = Qt::CaseInsensitive;
#else
    const auto sensitivity = Qt::CaseSensitive;
#endif
    const auto inside = [sensitivity](const QString &target, const QString &directory) {
        if (target.isEmpty() || directory.isEmpty()) return false;
        return target.startsWith(directory.endsWith('/') ? directory : directory + '/', sensitivity);
    };
    if (!inside(QDir::cleanPath(file.absoluteFilePath()), QDir::cleanPath(root.absoluteFilePath()))
        || !inside(file.canonicalFilePath(), root.canonicalFilePath()))
        return {false, QStringLiteral("删除目标不属于当前相册，或路径指向相册外文件。"), {}};
    if (folder) {
        // 不沿链接遍历目录；含别名的目录也要求用户先单独处理。
        QDirIterator entries(file.absoluteFilePath(), QDir::AllEntries | QDir::NoDotAndDotDot
            | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (entries.hasNext()) {
            entries.next();
            const auto entry = entries.fileInfo();
            if (entry.isSymLink() || entry.isShortcut()
                || !inside(entry.canonicalFilePath(), root.canonicalFilePath()))
                return {false, QStringLiteral("文件夹中包含链接或无法验证归属的项目，请先单独处理。"), {}};
        }
    }
    QString trashPath;
    if (!QFile::moveToTrash(file.absoluteFilePath(), &trashPath))
        return {false, QStringLiteral("无法移入回收站，图片和相册记录已保留；不会执行永久删除。"), {}};
    return {true, {}, trashPath};
}
}

PhotoTrashService::Result PhotoTrashService::moveToTrash(const QString &path, const QString &albumRoot)
{
    return recycle(path, albumRoot, false);
}

PhotoTrashService::Result PhotoTrashService::moveFolderToTrash(const QString &path, const QString &albumRoot)
{
    return recycle(path, albumRoot, true);
}
