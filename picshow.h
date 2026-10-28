#ifndef PICSHOW_H
#define PICSHOW_H

#include <QDialog>
#include <QPropertyAnimation>
#include <QImage>
#include <QVector>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QUndoStack>
#include "histogramwidget.h"
#include "imageprocessor.h"
#include"photoeditdocument.h"
class PhotoAdjustmentPanel;
namespace Ui {
class PicShow;
}

class PicShow : public QDialog
{
    Q_OBJECT

public:
    explicit PicShow(QWidget *parent = nullptr);
    ~PicShow();

    // 重新加载/刷新当前图片显示（触发 label 重新绘制）
    void ReloadPic();
    bool isModified() const { return _document.isModified(); }
    void SetMaterialAlbumRoot(const QString &path) { _material_album_root = path; }
    void SetBackupAlbumRoots(const QStringList &paths) { _backup_album_roots = paths; }
    bool IsEditingPath(const QString &path) const;
    bool ConfirmDeletionOfEditedPhoto(const QStringList &deletionPaths = {});

    // 清空当前图片数据并重绘清空展示区
    void Clear();

protected:
    // 监听鼠标进入/离开事件，用于控制上一张/下一张按钮的淡入淡出动画
    bool event(QEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;
    void reject() override {}

private:
    // 播放按钮透明度动画：b_show 为 true 表示淡入显示，false 表示淡出隐藏
    void ShowPreNextBtns(bool b_show);

    // 初始化专业画布工具栏与右侧 RGB 直方图/元数据信息面板
    void InitStudioUI();

    // 根据原始图像与当前旋转/镜像状态重新生成显示图像并刷新直方图与信息面板
    void RebuildTransformedPixmap();

    // 更新右侧图像元数据详情与 RGB 直方图
    void UpdateImageMetadata();

    // 重置视图缩放与平移偏移（自适应窗口）
    void ResetViewFit();
    void SyncControlsFromDocument();
    void SetOriginalComparison(bool enabled);
    bool ExportProcessedImage(const QStringList &protectedPaths = {});
    QPushButton *_compare_button = nullptr;
    QPushButton *_restore_original_button = nullptr;
    QPushButton *_save_material_button = nullptr;
    QString _material_album_root;
    QStringList _backup_album_roots;
    QPushButton *_overwrite_button = nullptr;

    friend class PhotoEditCommand;
    void ApplyEditState(const PhotoEditDocument::EditState &state);
    void PushEdit(const PhotoEditDocument::EditState &before,
                  const PhotoEditDocument::EditState &after, const QString &text);
    void FinishParameterDrag();
    QUndoStack *_undo_stack;
    bool _parameter_dragging = false;
    PhotoEditDocument::EditState _drag_start_state;

    Ui::PicShow *ui;
    QPropertyAnimation* _animation_show_pre;   // 上一张按钮的透明度动画
    QPropertyAnimation* _animation_show_next;  // 下一张按钮的透明度动画
    bool _b_btnvisible;                       // 标记当前按钮是否处于可见状态
    // ===== 底部工具栏与右侧直方图/算法工坊面板控件 =====
    QWidget *_toolbar_wid;
    QPushButton *_btn_zoom_reset;
    QWidget *_info_panel;
    HistogramWidget *_histogram_wid;
    QLabel *_lbl_meta_name;
    QLabel *_lbl_meta_dim;
    QLabel *_lbl_meta_size;
    QLabel *_lbl_meta_depth;
    QLabel *_lbl_meta_rgb;
    QLabel *_lbl_meta_time;
    QLabel *_lbl_meta_path;

    PhotoAdjustmentPanel *_adjustments = nullptr;
    PhotoEditDocument _document;



private slots:
    void SlotCrop();
    void SlotUndo();
    void SlotRedo();
    void SlotZoomIn();
    void SlotZoomOut();
    void SlotZoomFit();
    void SlotZoomActual();
    void SlotRotateLeft();
    void SlotRotateRight();
    void SlotFlipH();
    void SlotFlipV();
    void SlotToggleInfoPanel();
    void SlotOpenInExplorer();
    void SlotAdjustParamsChanged();
    void SlotResetAlgorithms();
    void SlotRestoreOriginal();
    void SlotSaveProcessedImage();
    void SlotSaveMaterial();
    void SlotOverwriteOriginal();
    void SlotRestoreBackup();

public slots:
    // 树节点被双击选中时的图片加载槽函数
    void SlotSelectItem(const QString &path);

    // 切换上一张/下一张时的图片更新槽函数
    void SlotUpdatePic(const QString &path);

signals:
    void SigSourceOverwritten(const QString &path);
    void SigMaterialSaved(const QString &path, const QString &albumRoot);
    void SigNextClicked(); // 点击下一张按钮时发射的信号
    void SigPreClicked();  // 点击上一张按钮时发射的信号
};

#endif // PICSHOW_H

