#pragma once
#include <QString>
class QWidget;

namespace MaterialFilePicker {
struct Result {
    QString path;
    QString error;
};
Result choose(const QString &sourcePath, const QString &initialPath, QWidget *parent);
}
