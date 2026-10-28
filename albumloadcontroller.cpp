#include "albumloadcontroller.h"
#include "opentreethread.h"
#include "protreethread.h"
#include "protreeitem.h"
#include "const.h"
#include <QDir>
#include <QFileInfo>
#include <QProgressDialog>
#include <QScopedValueRollback>

namespace {
QString CanonicalPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool SamePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

void PrepareProgress(QProgressDialog &dialog)
{
    dialog.setObjectName("albumLoadProgress");
    dialog.setAutoClose(false);
    dialog.setAutoReset(false);
    dialog.setWindowTitle(QObject::tr("正在加载相册"));
    dialog.setFixedWidth(PROGRESS_WIDTH);
    dialog.setRange(0, PROGRESS_MAX);
}

void UpdateProgress(QProgressDialog &dialog, int count)
{
    dialog.setValue(count >= PROGRESS_MAX ? count % PROGRESS_MAX : count);
}
}

AlbumLoadController::AlbumLoadController(QTreeWidget *tree)
    : QObject(tree), _tree(tree)
{
}

void AlbumLoadController::Open(const QString &path)
{
    if (path.trimmed().isEmpty() || !QFileInfo(path).isDir()) return;
    const QString canonical = CanonicalPath(path);
    if (!_pendingOpenPaths.contains(canonical)) _pendingOpenPaths.append(canonical);
    if (!_processing) DrainOpenQueue();
}

void AlbumLoadController::DrainOpenQueue()
{
    QScopedValueRollback<bool> guard(_processing, true);
    while (!_pendingOpenPaths.isEmpty()) {
        const QString path = _pendingOpenPaths.takeFirst();
        bool alreadyOpen = false;
        for (int i = 0; i < _tree->topLevelItemCount(); ++i) {
            auto *root = dynamic_cast<ProTreeItem*>(_tree->topLevelItem(i));
            if (root && SamePath(CanonicalPath(root->GetPath()), path)) {
                alreadyOpen = true;
                break;
            }
        }
        if (alreadyOpen || !QFileInfo(path).isDir()) continue;

        QProgressDialog progress(_tree);
        PrepareProgress(progress);
        OpenTreeThread worker(path, 0, _tree);
        bool canceled = false;
        connect(&progress, &QProgressDialog::canceled, &worker, [&]() {
            canceled = true;
            worker.SlotCanceled();
        });
        connect(&worker, &OpenTreeThread::SigUpdataProGress, &progress,
                [&](int count) { UpdateProgress(progress, count); });
        // Commit only after run() has returned, not on a signal emitted inside run().
        connect(&worker, &QThread::finished, &progress, &QDialog::accept);
        worker.start();
        canceled = progress.exec() != QDialog::Accepted || canceled;
        canceled = canceled || !worker.isFinished();
        if (canceled) worker.SlotCanceled();
        worker.wait();
        if (!canceled && worker.GetRoot()) {
            worker.CommitToTree(); // Sole ownership handoff: temporary worker -> tree.
            emit AlbumOpened(worker.GetRoot());
        }
    }
}

bool AlbumLoadController::Import(const QString &source, QTreeWidgetItem *album, QString *error)
{
    if (error) error->clear();
    const auto fail = [&](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (_processing) return fail(tr("相册正在加载或导入，请完成后再导入。"));
    auto *root = dynamic_cast<ProTreeItem*>(album);
    if (!root || root->treeWidget() != _tree || root->type() != TreeItemPro)
        return fail(tr("目标相册未打开。"));
    if (!QFileInfo(source).isDir() || !QFileInfo(root->GetPath()).isDir())
        return fail(tr("来源或相册目录不存在。"));
    const QString src = CanonicalPath(source);
    const QString dst = CanonicalPath(root->GetPath());
#ifdef Q_OS_WIN
    const auto cs = Qt::CaseInsensitive;
#else
    const auto cs = Qt::CaseSensitive;
#endif
    if (SamePath(src, dst) || src.startsWith(dst + '/', cs))
        return fail(tr("不能选择相册目录自身或相册内部子目录作为导入源。"));

    bool completed = false;
    {
        QScopedValueRollback<bool> guard(_processing, true);
        QProgressDialog progress(_tree);
        PrepareProgress(progress);
        progress.setWindowTitle(tr("正在导入照片"));
        ProTreeThread worker(src, dst, root, 0, _tree, root);
        bool canceled = false;
        connect(&progress, &QProgressDialog::canceled, &worker, [&]() {
            canceled = true;
            worker.SlotCanceled();
        });
        connect(&worker, &ProTreeThread::SigUpdateProgress, &progress,
                [&](int count) { UpdateProgress(progress, count); });
        connect(&worker, &QThread::finished, &progress, &QDialog::accept);
        worker.start();
        canceled = progress.exec() != QDialog::Accepted || canceled;
        canceled = canceled || !worker.isFinished();
        if (canceled) worker.SlotCanceled();
        worker.wait();
        if (!canceled) {
            worker.CommitToTree();
            completed = true;
            emit ImportCompleted();
        }
        // Uncommitted worker destruction rolls back only this import's new files.
    }
    DrainOpenQueue();
    return completed;
}
