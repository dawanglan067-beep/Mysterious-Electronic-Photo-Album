#pragma once
#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVariantMap>
#include "audiofileprobe.h"
class QWidget;
class MusicSettingsDialog;
class MusicController : public QObject {
    Q_OBJECT
    friend class MusicSettingsDialog;
public:
    explicit MusicController(QWidget *owner);
    ~MusicController() override;
    bool IsMusicPlaying() const;
    QString GetMusicPath() const { return _music_path; }
    int GetMusicVolume() const { return _music_volume; }
    bool GetPlayImmediately() const { return _play_immediately; }
    bool GetAutoPlayOnSlideshow() const { return _auto_play_on_slideshow; }
    bool GetAutoStopOnSlideshowExit() const { return _auto_stop_on_slideshow_exit; }
    QString GetLastMusicError() const { return _last_music_error; }
    static QString SniffAudioFileFormat(const QString &filePath, QString *outDiagReason = nullptr);
    static bool IsAudioFileValid(const QString &filePath, QString *outDiagReason = nullptr);
    void SetPlayImmediately(bool enable);
    void SetAutoPlayOnSlideshow(bool enable);
    void SetAutoStopOnSlideshowExit(bool enable);
    void SetMusicVolume(int volume);

    void ReadSettings(const QVariantMap &settings);
    void WriteSettings(QVariantMap &settings) const;
    static QString FindDefaultMusicFile();
public slots:
    void SlotSetMusic();
    void SlotSelectMusicFile();
    void SlotStartMusic();
    void SlotPauseMusic();
    void SlotStopMusic();
    void SlotToggleMusic();
    void SlotSlideShowStartMusic();
    void SlotSlideShowStopMusic();
signals:
    void SigMusicStateChanged(bool playing);
    void SigMusicSettingsChanged();
    void SigMusicErrorOccurred(const QString &message);
    void SettingsDirty();
private:
    QWidget *_window;
    QMediaPlayer *_player;
    QAudioOutput *_audio_output;
    QString _music_path;                      // 当前设置的背景音乐路径
    int _music_volume;                        // 背景音乐音量 (0 ~ 100)
    bool _play_immediately;                   // 选定音乐后是否立即播放
    bool _auto_play_on_slideshow;             // 进入轮播图时是否自动播放背景音乐
    bool _auto_stop_on_slideshow_exit;        // 退出/停止轮播图时是否自动停止背景音乐
    QString _last_music_error;                // 最近一次音频播放器错误信息
    bool _is_native_playing;                  // Windows 原生 MCI 引擎播放状态
    bool _is_native_paused;                   // Windows 原生 MCI 引擎暂停状态
    bool NativeMciPlay(const QString &filePath);
    void NativeMciPause();
    void NativeMciStop();
    void NativeMciSetVolume(int volume);
    bool NativeMciIsPlaying() const;
    void NativeMciClose();

    void EnsureMusicSourceLoaded();
};
