#include "albumdeletioncontroller.h"
#include "protreewidget.h"
#include "picshow.h"
#include <QDir>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>

AlbumDeletionController::AlbumDeletionController(ProTreeWidget *tree, PicShow *editor, QWidget *window)
    : QObject(window), _tree(tree), _editor(editor), _window(window)
{
}

void AlbumDeletionController::DeletePhotos(const QStringList &paths)
{
    DeletePaths(paths, {});
}

void AlbumDeletionController::DeleteFolder(const QString &path)
{
    DeletePaths(_tree->FolderPhotoPaths(path), path);
}

void AlbumDeletionController::DeletePaths(QStringList paths, const QString &folder)
{
    paths.removeDuplicates();
    if (paths.isEmpty() && folder.isEmpty()) return;
    QMessageBox prompt(QMessageBox::Warning, folder.isEmpty() ? tr("删除图片／素材") : tr("删除文件夹"),
        folder.isEmpty() ? tr("将以下 %1 个相册内文件移到回收站，删除后重开相册不会再出现。\n"
           "从相册外复制加入的素材只删除相册副本，外部文件保留。")
            .arg(paths.size()) : tr("将整个文件夹及其中全部文件和子文件夹移到回收站。\n"
                "包括未显示的非图片文件；重开相册不会再出现该目录。\n"
                "相册其他内容及相册外的素材原件保留。"), QMessageBox::Yes | QMessageBox::Cancel, _window);
    prompt.setObjectName(folder.isEmpty() ? "photoTrashConfirmation" : "folderTrashConfirmation");
    prompt.setDetailedText(folder.isEmpty() ? paths.join('\n') : folder);
    prompt.setInformativeText(!folder.isEmpty() ? QDir::toNativeSeparators(folder)
        : paths.size() == 1 ? QDir::toNativeSeparators(paths.first())
        : tr("请展开详细信息检查全部文件路径。"));
    prompt.button(QMessageBox::Yes)->setText(tr("移到回收站"));
    prompt.button(QMessageBox::Cancel)->setText(tr("取消"));
    prompt.setDefaultButton(QMessageBox::Cancel);
    if (prompt.exec() != QMessageBox::Yes) return;
    QStringList protectedPaths = paths;
    if (!folder.isEmpty()) protectedPaths.append(folder);
    for (const auto &path : paths) {
        if (_editor->IsEditingPath(path) && !_editor->ConfirmDeletionOfEditedPhoto(protectedPaths)) return;
    }
    if (!folder.isEmpty()) {
        QString error;
        if (!_tree->TrashFolder(folder, &error))
            QMessageBox::warning(_window, tr("文件夹未删除"), error);
        return;
    }
    QStringList failures;
    for (const auto &path : paths) {
        QString error;
        const bool edited = _editor->IsEditingPath(path);
        if (!_tree->TrashPhoto(path, &error)) failures << tr("%1：%2").arg(path, error);
        else if (edited && _editor->IsEditingPath(path)) _editor->Clear();
    }
    if (!failures.isEmpty()) QMessageBox::warning(_window, tr("部分图片未删除"),
        tr("以下文件未删除，相册记录已保留：\n%1").arg(failures.join('\n')));

}
