#ifndef PROTREEWIDGET_H
#define PROTREEWIDGET_H
#include<QSet>
#include <QTreeWidget>
#include<QAction>
#include<QProgressDialog>
#include"protreethread.h"
#include"opentreethread.h"
#include <QMediaPlayer>
#include <QAudioOutput>
class SlideShowDlg;
class ProTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    ProTreeWidget(QWidget *parent=nullptr);
    ~ProTreeWidget();
    void AddProToTree(const QString name,const QString path);
private:
    QSet<QString> _set_path;
    QTreeWidgetItem*_right_btn_item;
    QTreeWidgetItem *_active_item;
    QTreeWidgetItem*_selecetd_item;
    QAction*_action_import;
    QAction*_action_setstart;
    QAction*_action_closepro;
    QAction*_action_slideshow;
    QProgressDialog*_dialog_progress;
    QProgressDialog*_open_progressdlg;
    std::shared_ptr<ProTreeThread> _thread_create_pro;
    std::shared_ptr<OpenTreeThread> _thread_open_pro;
    bool _opening_projects = false;
    QStringList _pending_open_paths;
    QString _restoring_active_path;
    std::shared_ptr<SlideShowDlg>_slide_show_dlg;
    QMediaPlayer *_player;
    QAudioOutput *_audio_output;

    // ===== 智能检索、星标收藏、感知哈希查重与持久化扩展 =====
    QSet<QString> _favorite_paths;            // 已收藏（打星标）的图片绝对路径集合
    QString _search_keyword;                  // 当前实时搜索关键词
    bool _only_favorites;                     // 是否仅显示星标收藏图片
    QString _music_path;                      // 当前设置的背景音乐路径
    int _music_volume;                        // 背景音乐音量 (0 ~ 100)
    bool _play_immediately;                   // 选定音乐后是否立即播放
    bool _auto_play_on_slideshow;             // 进入轮播图时是否自动播放背景音乐
    bool _auto_stop_on_slideshow_exit;        // 退出/停止轮播图时是否自动停止背景音乐
    QString _last_music_error;                // 最近一次音频播放器错误信息
    bool _is_native_playing;                  // Windows 原生 MCI 引擎播放状态
    bool _is_native_paused;                   // Windows 原生 MCI 引擎暂停状态
    QAction *_action_detect_dup;              // 右键菜单：dHash 相似/重复照片检测
    QAction *_action_toggle_fav;              // 图片节点右键菜单：收藏/取消收藏

    bool NativeMciPlay(const QString &filePath);
    void NativeMciPause();
    void NativeMciStop();
    void NativeMciSetVolume(int volume);
    bool NativeMciIsPlaying() const;
    qint64 NativeMciGetPosition() const;
    qint64 NativeMciGetDuration() const;
    void NativeMciClose();

    void ApplyTreeFilter();
    bool FilterItemRecursive(QTreeWidgetItem *item);
    void CollectPicItemsRecursive(QTreeWidgetItem *item, QList<class ProTreeItem*> &outList);
    void RefreshItemFavoriteDisplay(QTreeWidgetItem *item);
    static quint64 ComputeDHash(const QString &imgPath);
    static int HammingDistance(quint64 h1, quint64 h2);
    void EnsureMusicSourceLoaded();
    void SaveWorkspaceSettings();
    void LoadWorkspaceSettings();

public:
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

private slots:
    void SlotItemPressed(QTreeWidgetItem*item,int column);
    void SlotDoubleClickItem(QTreeWidgetItem*doubleItem,int col);
    void SlotImport();
    void SlotSetAvtive();
    void SlotColsePro();
    void SlotUpdateProgress(int count);
    void SlotFinishProgress();
    void SlotCancelProgress();

    void SlotUpOpenProgress(int count);
    void SlotFinishOpenProgress();
    void SlotCancelOpenProgress();
    void SlotSlideShow();
    void SlotToggleFavorite();
public slots:
    void SlotOpenPro(const QString &path);
    void SlotNextShow();
    void SlotPreShow();
    void SlotSetMusic();
    void SlotSelectMusicFile();
    void SlotStartMusic();
    void SlotPauseMusic();
    void SlotStopMusic();
    void SlotToggleMusic();
    void SlotSlideShowStartMusic();
    void SlotSlideShowStopMusic();
    void SlotSearchTextChanged(const QString &text);
    void SlotSetOnlyFavorites(bool onlyFav);
    void SlotDetectDuplicates();
signals:
    void SigCancelProgress();
    void SigCancelOpenProgress();
    void SigUpdateSelected(const QString &);
    void SigUpdatePic(const QString &);
    void SigMusicStateChanged(bool isPlaying);
    void SigMusicSettingsChanged();
    void SigMusicErrorOccurred(const QString &errorMsg);
};

#endif // PROTREEWIDGET_H

