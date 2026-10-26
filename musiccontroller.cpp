#include "musiccontroller.h"
#include "musicsettingsdialog.h"
#include <QFileDialog>
#include <QMessageBox>
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QUrl>
#include <QWidget>
#include <QCoreApplication>

static const wchar_t *kMciAlias = L"album_bgm_channel";
MusicController::MusicController(QWidget *owner) : QObject(owner), _window(owner), _music_volume(80), _play_immediately(true), _auto_play_on_slideshow(true), _auto_stop_on_slideshow_exit(true), _is_native_playing(false), _is_native_paused(false)
{
    _player = new QMediaPlayer(this);
    _audio_output = new QAudioOutput(this);
    _audio_output->setMuted(false);
    _audio_output->setVolume(_music_volume / 100.0f);
    _player->setAudioOutput(_audio_output);

    // 监听播放状态流转（确保跨音频工作线程安全排队到 GUI 主线程）
    connect(_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState) _last_music_error.clear();
        emit SigMusicStateChanged(state == QMediaPlayer::PlayingState);
    }, Qt::QueuedConnection);

    // 核心容错与可观测性：监听媒体底层错误，消灭静默失败（线程安全排队）
    connect(_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &errorString) {
        QString errDetail;
        switch (error)
        {
        case QMediaPlayer::FormatError:
            errDetail = tr("音频格式不支持或解码失败（若文件为视频或扩展名与实际格式不符，底层系统解码器将拒绝播放）: %1").arg(errorString);
            break;
        case QMediaPlayer::ResourceError:
            errDetail = tr("无法打开音频资源或文件已被删除/占用: %1").arg(errorString);
            break;
        case QMediaPlayer::NetworkError:
            errDetail = tr("网络音频传输错误: %1").arg(errorString);
            break;
        case QMediaPlayer::AccessDeniedError:
            errDetail = tr("访问权限不足，无法读取音频文件: %1").arg(errorString);
            break;
        default:
            errDetail = tr("音频播放出错 (错误码 %1): %2").arg(QString::number(static_cast<int>(error)), errorString);
            break;
        }
        _last_music_error = errDetail;
        qWarning() << "[MusicPlayer] Error occurred:" << error << errorString << "Detail:" << errDetail;
        emit SigMusicErrorOccurred(errDetail);
        emit SigMusicStateChanged(false);
    }, Qt::QueuedConnection);

    // 监听媒体加载状态（线程安全排队）
    connect(_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia)
        {
            _last_music_error.clear();
        }
        else if (status == QMediaPlayer::InvalidMedia)
        {
            if (_last_music_error.isEmpty())
            {
                _last_music_error = tr("无效的媒体源（文件损坏或格式非法）");
            }
            qWarning() << "[MusicPlayer] MediaStatus: InvalidMedia for path:" << _music_path;
        }
    }, Qt::QueuedConnection);

}
MusicController::~MusicController() { NativeMciClose(); }
static QString FindDefaultMusicDir()
{
    QStringList baseDirs = { QDir::currentPath(), QCoreApplication::applicationDirPath() };
    for (const QString &base : baseDirs)
    {
        QDir dir(base);
        for (int depth = 0; depth < 4; ++depth)
        {
            if (dir.exists("music"))
            {
                QDir musicDir(dir.filePath("music"));
                return musicDir.absolutePath();
            }
            if (!dir.cdUp()) break;
        }
    }
    return QDir::currentPath();
}

QString MusicController::FindDefaultMusicFile()
{
    QString musicDirPath = FindDefaultMusicDir();
    QDir musicDir(musicDirPath);

    // 优先采用用户原始高保真音轨
    QString preferredBandicam = musicDir.filePath("bandicam_original.wav");
    if (QFileInfo::exists(preferredBandicam) && MusicController::IsAudioFileValid(preferredBandicam))
    {
        return preferredBandicam;
    }

    // 扫描匹配可用合法音频
    QStringList filters = { "*.wav", "*.mp3", "*.flac", "*.m4a", "*.aac", "*.ogg", "*.wma" };
    QFileInfoList files = musicDir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files)
    {
        QString reason;
        if (MusicController::IsAudioFileValid(fi.absoluteFilePath(), &reason))
        {
            return fi.absoluteFilePath();
        }
        else
        {
            qWarning() << "[MusicPlayer] FindDefaultMusicFile skipped invalid file:" << fi.fileName() << "Reason:" << reason;
        }
    }

    // 系统兜底高品质铃声
    QString winMediaDir = "C:/Windows/Media";
    if (QDir(winMediaDir).exists())
    {
        QStringList sysPreferred = { "Ring08.wav", "Ring05.wav", "Ring09.wav", "Ring03.wav", "Alarm01.wav" };
        for (const QString &sysFile : sysPreferred)
        {
            QString fullSys = winMediaDir + "/" + sysFile;
            if (QFileInfo::exists(fullSys))
            {
                return fullSys;
            }
        }
    }

    return QString();
}

void MusicController::EnsureMusicSourceLoaded()
{
    QString diagReason;
    if (_music_path.isEmpty() || !QFileInfo::exists(_music_path) || !IsAudioFileValid(_music_path, &diagReason))
    {
        if (!_music_path.isEmpty())
        {
            qWarning() << "[MusicPlayer] Current music path invalid:" << _music_path << "Reason:" << diagReason;
            _last_music_error = diagReason;
        }
        _music_path = FindDefaultMusicFile();
        emit SettingsDirty();
    }

    if (!_music_path.isEmpty() && QFileInfo::exists(_music_path))
    {
        QUrl expectedUrl = QUrl::fromLocalFile(_music_path);
        if (_player->source() != expectedUrl)
        {
            _player->setSource(expectedUrl);
            _player->setLoops(QMediaPlayer::Infinite);
        }
    }
    if (_audio_output)
    {
        _audio_output->setMuted(false);
        _audio_output->setVolume(qBound(0, _music_volume, 100) / 100.0f);
    }
}

void MusicController::NativeMciClose()
{
    mciSendStringW(QString("stop %1").arg(QString::fromWCharArray(kMciAlias)).toStdWString().c_str(), NULL, 0, NULL);
    mciSendStringW(QString("close %1").arg(QString::fromWCharArray(kMciAlias)).toStdWString().c_str(), NULL, 0, NULL);
    _is_native_playing = false;
    _is_native_paused = false;
}

bool MusicController::NativeMciPlay(const QString &filePath)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
    {
        _last_music_error = tr("音频文件未找到");
        return false;
    }

    // 若当前正在暂停同一首曲目，执行恢复播放
    if (_is_native_paused)
    {
        MCIERROR errResume = mciSendStringW(QString("resume %1").arg(QString::fromWCharArray(kMciAlias)).toStdWString().c_str(), NULL, 0, NULL);
        if (errResume == 0)
        {
            _is_native_playing = true;
            _is_native_paused = false;
            NativeMciSetVolume(_music_volume);
            emit SigMusicStateChanged(true);
            return true;
        }
    }

    // 重新打开新音轨：先彻底清理旧连接
    NativeMciClose();

    // 转换为 Windows 原生规范绝对路径
    QString nativePath = QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath());
    std::wstring wPath = nativePath.toStdWString();

    // 优先采用 mpegvideo 类型打开（全兼容 MP3 / WAV / WMA / AAC）
    std::wstring cmdOpen = L"open \"" + wPath + L"\" type mpegvideo alias " + kMciAlias;
    MCIERROR err = mciSendStringW(cmdOpen.c_str(), NULL, 0, NULL);

    // 若类型指定失败，回退尝试由 Windows MCI 依据注册表文件扩展名自动协商驱动
    if (err != 0)
    {
        std::wstring cmdFallback = L"open \"" + wPath + L"\" alias " + kMciAlias;
        err = mciSendStringW(cmdFallback.c_str(), NULL, 0, NULL);
    }

    if (err != 0)
    {
        wchar_t errBuf[256] = {0};
        mciGetErrorStringW(err, errBuf, 256);
        _last_music_error = tr("Windows 原生音频引擎打开失败: %1").arg(QString::fromWCharArray(errBuf));
        qDebug() << "[NativeAudio]" << _last_music_error;
        return false;
    }

    // 设置音量 (MCI 范围 0 ~ 1000)
    NativeMciSetVolume(_music_volume);

    // 循环播放
    std::wstring cmdPlay = L"play " + std::wstring(kMciAlias) + L" repeat";
    err = mciSendStringW(cmdPlay.c_str(), NULL, 0, NULL);
    if (err != 0)
    {
        cmdPlay = L"play " + std::wstring(kMciAlias);
        err = mciSendStringW(cmdPlay.c_str(), NULL, 0, NULL);
    }

    if (err != 0)
    {
        wchar_t errBuf[256] = {0};
        mciGetErrorStringW(err, errBuf, 256);
        _last_music_error = tr("Windows 原生音频引擎播放失败: %1").arg(QString::fromWCharArray(errBuf));
        qDebug() << "[NativeAudio]" << _last_music_error;
        return false;
    }

    _is_native_playing = true;
    _is_native_paused = false;
    _last_music_error.clear();
    emit SigMusicStateChanged(true);
    return true;
}

void MusicController::NativeMciPause()
{
    if (_is_native_playing && !_is_native_paused)
    {
        std::wstring cmd = L"pause " + std::wstring(kMciAlias);
        mciSendStringW(cmd.c_str(), NULL, 0, NULL);
        _is_native_playing = false;
        _is_native_paused = true;
        emit SigMusicStateChanged(false);
    }
}

void MusicController::NativeMciStop()
{
    if (_is_native_playing || _is_native_paused)
    {
        std::wstring cmd = L"stop " + std::wstring(kMciAlias);
        mciSendStringW(cmd.c_str(), NULL, 0, NULL);
        std::wstring cmdSeek = L"seek " + std::wstring(kMciAlias) + L" to start";
        mciSendStringW(cmdSeek.c_str(), NULL, 0, NULL);
        _is_native_playing = false;
        _is_native_paused = false;
        emit SigMusicStateChanged(false);
    }
}

void MusicController::NativeMciSetVolume(int volume)
{
    int vol1000 = qBound(0, volume, 100) * 10;
    std::wstring cmd = L"setaudio " + std::wstring(kMciAlias) + L" volume to " + std::to_wstring(vol1000);
    mciSendStringW(cmd.c_str(), NULL, 0, NULL);
}

bool MusicController::NativeMciIsPlaying() const
{
    if (!_is_native_playing) return false;
    wchar_t statusBuf[128] = {0};
    std::wstring cmd = L"status " + std::wstring(kMciAlias) + L" mode";
    if (mciSendStringW(cmd.c_str(), statusBuf, 128, NULL) == 0)
    {
        QString mode = QString::fromWCharArray(statusBuf).trimmed().toLower();
        return (mode == "playing");
    }
    return _is_native_playing;
}

bool MusicController::IsMusicPlaying() const
{
    return NativeMciIsPlaying() || (_player && _player->playbackState() == QMediaPlayer::PlayingState);
}

void MusicController::SetPlayImmediately(bool enable)
{
    if (_play_immediately == enable) return;
    _play_immediately = enable;
    emit SettingsDirty();
    emit SigMusicSettingsChanged();
}

void MusicController::SetAutoPlayOnSlideshow(bool enable)
{
    if (_auto_play_on_slideshow == enable) return;
    _auto_play_on_slideshow = enable;
    emit SettingsDirty();
    emit SigMusicSettingsChanged();
}

void MusicController::SetAutoStopOnSlideshowExit(bool enable)
{
    if (_auto_stop_on_slideshow_exit == enable) return;
    _auto_stop_on_slideshow_exit = enable;
    emit SettingsDirty();
    emit SigMusicSettingsChanged();
}

void MusicController::SetMusicVolume(int volume)
{
    _music_volume = qBound(0, volume, 100);
    NativeMciSetVolume(_music_volume);
    if (_audio_output)
    {
        _audio_output->setVolume(_music_volume / 100.0f);
    }
    emit SettingsDirty();
    emit SigMusicSettingsChanged();
}

void MusicController::SlotSelectMusicFile()
{
    QFileDialog file_dialog(_window);
    file_dialog.setFileMode(QFileDialog::ExistingFiles);
    file_dialog.setWindowTitle(tr("选择导入的音频文件"));
    file_dialog.setDirectory(FindDefaultMusicDir());
    file_dialog.setViewMode(QFileDialog::Detail);
    file_dialog.setNameFilter(tr("音频文件 (*.wav *.mp3 *.flac *.m4a *.aac *.ogg *.wma);;纯音频 (*.wav *.mp3 *.flac);;所有文件 (*.*)"));
    QStringList fileNames;
    if (file_dialog.exec())
    {
        fileNames = file_dialog.selectedFiles();
    }
    else
    {
        return;
    }
    if (!fileNames.isEmpty())
    {
        QString chosenPath = fileNames.first();
        QString diagReason;
        if (!IsAudioFileValid(chosenPath, &diagReason))
        {
            QMessageBox::warning(_window, tr("音频文件格式冲突或损坏"),
                                 tr("所选文件存在格式问题，系统可能无法正常解码播放：\n\n%1\n\n请选择有效的纯音频文件或重命名为正确扩展名。")
                                     .arg(diagReason));
            return;
        }

        NativeMciStop();
        _player->stop();
        _music_path = chosenPath;
        _last_music_error.clear();
        _player->setSource(QUrl::fromLocalFile(_music_path));
        _player->setLoops(QMediaPlayer::Infinite);
        _player->setPosition(0);
        emit SettingsDirty();
        emit SigMusicSettingsChanged();

        // 根据用户自主勾选的策略决定是否选完立即播放
        if (_play_immediately)
        {
            SlotStartMusic();
        }
    }
}

void MusicController::SlotStartMusic()
{
    EnsureMusicSourceLoaded();
    if (_music_path.isEmpty() || !QFileInfo::exists(_music_path))
    {
        _last_music_error = tr("未找到有效的音频文件，请在音乐设置中重新选择音频。");
        qWarning() << "[MusicPlayer]" << _last_music_error;
        emit SigMusicErrorOccurred(_last_music_error);
        return;
    }

    QString diagReason;
    if (!IsAudioFileValid(_music_path, &diagReason))
    {
        _last_music_error = diagReason;
        qWarning() << "[MusicPlayer] Pre-check failed:" << diagReason;
        emit SigMusicErrorOccurred(_last_music_error);
        return;
    }

    _last_music_error.clear();

    // 核心解决点：通过 Windows 原生多媒体引擎直接将音频流推入物理声卡，绕过 Qt 6.8 FFmpeg 插件的空通道重采样 Bug
    // MCI does not provide reliable FLAC/AAC/OGG support. Send these formats
    // straight to QMediaPlayer instead of logging an expected MCI failure.
    const QString suffix = QFileInfo(_music_path).suffix().toLower();
    const bool mciCompatible = suffix == QStringLiteral("wav") ||
                               suffix == QStringLiteral("mp3") ||
                               suffix == QStringLiteral("wma");
    const bool mciOk = mciCompatible && NativeMciPlay(_music_path);
    if (!mciOk)
    {
        qDebug() << "[MusicPlayer] Using QMediaPlayer fallback for" << _music_path;
        _last_music_error.clear();
        if (_audio_output)
        {
            _audio_output->setMuted(false);
            _audio_output->setVolume(qBound(0, _music_volume, 100) / 100.0f);
        }
        if (!_player->source().isEmpty())
        {
            _player->play();
        }
    }
}

void MusicController::SlotPauseMusic()
{
    NativeMciPause();
    if (_player)
    {
        _player->pause();
    }
}

void MusicController::SlotStopMusic()
{
    NativeMciStop();
    if (_player)
    {
        _player->stop();
    }
}

void MusicController::SlotToggleMusic()
{
    if (IsMusicPlaying())
    {
        SlotStopMusic();
    }
    else
    {
        SlotStartMusic();
    }
}

void MusicController::SlotSlideShowStartMusic()
{
    if (_auto_play_on_slideshow)
    {
        SlotStartMusic();
    }
}

void MusicController::SlotSlideShowStopMusic()
{
    if (_auto_stop_on_slideshow_exit)
    {
        SlotStopMusic();
    }
}


void MusicController::SlotSetMusic() { MusicSettingsDialog::Show(this, _window); }
QString MusicController::SniffAudioFileFormat(const QString &p, QString *r) { return AudioFileProbe::SniffAudioFileFormat(p,r); }
bool MusicController::IsAudioFileValid(const QString &p, QString *r) { return AudioFileProbe::IsAudioFileValid(p,r); }
void MusicController::ReadSettings(const QVariantMap &values)
{
    QVariantMap settings = values;
    _music_volume = settings.value("music_volume", 80).toInt();
    _play_immediately = settings.value("music_play_immediately", true).toBool();
    _auto_play_on_slideshow = settings.value("music_auto_play_slideshow", true).toBool();
    _auto_stop_on_slideshow_exit = settings.value("music_auto_stop_slideshow", true).toBool();

    _music_path = settings.value("music_path").toString();
    QString diagReason;
    if (_music_path.isEmpty() || !QFileInfo::exists(_music_path) || !IsAudioFileValid(_music_path, &diagReason))
    {
        _music_path = FindDefaultMusicFile();
        settings.insert("music_path", _music_path);
    }
    if (!_music_path.isEmpty() && QFileInfo::exists(_music_path))
    {
        _player->setSource(QUrl::fromLocalFile(_music_path));
        _player->setLoops(QMediaPlayer::Infinite);
    }
    if (_audio_output)
    {
        _audio_output->setMuted(false);
        _audio_output->setVolume(qBound(0, _music_volume, 100) / 100.0f);
    }
    emit SigMusicSettingsChanged();
}
void MusicController::WriteSettings(QVariantMap &settings) const
{
    settings.insert("music_path", _music_path);
    settings.insert("music_volume", _music_volume);
    settings.insert("music_play_immediately", _play_immediately);
    settings.insert("music_auto_play_slideshow", _auto_play_on_slideshow);
    settings.insert("music_auto_stop_slideshow", _auto_stop_on_slideshow_exit);
}
