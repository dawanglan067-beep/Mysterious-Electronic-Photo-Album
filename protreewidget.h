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
    std::shared_ptr<SlideShowDlg>_slide_show_dlg;
    QMediaPlayer *_player;
    QAudioOutput *_audio_output;
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
public slots:
    void SlotOpenPro(const QString &path);
    void SlotNextShow();
    void SlotPreShow();
    void SlotSetMusic();
    void SlotStartMusic();
    void SlotStopMusic();
signals:
    void SigCancelProgress();
    void SigCancelOpenProgress();
    void SigUpdateSelected(const QString &);
    void SigUpdatePic(const QString &);
};

#endif // PROTREEWIDGET_H
