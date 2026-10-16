#include "albumpdfexporter.h"
#include "photowallwidget.h"
#include "photowallmodel.h"
#include "thumbnailloader.h"
#include "mainwindow.h"
#include "protreewidget.h"
#include "protreeitem.h"
#include "picshow.h"
#include "const.h"
#include <QSettings>
#include <QStackedWidget>
#include <QApplication>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QListView>
#include <QImage>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QDirIterator>
#include <QCheckBox>
#include <QMenu>
#include <QtTest/QTest>
#include "removeprodialog.h"

static int failures = 0;
static void check(bool condition, const char *message)
{
    if (condition) qInfo() << "PASS" << message;
    else { qCritical() << "FAIL" << message; ++failures; }
}

struct ExportResult { bool success = false; bool cancelled = false; QString message; int progress = 0; };
static ExportResult exportPdf(const AlbumExportOptions &options, bool cancelBeforeStart = false, bool cancelAfterFirst = false)
{
    AlbumPdfExporter worker(options);
    QEventLoop loop;
    ExportResult result;
    QObject::connect(&worker, &AlbumPdfExporter::SigResult, &loop, [&](bool ok, bool cancelled, const QString &message) {
        result.success = ok; result.cancelled = cancelled; result.message = message;
    });
    QObject::connect(&worker, &AlbumPdfExporter::SigProgress, &worker, [&](int completed, int) {
        result.progress = completed;
        if (cancelAfterFirst) worker.Cancel();
    }, Qt::DirectConnection);
    QObject::connect(&worker, &QThread::finished, &loop, &QEventLoop::quit);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() { worker.Cancel(); loop.quit(); });
    timer.start(20000);
    if (cancelBeforeStart) worker.Cancel();
    worker.start();
    loop.exec();
    worker.wait();
    return result;
}

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    app.setFont(QFont(QStringLiteral("Microsoft YaHei"), 10));
    QFile theme(QStringLiteral(":/style/style.qss"));
    if (theme.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary test directory");
    const QString outputDir = app.arguments().size() > 1 ? app.arguments().at(1) : temporary.path();
    QDir().mkpath(outputDir);
    QList<PhotoEntry> photos;
    for (int i = 0; i < 5; ++i) {
        QImage image(i % 2 ? QSize(800, 1200) : QSize(1200, 800), QImage::Format_RGB32);
        image.fill(QColor::fromHsv(i * 65, 140, 210));
        const QString path = temporary.filePath(QStringLiteral("照片_%1.png").arg(i + 1));
        check(image.save(path), "create landscape/portrait photo");
        photos.append({path, QStringLiteral("照片 %1：中文说明与回忆").arg(i + 1)});
    }
    {
        PhotoWallWidget wall;
        wall.resize(960, 640);
        wall.SetPhotos(photos);
        wall.show();
        auto *view = wall.findChild<QListView*>();
        check(view && view->model()->rowCount() == 5, "wall exposes all photos");
        view->selectionModel()->select(view->model()->index(3, 0), QItemSelectionModel::Select);
        view->selectionModel()->select(view->model()->index(1, 0), QItemSelectionModel::Select);
        check(wall.SelectedPaths() == QStringList{photos.at(1).path, photos.at(3).path}, "export order follows wall, not click order");
        wall.SetPhotos({photos.at(1), photos.at(2)});
        check(wall.SelectedPaths() == QStringList{photos.at(1).path}, "filter/deletion drops stale selection");
        wall.SetPhotos(photos);
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 700) { app.processEvents(); QThread::msleep(5); }
        check(wall.grab().save(outputDir + QStringLiteral("/photo-wall.png")), "gallery screenshot");
        wall.SetPhotos({});
        check(wall.SelectedPaths().isEmpty() && view->model()->rowCount() == 0, "close album clears wall");
    }
    {
        ThumbnailLoader loader;
        bool stale = false;
        QObject::connect(&loader, &ThumbnailLoader::SigReady, &app, [&](const QString &, const QImage &) { stale = true; });
        loader.Request(photos.first().path);
        loader.Reset();
        QEventLoop loop; QTimer::singleShot(250, &loop, &QEventLoop::quit); loop.exec();
        check(!stale, "album reset discards stale thumbnail results");
    }
    for (int i = 0; i < 10; ++i) {
        PhotoWallModel model;
        model.SetPhotos(photos);
        model.data(model.index(0), Qt::DecorationRole);
        model.SetPhotos({photos.last()});
        model.data(model.index(0), Qt::DecorationRole);
    }
    app.processEvents();
    check(true, "destroy models with active thumbnail workers");
    {
        QSettings isolated(QStringLiteral("AlbumFeatureTests"), QStringLiteral("AlbumApp"));
        isolated.clear();
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        QTest::qWait(250);
        auto *tree = window.findChild<ProTreeWidget*>();
        auto *wall = window.findChild<PhotoWallWidget*>();
        auto *view = wall->findChild<QListView*>();
        const QString album = temporary.filePath(QStringLiteral("reopen-album"));
        QDir().mkpath(album);
        for (int i = 0; i < photos.size(); ++i)
            check(QFile::copy(photos.at(i).path, album + QStringLiteral("/shot_%1.png").arg(i)), "copy close/reopen sample");
        tree->SlotOpenPro(album);
        app.processEvents();
        check(tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 5, "open scans all sample photos");
        check(view->model()->rowCount() == 5, "first opening shows all sample photos");
        auto *root = tree->topLevelItem(0);
        tree->setCurrentItem(root);
        tree->scrollToItem(root);
        bool closeTriggered = false, closeAccepted = false;
        // A triggered action enters a nested dialog loop. A separate timer is
        // required: the menu timer cannot reenter its own running callback.
        QTimer dialogAutomation;
        QObject::connect(&dialogAutomation, &QTimer::timeout, &window, [&]() {
            if (auto *dialog = qobject_cast<RemoveProDialog*>(QApplication::activeModalWidget())) {
                check(!dialog->findChild<QCheckBox*>()->isChecked(), "close defaults to preserving local files");
                closeAccepted = true;
                dialog->accept();
            }
        });
        dialogAutomation.start(10);
        QTimer automation;
        QObject::connect(&automation, &QTimer::timeout, &window, [&]() {
            if (auto *dialog = qobject_cast<RemoveProDialog*>(QApplication::activeModalWidget())) {
                check(!dialog->findChild<QCheckBox*>()->isChecked(), "close defaults to preserving local files");
                closeAccepted = true;
                dialog->accept();
            } else {
                for (auto *widget : QApplication::topLevelWidgets()) {
                auto *menu = qobject_cast<QMenu*>(widget);
                if (!menu || !menu->isVisible()) continue;
                if (!closeTriggered) for (auto *action : menu->actions()) {
                    if (action->text() == QStringLiteral("关闭项目")) {
                        closeTriggered = true;
                        action->trigger();
                        menu->close();
                        break;
                    }
                }
                }
            }
        });
        automation.start(10);
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, &window, [&]() {
            if (auto *dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
            if (auto *menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) menu->close();
        });
        watchdog.start(5000);
        QTest::mousePress(tree->viewport(), Qt::RightButton, Qt::NoModifier, tree->visualItemRect(root).center());
        QTest::mouseRelease(tree->viewport(), Qt::RightButton, Qt::NoModifier, tree->visualItemRect(root).center());
        automation.stop();
        dialogAutomation.stop();
        watchdog.stop();
        app.processEvents();
        check(closeTriggered && closeAccepted && tree->topLevelItemCount() == 0, "right-click close removes only album UI");
        check(QDir(album).entryList({QStringLiteral("*.png")}, QDir::Files).size() == 5, "right-click close preserves all files");
        tree->SlotOpenPro(album);
        app.processEvents();
        check(tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 5 && view->model()->rowCount() == 5, "reopen restores every sample photo");
        window.grab().save(outputDir + QStringLiteral("/reopened-album.png"));
        const QString screenshots = QStringLiteral("C:/Users/Kronos/Pictures/Screenshots");
        if (QDir(screenshots).exists()) {
            QStringList files;
            QDirIterator scan(screenshots, {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files, QDirIterator::Subdirectories);
            while (scan.hasNext()) files.append(scan.next());
            tree->SelectPhotoByPath(album + QStringLiteral("/shot_0.png"));
            QMetaObject::invokeMethod(wall, "SigOpenPhoto", Qt::DirectConnection, Q_ARG(QString, album + QStringLiteral("/shot_0.png")));
            tree->SlotOpenPro(screenshots);
            app.processEvents();
            QTreeWidgetItem *shotsRoot = tree->topLevelItem(tree->topLevelItemCount() - 1);
            check(tree->CurrentAlbumRootPath() == screenshots && view->model()->rowCount() == files.size(), "opening another album changes current wall without a manual click");
            check(window.findChild<QStackedWidget*>()->currentWidget() == wall, "opening an album returns from single photo to gallery");
            check(tree->VisiblePhotos(screenshots).size() == files.size(), "real screenshot directory is read fully including subdirectories");
            qInfo() << "Real screenshot count:" << files.size() << "wall:" << view->model()->rowCount();
            window.grab().save(outputDir + QStringLiteral("/screenshots-album.png"));
        }
        isolated.clear();
    }
    {
        QSettings isolated(QStringLiteral("AlbumFeatureTests"), QStringLiteral("AlbumApp"));
        isolated.clear();
        MainWindow window;
        window.resize(1280, 800);
        auto *tree = window.findChild<ProTreeWidget*>();
        auto *wall = window.findChild<PhotoWallWidget*>();
        auto *rootA = new ProTreeItem(tree, QStringLiteral("测试相册 A"), temporary.path() + QStringLiteral("/A"), TreeItemPro);
        auto *rootB = new ProTreeItem(tree, QStringLiteral("测试相册 B"), temporary.path() + QStringLiteral("/B"), TreeItemPro);
        QList<ProTreeItem*> items;
        for (int i = 0; i < photos.size(); ++i) items.append(new ProTreeItem(rootA, QFileInfo(photos.at(i).path).fileName(), photos.at(i).path, rootA, TreeItemPic));
        new ProTreeItem(rootB, QStringLiteral("另一张照片"), photos.last().path, rootB, TreeItemPic);
        tree->setCurrentItem(rootA);
        window.show();
        app.processEvents();
        auto *view = wall->findChild<QListView*>();
        check(view->model()->rowCount() == 5, "tree album selection refreshes main-window wall");
        check(tree->SelectPhotoByPath(photos.at(2).path) && tree->currentItem() == items.at(2), "wall path resolves existing tree photo");
        tree->SlotSearchTextChanged(QStringLiteral("照片_2"));
        app.processEvents();
        check(view->model()->rowCount() == 1 && view->model()->data(view->model()->index(0, 0), Qt::UserRole).toString() == photos.at(1).path, "search updates integrated wall");
        tree->SlotSearchTextChanged(QString());
        app.processEvents();
        QMetaObject::invokeMethod(wall, "SigOpenPhoto", Qt::DirectConnection, Q_ARG(QString, photos.at(1).path));
        check(window.findChild<QStackedWidget*>()->currentWidget() == window.findChild<PicShow*>(), "wall activation opens existing viewer");
        tree->setCurrentItem(rootB);
        app.processEvents();
        check(view->model()->rowCount() == 1, "switch album replaces wall photos");
        tree->SlotSetOnlyFavorites(true);
        app.processEvents();
        check(view->model()->rowCount() == 0, "favorites filter updates wall");
        tree->SlotSetOnlyFavorites(false);
        app.processEvents();
        window.grab().save(outputDir + QStringLiteral("/main-window.png"));
        isolated.clear();
    }
    AlbumExportOptions options;
    options.title = QStringLiteral("我们的纪念相册");
    options.photos = photos;
    for (int layout : {1, 2, 4}) {
        options.photosPerPage = layout;
        options.outputPath = outputDir + QStringLiteral("/album-%1.pdf").arg(layout);
        const auto result = exportPdf(options);
        check(result.success && result.progress == 5 && readFile(options.outputPath).startsWith("%PDF-"), "PDF layout export completes");
    }
    const QString protectedPath = temporary.filePath(QStringLiteral("existing.pdf"));
    const QByteArray sentinel("EXISTING FILE MUST SURVIVE");
    { QFile file(protectedPath); file.open(QIODevice::WriteOnly); file.write(sentinel); }
    options.outputPath = protectedPath;
    auto result = exportPdf(options, true);
    check(result.cancelled && readFile(protectedPath) == sentinel, "cancel preserves existing destination");
    options.photos.clear();
    for (int i = 0; i < 200; ++i) options.photos.append(photos.at(i % 5));
    result = exportPdf(options, false, true);
    check(result.cancelled && result.progress == 1 && readFile(protectedPath) == sentinel, "cancel during export preserves destination");
    options.photos = photos;
    options.photos[1].path = temporary.filePath(QStringLiteral("missing.png"));
    result = exportPdf(options);
    check(!result.success && !result.cancelled && readFile(protectedPath) == sentinel, "missing source fails without replacing destination");
    options.photos = photos;
    options.outputPath = photos.first().path;
    const auto original = readFile(options.outputPath);
    result = exportPdf(options);
    check(!result.success && readFile(options.outputPath) == original, "source photo cannot be overwritten");
    options.outputPath = temporary.filePath(QStringLiteral("absent/fail.pdf"));
    result = exportPdf(options);
    check(!result.success, "unwritable destination reports failure");
    qInfo() << "Failures:" << failures;
    return failures == 0 ? 0 : 1;
}
