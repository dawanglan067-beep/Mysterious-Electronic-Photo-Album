#pragma once
#include <QString>

namespace PhotoTrashService {
struct Result {
    bool success = false;
    QString error;
    QString trashPath;
};
// 只处理相册内实际文件；失败不回退为永久删除。
Result moveToTrash(const QString &path, const QString &albumRoot);
Result moveFolderToTrash(const QString &path, const QString &albumRoot);
}
