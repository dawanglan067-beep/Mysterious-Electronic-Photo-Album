#pragma once
#include <QVariantMap>
#include <QString>
namespace WorkspaceSettings {
QVariantMap Load(const QString &organization);
void Save(const QVariantMap &values, const QString &organization);
}
