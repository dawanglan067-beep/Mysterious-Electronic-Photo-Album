#include "audiofileprobe.h"
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
QString AudioFileProbe::SniffAudioFileFormat(const QString &filePath, QString *outDiagReason)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
    {
        if (outDiagReason) *outDiagReason = QCoreApplication::translate("ProTreeWidget", "音频文件未找到");
        return QString();
    }
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (outDiagReason) *outDiagReason = QCoreApplication::translate("ProTreeWidget", "无法读取文件（可能被其他程序独占或无访问权限）");
        return QString();
    }
    QByteArray header = f.read(32);
    f.close();

    if (header.size() < 4)
    {
        if (outDiagReason) *outDiagReason = QCoreApplication::translate("ProTreeWidget", "文件数据为空或已损坏 (<4 字节)");
        return QString();
    }

    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    // 1. 深度检测 MP4 容器 (包含 ftyp 标识)
    if (header.size() >= 8 && header.mid(4, 4) == "ftyp")
    {
        if (ext == "mp3" || ext == "wav" || ext == "flac")
        {
            if (outDiagReason)
            {
                *outDiagReason = QCoreApplication::translate("ProTreeWidget", "⚠️ 严重格式冲突：该文件实质为 MP4 容器（录屏/视频），却被强行命名为了 .%1！系统原生解码器判定文件头非法而拒绝播放。建议改回 .mp4 真实扩展名，或提供真正的纯音频文件。").arg(ext);
            }
        }
        else if (outDiagReason)
        {
            *outDiagReason = QCoreApplication::translate("ProTreeWidget", "MP4 格式媒体（纯音频提取播放模式）");
        }
        return "mp4";
    }

    // 2. 检测标准 RIFF WAVE
    if (header.size() >= 12 && header.startsWith("RIFF") && header.mid(8, 4) == "WAVE")
    {
        if (outDiagReason) *outDiagReason = QCoreApplication::translate("ProTreeWidget", "标准 WAVE (PCM) 无损/纯音频");
        return "wav";
    }

    // 3. 检测标准 FLAC
    if (header.startsWith("fLaC"))
    {
        if (outDiagReason) *outDiagReason = QCoreApplication::translate("ProTreeWidget", "标准 FLAC 无损音频");
        return "flac";
    }

    // 4. 检测 MP3 标志 (ID3v2 标签 或 MPEG-1 Audio Sync Word)
    if (header.startsWith("ID3"))
    {
        if (outDiagReason) *outDiagReason = QCoreApplication::translate("ProTreeWidget", "标准 MP3 音频 (ID3 标签)");
        return "mp3";
    }
    const unsigned char *u = reinterpret_cast<const unsigned char*>(header.constData());
    if ((u[0] == 0xFF) && ((u[1] & 0xE0) == 0xE0))
    {
        if (outDiagReason) *outDiagReason = QCoreApplication::translate("ProTreeWidget", "标准 MP3 音频 (MPEG 帧同步头)");
        return "mp3";
    }

    // 5. 检测 Ogg
    if (header.startsWith("OggS"))
    {
        if (outDiagReason) *outDiagReason = QCoreApplication::translate("ProTreeWidget", "标准 OGG 音频");
        return "ogg";
    }

    if (outDiagReason)
    {
        *outDiagReason = QCoreApplication::translate("ProTreeWidget", "通用音频编码格式 (.%1)").arg(ext);
    }
    return ext;
}

bool AudioFileProbe::IsAudioFileValid(const QString &filePath, QString *outDiagReason)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
    {
        if (outDiagReason) *outDiagReason = QCoreApplication::translate("ProTreeWidget", "音频文件不存在");
        return false;
    }
    QFileInfo fi(filePath);
    if (fi.size() < 1024)
    {
        if (outDiagReason) *outDiagReason = QCoreApplication::translate("ProTreeWidget", "音频文件损坏或大小异常 (<1KB)");
        return false;
    }

    QString sniffReason;
    QString realFormat = SniffAudioFileFormat(filePath, &sniffReason);
    QString ext = fi.suffix().toLower();
    if (realFormat == "mp4" && (ext == "mp3" || ext == "wav" || ext == "flac"))
    {
        if (outDiagReason) *outDiagReason = sniffReason;
        return false;
    }
    if (outDiagReason) *outDiagReason = sniffReason;
    return true;
}
