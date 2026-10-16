#include "workspacesettings.h"
#include <QSettings>
QVariantMap WorkspaceSettings::Load(const QString &organization)
{
    QSettings settings(organization, QStringLiteral("AlbumApp"));
    QVariantMap values;
    for (const QString &key : settings.allKeys()) values.insert(key, settings.value(key));
    return values;
}
void WorkspaceSettings::Save(const QVariantMap &values, const QString &organization)
{
    QSettings settings(organization, QStringLiteral("AlbumApp"));
    for (auto it = values.cbegin(); it != values.cend(); ++it) settings.setValue(it.key(), it.value());
}
