#pragma once
#include <QString>

namespace MaterialImportService {
struct Result {
    bool success = false;
    QString albumPath;
    QString error;
};
// 只准备磁盘文件，不依赖窗口、树节点或编辑文档。调用方负责相册是否已打开。
Result prepare(const QString &savedPath, const QString &albumRoot);
}
