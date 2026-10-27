#ifndef PROTREEWIDGET_H
#define PROTREEWIDGET_H
#include<QSet>
#include <QTreeWidget>
#include<QAction>
#include<QProgressDialog>
#include"protreethread.h"
#include"opentreethread.h"
#include "musiccontroller.h"
#include "photoentry.h"
class SlideShowDlg;
class ProTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    ProTreeWidget(QWidget *parent=nullptr);
    ~ProTreeWidget();
    void AddProToTree(const QString name,const QString path);
    QString CurrentAlbumRootPath() const;
    QList<PhotoEntry> VisiblePhotos(const QString &albumRootPath) const;
    bool SelectPhotoByPath(const QString &path);
    QString AlbumRootForPhoto(const QString &path) const;
    bool TrashPhoto(const QString &path, QString *error = nullptr);
    bool TrashFolder(const QString &path, QString *error = nullptr);
    QStringList FolderPhotoPaths(const QString &path) const;
    void HideFolder(const QString &path);
    void ShowHiddenFolders(const QString &albumRoot);
    bool RegisterMaterial(const QString &albumRoot, const QString &savedPath,
                          QString *registeredPath, QString *error);
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
    MusicController *_music;

    // ===== 智能检索、星标收藏、感知哈希查重与持久化扩展 =====
    QSet<QString> _favorite_paths;            // 已收藏（打星标）的图片绝对路径集合
    QString _search_keyword;                  // 当前实时搜索关键词
    bool _only_favorites;                     // 是否仅显示星标收藏图片
    QAction *_action_detect_dup;              // 右键菜单：dHash 相似/重复照片检测
    QAction *_action_toggle_fav;              // 图片节点右键菜单：收藏/取消收藏
    QAction *_action_trash_photo = nullptr;
    QAction *_action_hide_folder = nullptr;
    QAction *_action_trash_folder = nullptr;
    QAction *_action_show_folders = nullptr;
    QSet<QString> _hidden_folder_paths;
    QSet<QString> _show_empty_material_roots;
    bool TrashPath(const QString &path, bool folder, QString *error);

    void ApplyTreeFilter();
    bool FilterItemRecursive(QTreeWidgetItem *item);
    void CollectPicItemsRecursive(QTreeWidgetItem *item, QList<class ProTreeItem*> &outList);
    void RefreshItemFavoriteDisplay(QTreeWidgetItem *item);
    void SaveWorkspaceSettings();
    void LoadWorkspaceSettings();

public:
    bool IsMusicPlaying() const;
    QString GetMusicPath() const { return _music->GetMusicPath(); }
    int GetMusicVolume() const { return _music->GetMusicVolume(); }
    bool GetPlayImmediately() const { return _music->GetPlayImmediately(); }
    bool GetAutoPlayOnSlideshow() const { return _music->GetAutoPlayOnSlideshow(); }
    bool GetAutoStopOnSlideshowExit() const { return _music->GetAutoStopOnSlideshowExit(); }
    QString GetLastMusicError() const { return _music->GetLastMusicError(); }
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
    void SigAlbumOpened(const QString &path);
    void SigPhotoSetChanged();
    void SigDeletePhotosRequested(const QStringList &paths);
    void SigDeleteFolderRequested(const QString &path);
    void SigCancelProgress();
    void SigCancelOpenProgress();
    void SigUpdateSelected(const QString &);
    void SigUpdatePic(const QString &);
    void SigMusicStateChanged(bool isPlaying);
    void SigMusicSettingsChanged();
    void SigMusicErrorOccurred(const QString &errorMsg);
};

#endif // PROTREEWIDGET_H

