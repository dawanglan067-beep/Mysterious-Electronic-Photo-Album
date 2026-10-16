#include "musicsettingsdialog.h"
#include "musiccontroller.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QFrame>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QUrl>
#include <QWidget>
#include <QCoreApplication>
void MusicSettingsDialog::Show(MusicController *controller, QWidget *parent)
{
    controller->EnsureMusicSourceLoaded();

    QDialog dlg(parent);
    dlg.setWindowTitle(QCoreApplication::translate("ProTreeWidget", "🎵 背景音乐控制与偏好设置"));
    dlg.setMinimumWidth(500);
    dlg.setStyleSheet(
        "QDialog { background-color: rgb(43,45,48); color: rgb(230,233,238); }"
        "QLabel { color: rgb(225,228,235); font-size: 12px; }"
        "QCheckBox { color: rgb(225,228,235); font-size: 12px; spacing: 8px; padding: 3px 0; }"
        "QCheckBox::indicator { width: 16px; height: 16px; }");

    QVBoxLayout *mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setContentsMargins(18, 16, 18, 16);
    mainLayout->setSpacing(12);

    // 1. 当前音频文件信息与选择区
    QLabel *titleTrack = new QLabel(QCoreApplication::translate("ProTreeWidget", "<b>🎧 当前背景音乐曲目与设备诊断</b>"), &dlg);
    mainLayout->addWidget(titleTrack);

    QLabel *lblCurrentTrack = new QLabel(&dlg);
    lblCurrentTrack->setWordWrap(true);
    lblCurrentTrack->setStyleSheet(
        "background-color: rgb(54,56,60); border: 1px solid rgb(82,85,90); "
        "border-radius: 5px; padding: 10px; color: rgb(235,238,242); line-height: 140%;");

    auto updateTrackLabel = [controller, lblCurrentTrack]() {
        QString statusStr;
        if (!controller->_last_music_error.isEmpty())
        {
            statusStr = QCoreApplication::translate("ProTreeWidget", "🔴 播放异常: %1").arg(controller->_last_music_error);
        }
        else if (controller->IsMusicPlaying())
        {
            statusStr = QCoreApplication::translate("ProTreeWidget", "🟢 正在播放");
        }
        else if (controller->_is_native_paused || (controller->_player && controller->_player->playbackState() == QMediaPlayer::PausedState))
        {
            statusStr = QCoreApplication::translate("ProTreeWidget", "🟡 已暂停");
        }
        else
        {
            statusStr = QCoreApplication::translate("ProTreeWidget", "⚪ 已停止");
        }

        QString devName = QMediaDevices::defaultAudioOutput().description();
        if (devName.isEmpty())
        {
            devName = QCoreApplication::translate("ProTreeWidget", "默认系统音频输出设备");
        }

        if (controller->_music_path.isEmpty() || !QFileInfo::exists(controller->_music_path))
        {
            lblCurrentTrack->setText(QCoreApplication::translate("ProTreeWidget", "状态：%1\n曲目：未设置有效音频文件\n输出设备：%2").arg(statusStr, devName));
        }
        else
        {
            QFileInfo fi(controller->_music_path);
            QString sniffReason;
            controller->SniffAudioFileFormat(controller->_music_path, &sniffReason);
            lblCurrentTrack->setText(
                QCoreApplication::translate("ProTreeWidget", "状态：%1\n曲目：%2\n格式：%3\n输出设备：%4\n路径：%5")
                    .arg(statusStr, fi.fileName(), sniffReason, devName, QDir::toNativeSeparators(fi.absoluteFilePath())));
        }
    };
    updateTrackLabel();
    mainLayout->addWidget(lblCurrentTrack);

    QHBoxLayout *fileBtnRow = new QHBoxLayout();
    QPushButton *btnChooseFile = new QPushButton(QCoreApplication::translate("ProTreeWidget", "选择/更换音乐文件..."), &dlg);
    btnChooseFile->setIcon(QIcon(":/icon/line/openpro.svg"));
    btnChooseFile->setIconSize(QSize(20, 20));
    btnChooseFile->setObjectName("canvasToolBtn");
    btnChooseFile->setCursor(Qt::PointingHandCursor);
    btnChooseFile->setMinimumHeight(30);

    QPushButton *btnDefaultFile = new QPushButton(QCoreApplication::translate("ProTreeWidget", "恢复默认音乐 (高品质旋律)"), &dlg);
    btnDefaultFile->setIcon(QIcon(":/icon/line/reset.svg"));
    btnDefaultFile->setIconSize(QSize(20, 20));
    btnDefaultFile->setObjectName("canvasToolBtn");
    btnDefaultFile->setCursor(Qt::PointingHandCursor);
    btnDefaultFile->setMinimumHeight(30);

    fileBtnRow->addWidget(btnChooseFile);
    fileBtnRow->addWidget(btnDefaultFile);
    fileBtnRow->addStretch();
    mainLayout->addLayout(fileBtnRow);

    QFrame *sep1 = new QFrame(&dlg);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setStyleSheet("color: rgb(72,75,80);");
    mainLayout->addWidget(sep1);

    // 2. 实时播放控制与音量调节区
    QLabel *titleCtrl = new QLabel(QCoreApplication::translate("ProTreeWidget", "<b>🎛️ 实时播放控制与音量</b>"), &dlg);
    mainLayout->addWidget(titleCtrl);

    QHBoxLayout *ctrlRow = new QHBoxLayout();
    QPushButton *btnPlay = new QPushButton(QCoreApplication::translate("ProTreeWidget", "立即播放"), &dlg);
    btnPlay->setIcon(QIcon(":/icon/line/play.svg"));
    btnPlay->setIconSize(QSize(20, 20));
    btnPlay->setObjectName("canvasToolBtn");
    btnPlay->setCursor(Qt::PointingHandCursor);
    btnPlay->setMinimumHeight(30);

    QPushButton *btnPause = new QPushButton(QCoreApplication::translate("ProTreeWidget", "暂停"), &dlg);
    btnPause->setIcon(QIcon(":/icon/line/pause.svg"));
    btnPause->setIconSize(QSize(20, 20));
    btnPause->setObjectName("canvasToolBtn");
    btnPause->setCursor(Qt::PointingHandCursor);
    btnPause->setMinimumHeight(30);

    QPushButton *btnStop = new QPushButton(QCoreApplication::translate("ProTreeWidget", "停止音乐"), &dlg);
    btnStop->setIcon(QIcon(":/icon/line/stop.svg"));
    btnStop->setIconSize(QSize(20, 20));
    btnStop->setObjectName("canvasToolBtn");
    btnStop->setCursor(Qt::PointingHandCursor);
    btnStop->setMinimumHeight(30);

    ctrlRow->addWidget(btnPlay);
    ctrlRow->addWidget(btnPause);
    ctrlRow->addWidget(btnStop);
    ctrlRow->addStretch();
    mainLayout->addLayout(ctrlRow);

    QHBoxLayout *volRow = new QHBoxLayout();
    QLabel *lblVolTitle = new QLabel(QCoreApplication::translate("ProTreeWidget", "🔊 音量大小:"), &dlg);
    QSlider *sliderVol = new QSlider(Qt::Horizontal, &dlg);
    sliderVol->setRange(0, 100);
    sliderVol->setValue(controller->_music_volume);
    QLabel *lblVolVal = new QLabel(QString("%1%").arg(controller->_music_volume), &dlg);
    lblVolVal->setFixedWidth(40);
    volRow->addWidget(lblVolTitle);
    volRow->addWidget(sliderVol, 1);
    volRow->addWidget(lblVolVal);
    mainLayout->addLayout(volRow);

    QFrame *sep2 = new QFrame(&dlg);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet("color: rgb(72,75,80);");
    mainLayout->addWidget(sep2);

    // 3. 人性化播放策略选项区（由用户自主决定）
    QLabel *titleOpts = new QLabel(QCoreApplication::translate("ProTreeWidget", "<b>⚙️ 播放行为自主选项（自动记忆）</b>"), &dlg);
    mainLayout->addWidget(titleOpts);

    QCheckBox *chkImmediate = new QCheckBox(QCoreApplication::translate("ProTreeWidget", "选定音乐文件后立即开始播放"), &dlg);
    chkImmediate->setChecked(controller->_play_immediately);

    QCheckBox *chkSlideshowPlay = new QCheckBox(QCoreApplication::translate("ProTreeWidget", "进入【轮播图播放】时自动开启背景音乐"), &dlg);
    chkSlideshowPlay->setChecked(controller->_auto_play_on_slideshow);

    QCheckBox *chkSlideshowStop = new QCheckBox(
        QCoreApplication::translate("ProTreeWidget", "退出或暂停【轮播图播放】时自动停止背景音乐\n（若取消勾选，退出轮播后音乐将继续在主界面后台播放）"), &dlg);
    chkSlideshowStop->setChecked(controller->_auto_stop_on_slideshow_exit);

    mainLayout->addWidget(chkImmediate);
    mainLayout->addWidget(chkSlideshowPlay);
    mainLayout->addWidget(chkSlideshowStop);

    // 底部关闭按钮
    QHBoxLayout *bottomRow = new QHBoxLayout();
    bottomRow->addStretch();
    QPushButton *btnClose = new QPushButton(QCoreApplication::translate("ProTreeWidget", "完成并关闭"), &dlg);
    btnClose->setObjectName("canvasToolBtn");
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setMinimumSize(100, 30);
    bottomRow->addWidget(btnClose);
    mainLayout->addLayout(bottomRow);

    // 信号绑定
    QObject::connect(btnChooseFile, &QPushButton::clicked, &dlg, [controller, updateTrackLabel]() {
        controller->SlotSelectMusicFile();
        updateTrackLabel();
    });
    QObject::connect(btnDefaultFile, &QPushButton::clicked, &dlg, [controller, updateTrackLabel]() {
        QString defFile = controller->FindDefaultMusicFile();
        if (!defFile.isEmpty())
        {
            bool wasPlaying = controller->IsMusicPlaying();
            controller->NativeMciStop();
            controller->_player->stop();
            controller->_music_path = defFile;
            controller->_last_music_error.clear();
            controller->_player->setSource(QUrl::fromLocalFile(controller->_music_path));
            controller->_player->setLoops(QMediaPlayer::Infinite);
            emit controller->SettingsDirty();
            if (wasPlaying || controller->_play_immediately)
            {
                controller->SlotStartMusic();
            }
            updateTrackLabel();
        }
    });
    QObject::connect(btnPlay, &QPushButton::clicked, &dlg, [controller, updateTrackLabel]() {
        controller->SlotStartMusic();
        updateTrackLabel();
    });
    QObject::connect(btnPause, &QPushButton::clicked, &dlg, [controller, updateTrackLabel]() {
        controller->SlotPauseMusic();
        updateTrackLabel();
    });
    QObject::connect(btnStop, &QPushButton::clicked, &dlg, [controller, updateTrackLabel]() {
        controller->SlotStopMusic();
        updateTrackLabel();
    });
    QObject::connect(controller, &MusicController::SigMusicStateChanged, &dlg, [updateTrackLabel](bool) {
        updateTrackLabel();
    });
    QObject::connect(controller->_player, &QMediaPlayer::playbackStateChanged, &dlg, [updateTrackLabel](QMediaPlayer::PlaybackState) {
        updateTrackLabel();
    });
    QObject::connect(controller->_player, &QMediaPlayer::errorOccurred, &dlg, [updateTrackLabel](QMediaPlayer::Error, const QString &) {
        updateTrackLabel();
    });
    QObject::connect(sliderVol, &QSlider::valueChanged, &dlg, [controller, lblVolVal](int val) {
        lblVolVal->setText(QString("%1%").arg(val));
        controller->SetMusicVolume(val);
    });
    QObject::connect(chkImmediate, &QCheckBox::toggled, controller, &MusicController::SetPlayImmediately);
    QObject::connect(chkSlideshowPlay, &QCheckBox::toggled, controller, &MusicController::SetAutoPlayOnSlideshow);
    QObject::connect(chkSlideshowStop, &QCheckBox::toggled, controller, &MusicController::SetAutoStopOnSlideshowExit);
    QObject::connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}
