#pragma once
#include <QString>
namespace AudioFileProbe {
QString SniffAudioFileFormat(const QString &filePath, QString *outDiagReason = nullptr);
bool IsAudioFileValid(const QString &filePath, QString *outDiagReason = nullptr);
}
