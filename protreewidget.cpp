#include "protreewidget.h"
#include <QDir>
#include <QIcon>
#include <QHeaderView>
#include "const.h"
#include "protreeitem.h"
#include<QGuiApplication>
#include<QMenu>
#include<QFileDialog>
#include <qprogressdialog.h>
#include"removeprodialog.h"
#include"slideshowdlg.h"
ProTreeWidget::ProTreeWidget(QWidget *parent): QTreeWidget(parent)
    ,_active_item(nullptr),_right_btn_item(nullptr),_open_progressdlg(nullptr),_dialog_progress(nullptr)
    ,_selecetd_item(nullptr),_thread_create_pro(nullptr),_thread_open_pro(nullptr)
{
    this->setHeaderHidden(true);
    this->setIconSize(QSize(20, 20));
    connect(this,&ProTreeWidget::itemPressed,this,&ProTreeWidget::SlotItemPressed);
    _action_import=new QAction(QIcon(":/icon/import.png"),tr("导入文件"),this);
    _action_setstart=new QAction(QIcon(":/icon/core.png"),tr("设置活动项目"),this);
    _action_closepro=new QAction(QIcon(":/icon/close.png"),tr("关闭项目"),this);
    _action_slideshow=new QAction(QIcon(":/icon/slideshow.png"),tr("轮播图播放"),this);
    connect(_action_import,&QAction::triggered,this,&ProTreeWidget::SlotImport);
    connect(_action_setstart,&QAction::triggered,this,&ProTreeWidget::SlotSetAvtive);
    connect(_action_closepro,&QAction::triggered,this,&ProTreeWidget::SlotColsePro);
    connect(this,&ProTreeWidget::itemDoubleClicked,this,&ProTreeWidget::SlotDoubleClickItem);
    connect(_action_slideshow,&QAction::triggered,this,&ProTreeWidget::SlotSlideShow);
    _player = new QMediaPlayer(this);
    _audio_output = new QAudioOutput(this);
    _player->setAudioOutput(_audio_output);
}

void ProTreeWidget::AddProToTree(const QString name, const QString path)
{
    QDir dir(path);
    QString file_path = dir.absoluteFilePath(name);
    if (_set_path.find(file_path) != _set_path.end())
    {
        return;
    }

    QDir pro_dir(file_path);
    if (!pro_dir.exists())
    {
        bool enable = pro_dir.mkpath(file_path);
        if (!enable)
        {
            return;
        }
    }

    _set_path.insert(file_path);

    auto *item = new ProTreeItem(this, name, file_path, TreeItemPro);
    item->setData(0, Qt::DisplayRole, name);
    item->setData(0, Qt::DecorationRole, QIcon(":/icon/dir.png"));
    item->setData(0, Qt::ToolTipRole, file_path);
    this->addTopLevelItem(item);
}

void ProTreeWidget::SlotItemPressed(QTreeWidgetItem *item, int column)
{
    if(QGuiApplication::mouseButtons()==Qt::RightButton)
    {
        QMenu menu(this);
        int itemtype=item->type();
        if(itemtype==TreeItemPro)
        {
            _right_btn_item=item;
            menu.addAction(_action_import);
            menu.addAction(_action_setstart);
            menu.addAction(_action_closepro);
            menu.addAction(_action_slideshow);
            menu.exec(QCursor::pos());
        }
    }
}

void ProTreeWidget::SlotDoubleClickItem(QTreeWidgetItem *doubleItem, int col)
{
    if(QGuiApplication::mouseButtons()==Qt::LeftButton)
    {
        auto *tree_doubleItem=dynamic_cast<ProTreeItem*>(doubleItem);
        if(!tree_doubleItem)
        {
            return;
        }
        int itemtype=tree_doubleItem->type();
        if(itemtype==TreeItemPic)
        {
            emit SigUpdateSelected(tree_doubleItem->GetPath());
            _selecetd_item=doubleItem;

        }
    }
}

void ProTreeWidget::SlotImport()
{
    QFileDialog file_dialog;
    file_dialog.setFileMode(QFileDialog::Directory);
    file_dialog.setWindowTitle(tr("选择导入的文件夹"));
    QString path="";
    if(!_right_btn_item)
    {
        qDebug()<<"_right_btn_item is empty!";
        return;
    }
    path=dynamic_cast<ProTreeItem*>(_right_btn_item)->GetPath();
    file_dialog.setDirectory(path);
    file_dialog.setViewMode(QFileDialog::Detail);

    QStringList fileNames;
    if(file_dialog.exec())
    {
        fileNames=file_dialog.selectedFiles();
    }
    if(fileNames.length()<=0)
    {
        return;
    }
    QString import_path=fileNames.at(0);
    int file_count=0;
    //进度监控机制
    _dialog_progress=new QProgressDialog(this);
    //线程
    _thread_create_pro=std::make_shared<ProTreeThread>(std::ref(import_path),std::ref(path),
    std::ref(_right_btn_item),file_count,this,_right_btn_item,nullptr);
    connect(_thread_create_pro.get(),&ProTreeThread::SigUpdateProgress,this,&ProTreeWidget::SlotUpdateProgress);
    connect(_thread_create_pro.get(),&ProTreeThread::SigFinishProgress,this,&ProTreeWidget::SlotFinishProgress);
    connect(_dialog_progress,&QProgressDialog::canceled,this,&ProTreeWidget::SlotCancelProgress);
    connect(this,&ProTreeWidget::SigCancelProgress,_thread_create_pro.get(),&ProTreeThread::SlotCanceled);
    _thread_create_pro->start();
    _dialog_progress->setWindowTitle("Please wait...");
    _dialog_progress->setFixedWidth(PROGRESS_WIDTH);
    _dialog_progress->setRange(0,PROGRESS_WIDTH);
    _dialog_progress->exec();

}

void ProTreeWidget::SlotSetAvtive()
{
    if(!_right_btn_item)
    {
        return;
    }
    QFont nullFont;
    nullFont.setBold(false);
    if(_active_item)
    {
        _active_item->setFont(0,nullFont);
    }
    _active_item=_right_btn_item;
    nullFont.setBold(true);
    _active_item->setFont(0,nullFont);
}

void ProTreeWidget::SlotColsePro()
{
    RemoveProDialog remove_pro_dialog;
    auto res=remove_pro_dialog.exec();
    if(res!=QDialog::Accepted)
    {
        return;
    }
    bool b_remove=remove_pro_dialog.IsRemoved();
    auto index_right_btn=this->indexOfTopLevelItem(_right_btn_item);
    auto *protreeitem=dynamic_cast<ProTreeItem*>(_right_btn_item);
    auto*selecteditem=dynamic_cast<ProTreeItem*>(_selecetd_item);
    auto delete_path=protreeitem->GetPath();
    _set_path.remove(delete_path);
    if(b_remove)
    {
        QDir delete_dir(delete_path);
        delete_dir.removeRecursively();
    }
    // 如果当前正在展示的图片属于即将被删除/关闭的相册，清空图片展示区与选中记录
    if(selecteditem && protreeitem == selecteditem->GetRoot())
    {
        _selecetd_item = nullptr;
        emit SigUpdatePic(""); // 发送空路径信号清空 PicShow 展示区
    }
    if(protreeitem==_active_item)
    {
        _active_item=nullptr;
    }
    delete this->takeTopLevelItem(index_right_btn);//从树控件中移除并释放相册顶层节点
    _right_btn_item=nullptr;
}
void ProTreeWidget::SlotUpdateProgress(int count)
{
    if(!_dialog_progress)
    {
        return;
    }
    if(count>=PROGRESS_MAX)
    {
        _dialog_progress->setValue(count%PROGRESS_MAX);
    }
    else
    {
        _dialog_progress->setValue(count);
    }
}

void ProTreeWidget::SlotFinishProgress()
{
    if(!_dialog_progress)
    {
        return;
    }
    _dialog_progress->setValue(PROGRESS_MAX);
    _dialog_progress->deleteLater();
    _dialog_progress=nullptr;
}

void ProTreeWidget::SlotCancelProgress()
{
    emit SigCancelProgress();
    _dialog_progress->deleteLater();
    _dialog_progress=nullptr;
}

void ProTreeWidget::SlotUpOpenProgress(int count)
{
    if(!_open_progressdlg)
    {
        return;
    }
    if(count>=PROGRESS_MAX)
    {
        _open_progressdlg->setValue(count%PROGRESS_MAX);
    }
    else
    {
        _open_progressdlg->setValue(count);
    }
}

void ProTreeWidget::SlotFinishOpenProgress()
{
    if(!_open_progressdlg)
    {
        return;
    }
    _open_progressdlg->setValue(PROGRESS_MAX);
    _open_progressdlg->deleteLater();
    _open_progressdlg=nullptr;
}

void ProTreeWidget::SlotCancelOpenProgress()
{
    emit SigCancelOpenProgress();
    _open_progressdlg->deleteLater();
    _open_progressdlg=nullptr;
}

void ProTreeWidget::SlotSlideShow()
{
    if(!_right_btn_item)
    {
        return;
    }
    auto *right_pro_item=dynamic_cast<ProTreeItem*>(_right_btn_item);
    auto *last_child_item=right_pro_item->GetLastPicChild();
    if(!last_child_item)
    {
        return;
    }
    auto *first_child_item=right_pro_item->GetFirstPicChild();
    if(!first_child_item)
    {
        return;
    }
    _slide_show_dlg=std::make_shared<SlideShowDlg>(this,first_child_item,last_child_item);
    _slide_show_dlg->setModal(true);
    _slide_show_dlg->showMaximized();
}

void ProTreeWidget::SlotOpenPro(const QString &path)
{
    //避免重复打开
    if(_set_path.find(path)!=_set_path.end())
    {
        return;
    }
    _set_path.insert(path);
    int file_count=0;
    QDir pro_dir(path);
    QString proname=pro_dir.dirName();

    _thread_open_pro=std::make_shared<OpenTreeThread>(path,file_count,this,nullptr);
    _thread_open_pro->start();

    _open_progressdlg=new QProgressDialog(this);
    connect(_thread_open_pro.get(),&OpenTreeThread::SigUpdataProGress,this,&ProTreeWidget::SlotUpOpenProgress);
    connect(_thread_open_pro.get(),&OpenTreeThread::SigFinishProGress,this,&ProTreeWidget::SlotFinishOpenProgress);
    connect(_open_progressdlg,&QProgressDialog::canceled,this,&ProTreeWidget::SlotCancelOpenProgress);
    connect(this,&ProTreeWidget::SigCancelOpenProgress,_thread_open_pro.get(),&OpenTreeThread::SlotCanceled);
    _open_progressdlg->setWindowTitle("Please wait...");
    _open_progressdlg->setFixedWidth(PROGRESS_WIDTH);
    _open_progressdlg->setRange(0,PROGRESS_WIDTH);
    _open_progressdlg->exec();
}

// 响应 PicShow 界面点击“下一张”按钮的槽函数
void ProTreeWidget::SlotNextShow()
{
    if(!_selecetd_item)
    {
        return;
    }
    // 获取当前图片节点在双向链表中的后继节点
    auto *curItem=dynamic_cast<ProTreeItem*>(_selecetd_item)->GetNextItem();
    if(!curItem)
    {
        return; // 已经到达相册末尾，没有下一张
    }
    // 发送图片路径更新信号，通知 PicShow 加载并显示下一张
    emit SigUpdatePic(curItem->GetPath());
    _selecetd_item=curItem;
    this->setCurrentItem(curItem); // 同步树目录中的当前高亮选中项
}

// 响应 PicShow 界面点击“上一张”按钮的槽函数
void ProTreeWidget::SlotPreShow()
{
    if(!_selecetd_item)
    {
        return;
    }
    // 获取当前图片节点在双向链表中的前驱节点
    auto *curItem=dynamic_cast<ProTreeItem*>(_selecetd_item)->GetPreItem();
    if(!curItem)
    {
        return; // 已经到达相册起始，没有上一张
    }
    // 发送图片路径更新信号，通知 PicShow 加载并显示上一张
    emit SigUpdatePic(curItem->GetPath());
    _selecetd_item=curItem;
    this->setCurrentItem(curItem); // 同步树目录中的当前高亮选中项
}

void ProTreeWidget::SlotSetMusic()
{
    QFileDialog file_dialog;
    file_dialog.setFileMode(QFileDialog::ExistingFiles);
    file_dialog.setWindowTitle(tr("选择导入的音频文件"));
    file_dialog.setDirectory(QDir::currentPath());
    file_dialog.setViewMode(QFileDialog::Detail);
    file_dialog.setNameFilter(tr("音频文件 (*.mp3 *.wav *.flac *.aac);;所有文件 (*.*)"));
    QStringList fileNames;
    if(file_dialog.exec())
    {
        fileNames=file_dialog.selectedFiles();
    }
    else
    {
        return;
    }
    if (!fileNames.isEmpty())
    {
        // 1. 先停止当前正在播放的音频
        _player->stop();
        // 2. 将选中的本地文件路径转为 QUrl 并设置为新的音频源（自动替换旧音频）
        _player->setSource(QUrl::fromLocalFile(fileNames.first()));
        // 3. 设置背景音乐循环播放
        _player->setLoops(QMediaPlayer::Infinite);
    }
    if (_player->playbackState() != QMediaPlayer::PlayingState)
    {
        _player->setPosition(0); // 将音频进度重置到开头（0毫秒）
    }
}

void ProTreeWidget::SlotStartMusic()
{
    _player->play();
}

void ProTreeWidget::SlotStopMusic()
{
    _player->stop();
}


