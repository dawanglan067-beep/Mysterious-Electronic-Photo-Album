#include "albumpdfexporter.h"
#include "photowallwidget.h"
#include "photowallmodel.h"
#include "thumbnailloader.h"
#include "mainwindow.h"
#include "protreewidget.h"
#include "albumloadcontroller.h"
#include "protreeitem.h"
#include "picshow.h"
#include "cropdialog.h"
#include <QDialogButtonBox>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QTransform>
#include "imageprocessor.h"
#include "imageexportservice.h"
#include "materialsavedialog.h"
#include "phototrashservice.h"
#include "materialimportservice.h"
#include "duplicatereviewdialog.h"
#include "photocanvas.h"
#include "photoadjustmentpanel.h"
#include <QListWidget>
#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#endif
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QFileDialog>
#include "duplicatephotofinder.h"
#include "workspacesettings.h"
#include "musiccontroller.h"
#include <QSlider>
#include <QComboBox>
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
    {
        QTreeWidget tree;
        AlbumLoadController loader(&tree);
        const QString first = temporary.filePath("load-controller-first");
        const QString second = temporary.filePath("load-controller-second");
        const QString source = temporary.filePath("load-controller-source");
        QDir().mkpath(first); QDir().mkpath(second); QDir().mkpath(source);
        QImage sample(12, 9, QImage::Format_RGB32);
        sample.fill(Qt::blue);
        check(sample.save(first + "/existing.png") && sample.save(second + "/other.png")
              && sample.save(source + "/imported.png"), "create load-controller fixtures");
        QStringList opened;
        QObject::connect(&loader, &AlbumLoadController::AlbumOpened, &tree, [&](QTreeWidgetItem *root) {
            opened.append(dynamic_cast<ProTreeItem*>(root)->GetPath());
            if (opened.size() == 1) loader.Open(second); // reentry must queue this album.
        });
        loader.Open(first);
        check(opened.size() == 2 && tree.topLevelItemCount() == 2 && !loader.IsBusy(),
              "load controller serializes reentrant album opening and transfers roots once");
        loader.Open(first);
        check(tree.topLevelItemCount() == 2 && opened.size() == 2,
              "load controller ignores duplicate opened album");

        auto *firstRoot = dynamic_cast<ProTreeItem*>(tree.topLevelItem(0));
        QString error;
        check(!loader.Import(first, firstRoot, &error) && !error.isEmpty()
              && !QFileInfo::exists(first + "/load-controller-first"),
              "load controller rejects importing album into itself");
        check(loader.Import(source, firstRoot, &error) && error.isEmpty()
              && QFileInfo::exists(first + "/imported.png") && firstRoot->childCount() == 2,
              "load controller commits imported files and tree nodes together");
        check(loader.Import(source, firstRoot, &error) && firstRoot->childCount() == 2,
              "load controller repeated import does not duplicate nodes");

        const QString cancelSource = temporary.filePath("load-controller-cancel-source");
        QDir().mkpath(cancelSource);
        for (int i = 0; i < 250; ++i)
            sample.save(cancelSource + QStringLiteral("/cancel_%1.png").arg(i));
        QTimer::singleShot(0, &tree, [&]() {
            if (auto *dialog = tree.findChild<QProgressDialog*>("albumLoadProgress")) dialog->reject();
        });
        check(!loader.Import(cancelSource, firstRoot, &error) && firstRoot->childCount() == 2
              && QDir(first).entryList({"cancel_*.png"}, QDir::Files).isEmpty() && !loader.IsBusy(),
              "load controller cancellation rolls back new files without changing existing album");
        QTimer::singleShot(0, &tree, [&]() {
            if (auto *dialog = tree.findChild<QProgressDialog*>("albumLoadProgress")) dialog->reject();
        });
        loader.Open(cancelSource);
        check(tree.topLevelItemCount() == 2 && !loader.IsBusy(),
              "canceled album scan leaves no partial root or busy state");
        loader.Open(cancelSource);
        check(tree.topLevelItemCount() == 3 && tree.topLevelItem(2)->childCount() == 250,
              "album can reopen fully after canceled scan");
    }
    {
        PhotoAdjustmentPanel panel;
        panel.resize(268,180); panel.show();
        int changes = 0, starts = 0, finishes = 0;
        QObject::connect(&panel,&PhotoAdjustmentPanel::SigParametersChanged,&panel,[&]() { ++changes; });
        QObject::connect(&panel,&PhotoAdjustmentPanel::SigGestureStarted,&panel,[&]() { ++starts; });
        QObject::connect(&panel,&PhotoAdjustmentPanel::SigGestureFinished,&panel,[&]() { ++finishes; });
        ImageProcessor::Parameters parameters;
        parameters.brightness = 21; parameters.contrast = -12; parameters.saturation = 35;
        parameters.filter = ImageProcessor::FilterSepia;
        panel.SetParameters(parameters);
        auto current = panel.Parameters();
        check(changes == 0 && current.brightness == 21 && current.contrast == -12
              && current.saturation == 35 && current.filter == ImageProcessor::FilterSepia,
              "standalone adjustment panel synchronizes controls without emitting edit requests");
        auto *slider = panel.findChildren<QSlider*>().first();
        slider->setSliderDown(true); slider->setValue(22); slider->setValue(23);
        check(panel.IsSliderDown() && changes == 2 && starts == 1 && finishes == 0,
              "adjustment panel emits live values and one gesture start");
        slider->setSliderDown(false);
        bool updatedLabel = false;
        for (auto *label : panel.findChildren<QLabel*>()) updatedLabel |= label->text() == "23";
        check(!panel.IsSliderDown() && finishes == 1 && updatedLabel && panel.Parameters().brightness == 23,
              "adjustment panel owns value labels and reports gesture end without document or history");
        panel.SetParameters({});
        check(changes == 2 && panel.Parameters().brightness == 0 && panel.Parameters().filter == ImageProcessor::FilterNone,
              "panel programmatic reset does not create extra history requests");
    }
    {
        PhotoCanvas canvas; canvas.resize(240,160); canvas.show(); app.processEvents();
        QImage source(480,320,QImage::Format_RGB32);
        for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(x,y,QColor(x % 256,y % 256,(x+y) % 256));
        QImage edited = source; edited.invertPixels();
        const QImage originalPixels = source;
        QString zoomText;
        QObject::connect(&canvas,&PhotoCanvas::SigZoomTextChanged,&canvas,[&](const QString &text) { zoomText = text; });
        canvas.SetFrame(source,edited.scaled(120,80),source.size(),true,90);
        canvas.ZoomActual(); canvas.ZoomIn();
        check(zoomText == "240%", "canvas actual-pixel zoom uses full output size despite reduced preview cache");
        canvas.ZoomFit();
        QWheelEvent wheel(QPointF(180,110),canvas.mapToGlobal(QPoint(180,110)),QPoint(),QPoint(0,120),
                          Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&canvas,&wheel);
        check(zoomText == "115%", "standalone canvas wheel updates zoom without editor state");
        const auto beforePan = canvas.grab().toImage();
        const auto mouse = [&](QEvent::Type type,const QPointF &point,Qt::MouseButton button,Qt::MouseButtons held) {
            QMouseEvent event(type,point,canvas.mapToGlobal(point.toPoint()),button,held,Qt::NoModifier);
            QApplication::sendEvent(&canvas,&event);
        };
        mouse(QEvent::MouseButtonPress,QPointF(100,100),Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseMove,QPointF(130,115),Qt::NoButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,QPointF(130,115),Qt::LeftButton,Qt::NoButton);
        const auto panned = canvas.grab().toImage();
        check(panned != beforePan && canvas.cursor().shape() == Qt::ArrowCursor,
              "standalone canvas left drag pans cached pixels and releases cursor");
        canvas.SetOriginalComparison(true);
        check(canvas.IsComparing() && canvas.grab().toImage() != panned,
              "canvas comparison fits full source independently of panned edited view");
        canvas.SetOriginalComparison(false);
        check(canvas.grab().toImage() == panned && source == originalPixels,
              "canvas comparison release restores view and never mutates source pixels");
        mouse(QEvent::MouseButtonPress,QPointF(100,100),Qt::LeftButton,Qt::LeftButton);
        QFocusEvent focusOut(QEvent::FocusOut); QApplication::sendEvent(&canvas,&focusOut);
        mouse(QEvent::MouseMove,QPointF(160,120),Qt::NoButton,Qt::NoButton);
        check(canvas.grab().toImage() == panned && canvas.cursor().shape() == Qt::ArrowCursor,
              "canvas focus loss ends drag without changing view or requiring mouse release");
        canvas.SetOriginalComparison(true); canvas.hide();
        check(!canvas.IsComparing(), "standalone canvas hide ends original comparison");
        canvas.show(); canvas.SetFrame({}, {}, {}, false, 0);
        canvas.SetOriginalComparison(true);
        check(!canvas.IsComparing() && canvas.PreviewImage().isNull(), "clearing canvas removes comparison and stale preview");
    }
    {
        const QString album = temporary.filePath("service-only-album"); QDir().mkpath(album);
        const QString external = temporary.filePath("service-material.png");
        QImage image(32,24,QImage::Format_ARGB32); image.fill(QColor(20,40,60,110)); image.save(external);
        const auto bytes = readFile(external);
        const auto first = MaterialImportService::prepare(external,album);
        const auto repeated = MaterialImportService::prepare(external,album);
        check(first.success && first.albumPath == repeated.albumPath && readFile(first.albumPath) == bytes,
              "material service copies and deduplicates without tree or editor");
        image.fill(Qt::red); image.save(external);
        const auto changed = MaterialImportService::prepare(external,album);
        check(changed.success && changed.albumPath != first.albumPath && readFile(first.albumPath) == bytes
              && readFile(changed.albumPath) == readFile(external),
              "material service avoids name collision without overwriting earlier copy");
        const auto local = MaterialImportService::prepare(first.albumPath,album);
        check(local.success && local.albumPath == first.albumPath,
              "material service returns existing album file without extra copy");
        QFile invalid(temporary.filePath("invalid-material.png")); invalid.open(QIODevice::WriteOnly);
        invalid.write("not an image"); invalid.close();
        check(!MaterialImportService::prepare(invalid.fileName(),album).success
              && !MaterialImportService::prepare(external,temporary.filePath("missing-album")).success
              && readFile(first.albumPath) == bytes,
              "material service rejects invalid input while preserving previous material");
        const QString blockedAlbum = temporary.filePath("blocked-material-album"); QDir().mkpath(blockedAlbum);
        QFile blocked(blockedAlbum + QStringLiteral("/素材")); blocked.open(QIODevice::WriteOnly);
        blocked.write("occupied"); blocked.close();
        check(!MaterialImportService::prepare(external,blockedAlbum).success && readFile(blocked.fileName()) == "occupied",
              "material service handles unavailable target folder without replacing file");
    }
    {
        QSettings isolated("AlbumFeatureTests","AlbumApp"); isolated.clear();
        const QString album = temporary.filePath("duplicate-review-album"); QDir().mkpath(album);
        const QString otherAlbum = temporary.filePath("duplicate-other-album"); QDir().mkpath(otherAlbum);
        QImage image(64,48,QImage::Format_RGB32); image.fill(Qt::cyan);
        const QString keep = album + "/a.png", duplicate = album + "/b.png", other = otherAlbum + "/c.png";
        image.save(keep); image.save(duplicate); image.save(other);
        const auto original = readFile(keep);
        MainWindow window; window.resize(1280,800); window.show();
        auto *tree = window.findChild<ProTreeWidget*>();
        auto *wall = window.findChild<PhotoWallWidget*>();
        auto *editor = window.findChild<PicShow*>();
        auto *root = new ProTreeItem(tree,"duplicates",album,TreeItemPro);
        new ProTreeItem(tree,"other album",otherAlbum,TreeItemPro);
        tree->setCurrentItem(root);
        QString registered,error;
        check(tree->RegisterMaterial(album,keep,&registered,&error)
              && tree->RegisterMaterial(album,duplicate,&registered,&error)
              && tree->RegisterMaterial(otherAlbum,other,&registered,&error), "register duplicate review integration fixtures");
        tree->SelectPhotoByPath(duplicate); app.processEvents();
        editor->findChildren<QSlider*>().first()->setValue(25);
        auto *history = editor->findChild<QUndoStack*>();
        const int historyIndex = history->index();
        enum class ReviewChoice { Close, None, Delete };
        ReviewChoice choice = ReviewChoice::Close;
        bool confirm = false, discard = false, sawReport = false;
        QTimer automation;
        QObject::connect(&automation,&QTimer::timeout,&window,[&]() {
            if (auto *review = qobject_cast<DuplicateReviewDialog*>(QApplication::activeModalWidget())) {
                sawReport = true;
                auto *list = review->findChild<QListWidget*>("duplicateReviewList");
                check(list && list->count() == 1 && review->SelectedPaths() == QStringList{duplicate},
                      "duplicate dialog owns path selection and excludes cross-album matches");
                check(review->grab().save(outputDir + "/duplicate-review-dialog.png"), "duplicate review screenshot");
                if (choice == ReviewChoice::Close) review->reject();
                else {
                    auto *deleteButton = review->findChild<QPushButton*>("duplicateTrashButton");
                    if (choice == ReviewChoice::None) {
                        list->item(0)->setCheckState(Qt::Unchecked);
                        check(!deleteButton->isEnabled() && review->SelectedPaths().isEmpty(),
                              "duplicate cleanup disabled without checked paths");
                        review->reject();
                    } else deleteButton->click();
                }
            } else if (auto *prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                if (prompt->objectName() == "photoTrashConfirmation")
                    prompt->button(confirm ? QMessageBox::Yes : QMessageBox::Cancel)->click();
                else if (prompt->objectName() == "photoDeleteEditPrompt") {
                    if (discard) prompt->findChild<QPushButton*>("deleteDiscardButton")->click();
                    else prompt->button(QMessageBox::Cancel)->click();
                } else {
                    prompt->accept();
                }
            }
        });
        automation.start(10);
        tree->SlotDetectDuplicates();
        check(sawReport && QFileInfo::exists(duplicate) && history->index() == historyIndex,
              "closing duplicate review preserves original file and edits");
        choice = ReviewChoice::None; tree->SlotDetectDuplicates();
        check(QFileInfo::exists(duplicate), "unchecked duplicate review cannot start deletion");
        choice = ReviewChoice::Delete; tree->SlotDetectDuplicates();
        confirm = true; tree->SlotDetectDuplicates();
        check(QFileInfo::exists(duplicate) && tree->VisiblePhotos(album).size() == 2
              && history->index() == historyIndex && editor->isModified(),
              "duplicate cleanup shares deletion confirmation and dirty-editor cancellation");
#ifdef Q_OS_WIN
        const HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(duplicate.utf16()), GENERIC_READ,
            FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(locked != INVALID_HANDLE_VALUE, "lock duplicate against recycling");
        if (locked != INVALID_HANDLE_VALUE) {
            discard = true; tree->SlotDetectDuplicates();
            check(QFileInfo::exists(duplicate) && tree->VisiblePhotos(album).size() == 2
                  && history->index() == historyIndex && editor->isModified(),
                  "duplicate recycle failure preserves node and unsaved edit history");
            CloseHandle(locked);
        }
#endif
        discard = true; tree->SlotDetectDuplicates(); automation.stop(); app.processEvents();
        check(!QFileInfo::exists(duplicate) && readFile(keep) == original && readFile(other) == original
              && tree->VisiblePhotos(album).size() == 1 && tree->VisiblePhotos(otherAlbum).size() == 1
              && wall->findChild<QListView*>()->model()->rowCount() == 1
              && editor->IsEditingPath(keep) && history->count() == 0,
              "duplicate cleanup uses recycling coordinator and refreshes editor navigation and wall");
        check(root->GetFirstPicChild() && !root->GetFirstPicChild()->GetNextItem(),
              "duplicate cleanup uses shared navigation repair");
        isolated.clear();
    }
    {
        const QString album = temporary.filePath("trash-album");
        QDir().mkpath(album);
        QImage fixture(80,60,QImage::Format_RGB32); fixture.fill(Qt::cyan);
        const QString outside = temporary.filePath("trash-external.png"); fixture.save(outside);
        const QByteArray externalBytes = readFile(outside);
        const QString direct = album + "/direct.png"; fixture.save(direct);
        const auto bytes = readFile(direct);
        const auto trashed = PhotoTrashService::moveToTrash(direct,album);
        check(trashed.success && !QFileInfo::exists(direct), "real recycling moves fixture out of album");
        if (!trashed.trashPath.isEmpty()) {
            check(readFile(trashed.trashPath) == bytes, "recycle bin retains original file bytes");
        }
        check(!PhotoTrashService::moveToTrash(outside,album).success && readFile(outside) == externalBytes,
              "recycling rejects external file without modifying it");
        check(!PhotoTrashService::moveToTrash(album,album).success,
              "single photo deletion cannot recycle an album directory");
        const QString link = album + "/external-link.png";
        const bool linked = QFile::link(outside,link);
        if (linked) {
            const QFileInfo linkInfo(link);
            const auto linkResult = PhotoTrashService::moveToTrash(link,album);
            if (linkInfo.isSymLink() || linkInfo.isShortcut())
                check(!linkResult.success && QFileInfo::exists(outside), "recycling rejects link to external file");
            else
                check(linkResult.success && readFile(outside) == externalBytes,
                      "recycling album hard link preserves external file and bytes");
        }
#ifdef Q_OS_WIN
        const QString symlink = album + "/outside-symlink.png";
        if (CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(symlink.utf16()),
                               reinterpret_cast<LPCWSTR>(outside.utf16()), 0x2)) {
            check(!PhotoTrashService::moveToTrash(symlink,album).success && readFile(outside) == externalBytes,
                  "Windows symbolic link cannot delete external image");
            DeleteFileW(reinterpret_cast<LPCWSTR>(symlink.utf16()));
        }
#endif
        check(!PhotoTrashService::moveToTrash(album + "/missing.png",album).success,
              "missing file reports recycle failure");

        QSettings isolated("AlbumFeatureTests","AlbumApp"); isolated.clear();
        MainWindow window; window.resize(1280,800); window.show();
        auto *tree = window.findChild<ProTreeWidget*>();
        auto *wall = window.findChild<PhotoWallWidget*>();
        auto *view = wall->findChild<QListView*>();
        auto *editor = window.findChild<PicShow*>();
        auto *root = new ProTreeItem(tree,"trash album",album,TreeItemPro);
        tree->setCurrentItem(root);
        QString first, second, error;
        check(tree->RegisterMaterial(album,outside,&first,&error), "create album copy to recycle");
        const QString local = album + "/keep.png"; fixture.save(local);
        check(tree->RegisterMaterial(album,local,&second,&error), "register second recycle fixture");
        tree->SelectPhotoByPath(first); app.processEvents();
        auto *history = editor->findChild<QUndoStack*>();
        auto *slider = editor->findChildren<QSlider*>().first(); slider->setValue(25);
        const auto edited = editor->findChild<QLabel*>("label")->grab().toImage();
        const int historyIndex = history->index();
        auto *deleteButton = wall->findChild<QPushButton*>("photoWallDeleteButton");
        check(deleteButton && !deleteButton->isEnabled(), "wall delete disabled without selection");
        view->selectionModel()->select(view->model()->index(0,0), QItemSelectionModel::Select);
        check(deleteButton->isEnabled(), "wall delete enabled for selected material");
        const auto requestDeletion = [&](const QStringList &paths, bool confirm, bool discard,
                                         bool exportFirst = false, const QString &exportPath = QString()) {
            QTimer automation;
            QObject::connect(&automation,&QTimer::timeout,&window,[&]() {
                if (auto *dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                    if (exportPath.isEmpty()) dialog->reject();
                    else {
                        dialog->setOption(QFileDialog::DontConfirmOverwrite);
                        dialog->selectFile(exportPath);
                        QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
                    }
                } else if (auto *prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    if (prompt->objectName() == "photoTrashConfirmation") {
                        check(prompt->detailedText().contains(paths.first()), "delete confirmation identifies actual file path");
                        prompt->button(confirm ? QMessageBox::Yes : QMessageBox::Cancel)->click();
                    } else if (prompt->objectName() == "photoDeleteEditPrompt") {
                        if (exportFirst) {
                            auto *button = prompt->findChild<QPushButton*>("deleteExportButton");
                            button->click();
                        } else if (discard) prompt->findChild<QPushButton*>("deleteDiscardButton")->click();
                        else prompt->button(QMessageBox::Cancel)->click();
                    } else prompt->accept();
                }
            });
            automation.start(10);
            QMetaObject::invokeMethod(wall,"SigDeletePhotosRequested",Qt::DirectConnection,Q_ARG(QStringList,paths));
            app.processEvents();
        };
        requestDeletion({first},false,false);
        requestDeletion({first},true,false);
        check(QFileInfo::exists(first) && tree->VisiblePhotos(album).size() == 2
              && history->index() == historyIndex && editor->isModified()
              && editor->findChild<QLabel*>("label")->grab().toImage() == edited,
              "confirmation and dirty-edit cancellation preserve file nodes preview and history");
        // Use Qt's dialog for automated export cancellation; the native picker is manual acceptance.
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        requestDeletion({first},true,false,true);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,false);
        check(QFileInfo::exists(first) && editor->isModified() && history->index() == historyIndex,
              "cancel export-before-delete cancels deletion and preserves edits");
#ifdef Q_OS_WIN
        const HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(first.utf16()), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(locked != INVALID_HANDLE_VALUE, "lock fixture against deletion");
        if (locked != INVALID_HANDLE_VALUE) {
            requestDeletion({first},true,true);
            check(QFileInfo::exists(first) && tree->VisiblePhotos(album).size() == 2
                  && editor->isModified() && history->index() == historyIndex,
                  "recycle failure retains current edited photo and tree node");
            CloseHandle(locked);
        }
#endif
        const QString fakePath = temporary.filePath("not-in-tree.png"); fixture.save(fakePath);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        requestDeletion({first,fakePath},true,false,true,fakePath);
        check(QFileInfo::exists(first) && readFile(fakePath) == externalBytes && editor->isModified(),
              "export-before-delete rejects another file in deletion batch");
        const QString rescued = temporary.filePath("edited-copy-before-delete.png");
        requestDeletion({first,fakePath},true,false,true,rescued);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,false);
        ImageProcessor::Parameters savedParams; savedParams.brightness = 25;
        check(QImage(rescued) == ImageProcessor::Apply(fixture,savedParams),
              "export-before-delete preserves full edited pixels in separate copy");
        check(!QFileInfo::exists(first) && readFile(outside) == externalBytes
              && QFileInfo::exists(fakePath) && tree->VisiblePhotos(album).size() == 1,
              "batch partial failure removes only album copy and preserves external files");
        check(view->model()->rowCount() == 1 && editor->IsEditingPath(second) && !editor->isModified()
              && history->count() == 0, "deleting edited photo updates viewer history and integrated wall");
        auto *emptyMaterialFolder = root->child(0);
        check(emptyMaterialFolder && emptyMaterialFolder->type() == TreeItemDir && emptyMaterialFolder->isHidden()
              && QDir(QFileInfo(first).absolutePath()).exists() && QDir(QFileInfo(first).absolutePath()).isEmpty(),
              "last material deletion automatically hides real empty folder without deleting directory");
        auto *remaining = root->GetFirstPicChild();
        check(remaining && !remaining->GetPreItem() && !remaining->GetNextItem(), "deletion repairs photo navigation links");
        tree->SlotNextShow(); tree->SlotPreShow();
        check(editor->IsEditingPath(second), "navigation remains valid after deleting neighbor");
        root->setExpanded(true); app.processEvents();
        bool treeDeleteTriggered = false;
        QTimer treeAutomation;
        QTimer treeConfirmAutomation;
        QObject::connect(&treeConfirmAutomation,&QTimer::timeout,&window,[&]() {
            if (auto *prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                check(prompt->grab().save(outputDir + "/material-delete-confirmation.png"), "delete confirmation screenshot");
                prompt->button(QMessageBox::Yes)->click();
            }
        });
        treeConfirmAutomation.start(10);
        QObject::connect(&treeAutomation,&QTimer::timeout,&window,[&]() {
            for (auto *widget : QApplication::topLevelWidgets()) {
                auto *menu = qobject_cast<QMenu*>(widget);
                if (!menu || !menu->isVisible() || treeDeleteTriggered) continue;
                for (auto *action : menu->actions()) {
                    if (action->objectName() == "treeTrashPhotoAction") {
                        treeDeleteTriggered = true;
                        action->trigger();
                        menu->close();
                        return;
                    }
                }
            }
        });
        treeAutomation.start(10);
        QTimer treeWatchdog;
        treeWatchdog.setSingleShot(true);
        QObject::connect(&treeWatchdog,&QTimer::timeout,&window,[]() {
            if (auto *dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
            if (auto *menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) menu->close();
        });
        treeWatchdog.start(5000);
        tree->scrollToItem(remaining); app.processEvents();
        QTest::mousePress(tree->viewport(),Qt::RightButton,Qt::NoModifier,tree->visualItemRect(remaining).center());
        QTest::mouseRelease(tree->viewport(),Qt::RightButton,Qt::NoModifier,tree->visualItemRect(remaining).center());
        treeAutomation.stop(); treeConfirmAutomation.stop(); treeWatchdog.stop();
        app.processEvents();
        check(treeDeleteTriggered && !QFileInfo::exists(second) && view->model()->rowCount() == 0 && !editor->isModified()
              && !editor->findChild<QPushButton*>("photoCompareButton")->isEnabled(),
              "deleting last photo clears editor and wall without dangling selection");
        tree->clear(); tree->SlotOpenPro(album); app.processEvents();
        check(tree->VisiblePhotos(album).isEmpty() && view->model()->rowCount() == 0,
              "deleted materials do not return when album is reopened");
        isolated.clear();
    }
    {
        QSettings isolated("AlbumFeatureTests","AlbumApp"); isolated.clear();
        const QString album = temporary.filePath("folder-management-album"); QDir().mkpath(album);
        const QString outside = temporary.filePath("folder-external.png");
        QImage fixture(64,48,QImage::Format_RGB32); fixture.fill(Qt::green); fixture.save(outside);
        const auto originalBytes = readFile(outside);
        check(!PhotoTrashService::moveFolderToTrash(album,album).success
              && !PhotoTrashService::moveFolderToTrash(temporary.path(),album).success,
              "folder recycling rejects album root and external directory");
        MainWindow window; window.resize(1280,800); window.show();
        auto *tree = window.findChild<ProTreeWidget*>();
        auto *wall = window.findChild<PhotoWallWidget*>();
        auto *view = wall->findChild<QListView*>();
        auto *editor = window.findChild<PicShow*>();
        auto *root = new ProTreeItem(tree,"folder album",album,TreeItemPro);
        tree->setCurrentItem(root);
        QString material, error;
        check(tree->RegisterMaterial(album,outside,&material,&error), "register managed-folder fixture");
        const QString folderPath = QFileInfo(material).absolutePath();
        auto *folder = root->child(0);
        QFile notes(folderPath + "/.notes.txt"); notes.open(QIODevice::WriteOnly); notes.write("keep non-image data"); notes.close();
#ifdef Q_OS_WIN
        SetFileAttributesW(reinterpret_cast<LPCWSTR>(notes.fileName().utf16()), FILE_ATTRIBUTE_HIDDEN);
#endif
        check(tree->TrashPhoto(material,&error) && !folder->isHidden() && QFileInfo::exists(notes.fileName()),
              "non-image and hidden files prevent empty material directory classification");
        check(tree->RegisterMaterial(album,outside,&material,&error), "register material again in retained directory");
        tree->SelectPhotoByPath(material); app.processEvents();
        auto *history = editor->findChild<QUndoStack*>();
        editor->findChildren<QSlider*>().first()->setValue(25);
        const int originalHistory = history->index();
        const auto originalPreview = editor->findChild<QLabel*>("label")->grab().toImage();

        const auto triggerMenu = [&](QTreeWidgetItem *item, const QString &actionName) {
            bool triggered = false;
            QTimer automation;
            QObject::connect(&automation,&QTimer::timeout,&window,[&]() {
                for (auto *widget : QApplication::topLevelWidgets()) {
                    auto *menu = qobject_cast<QMenu*>(widget);
                    if (!menu || !menu->isVisible() || triggered) continue;
                    for (auto *action : menu->actions()) if (action->objectName() == actionName) {
                        triggered = true;
                        if (actionName == "treeHideFolderAction")
                            check(menu->grab().save(outputDir + "/material-folder-menu.png"), "folder context-menu screenshot");
                        action->trigger(); menu->close(); return;
                    }
                }
            });
            automation.start(10);
            QTimer watchdog;
            watchdog.setSingleShot(true);
            QObject::connect(&watchdog,&QTimer::timeout,&window,[]() {
                if (auto *dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
                if (auto *menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) menu->close();
            });
            watchdog.start(5000);
            root->setExpanded(true); tree->scrollToItem(item); app.processEvents();
            QTest::mousePress(tree->viewport(),Qt::RightButton,Qt::NoModifier,tree->visualItemRect(item).center());
            QTest::mouseRelease(tree->viewport(),Qt::RightButton,Qt::NoModifier,tree->visualItemRect(item).center());
            automation.stop(); watchdog.stop(); app.processEvents();
            check(triggered, "folder management action available through actual right-click menu");
        };
        triggerMenu(folder,"treeHideFolderAction");
        check(folder->isHidden() && view->model()->rowCount() == 0 && QFileInfo::exists(material)
              && editor->isModified() && history->index() == originalHistory,
              "hide folder preserves disk and dirty editor and removes wall photos");
        tree->SlotSearchTextChanged("png"); tree->SlotSearchTextChanged("");
        check(folder->isHidden(), "manual folder hiding survives search changes");
        const bool registeredAgain = tree->RegisterMaterial(album,outside,&material,&error);
        app.processEvents(); // 照片墙通过排队连接接收集合变更。
        check(registeredAgain && !folder->isHidden()
              && view->model()->rowCount() == 1 && history->index() == originalHistory,
              "registering existing material unhides directory without duplicating or discarding edits");
        tree->HideFolder(folderPath);
        triggerMenu(root,"treeShowFoldersAction");
        check(!folder->isHidden() && view->model()->rowCount() == 1 && editor->isModified(),
              "album menu restores hidden directory and photos without closing album");
        auto *alsoOpened = new ProTreeItem(tree,"separately opened folder",folderPath,TreeItemPro);
        check(!tree->TrashFolder(folderPath,&error) && QFileInfo::exists(material),
              "folder recycling requires closing independently opened overlapping album first");
        delete tree->takeTopLevelItem(tree->indexOfTopLevelItem(alsoOpened));
#ifdef Q_OS_WIN
        const QString alias = folderPath + "/external-alias.png";
        if (CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(alias.utf16()),
                               reinterpret_cast<LPCWSTR>(outside.utf16()),0x2)) {
            check(!PhotoTrashService::moveFolderToTrash(folderPath,album).success
                  && QFileInfo::exists(material) && readFile(outside) == originalBytes,
                  "folder containing external alias is rejected without touching files");
            DeleteFileW(reinterpret_cast<LPCWSTR>(alias.utf16()));
        }
#endif

        bool confirm = false, discard = false, exportFirst = false;
        QString exportPath;
        QTimer prompts;
        QObject::connect(&prompts,&QTimer::timeout,&window,[&]() {
            if (auto *dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                dialog->setOption(QFileDialog::DontConfirmOverwrite);
                dialog->selectFile(exportPath);
                QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
            } else if (auto *prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                if (prompt->objectName() == "folderTrashConfirmation") {
                    check(prompt->informativeText().contains(QDir::toNativeSeparators(folderPath))
                          && prompt->text().contains(QStringLiteral("非图片")), "folder confirmation includes path and all-file warning");
                    check(prompt->grab().save(outputDir + "/material-folder-delete-confirmation.png"), "folder deletion confirmation screenshot");
                    prompt->button(confirm ? QMessageBox::Yes : QMessageBox::Cancel)->click();
                } else if (prompt->objectName() == "photoDeleteEditPrompt") {
                    if (exportFirst) prompt->findChild<QPushButton*>("deleteExportButton")->click();
                    else if (discard) prompt->findChild<QPushButton*>("deleteDiscardButton")->click();
                    else prompt->button(QMessageBox::Cancel)->click();
                } else prompt->accept();
            }
        });
        prompts.start(10);
        triggerMenu(folder,"treeTrashFolderAction");
        confirm = true;
        QMetaObject::invokeMethod(tree,"SigDeleteFolderRequested",Qt::DirectConnection,Q_ARG(QString,folderPath));
        check(QFileInfo::exists(material) && editor->isModified() && history->index() == originalHistory
              && editor->findChild<QLabel*>("label")->grab().toImage() == originalPreview,
              "cancel folder confirmation or dirty prompt preserves directory and editor");
#ifdef Q_OS_WIN
        const HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(material.utf16()), GENERIC_READ,
            FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(locked != INVALID_HANDLE_VALUE, "lock folder child against recycling");
        if (locked != INVALID_HANDLE_VALUE) {
            discard = true;
            QMetaObject::invokeMethod(tree,"SigDeleteFolderRequested",Qt::DirectConnection,Q_ARG(QString,folderPath));
            check(QFileInfo::exists(material) && QFileInfo::exists(notes.fileName()) && root->childCount() == 1
                  && history->index() == originalHistory && editor->isModified(),
                  "folder recycle failure preserves subtree non-image file and edited history");
            CloseHandle(locked);
        }
#endif
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
        exportFirst = true; exportPath = folderPath + "/unsafe-rescue.png";
        QMetaObject::invokeMethod(tree,"SigDeleteFolderRequested",Qt::DirectConnection,Q_ARG(QString,folderPath));
        check(QFileInfo::exists(material) && !QFileInfo::exists(exportPath) && editor->isModified(),
              "rescue export cannot be written inside directory being recycled");
        exportPath = temporary.filePath("safe-folder-rescue.png");
        QMetaObject::invokeMethod(tree,"SigDeleteFolderRequested",Qt::DirectConnection,Q_ARG(QString,folderPath));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,false);
        prompts.stop(); app.processEvents();
        ImageProcessor::Parameters parameters; parameters.brightness = 25;
        check(!QFileInfo::exists(folderPath) && root->childCount() == 0 && view->model()->rowCount() == 0
              && history->count() == 0 && !editor->isModified() && readFile(outside) == originalBytes
              && QImage(exportPath) == ImageProcessor::Apply(fixture,parameters),
              "folder recycle removes entire subtree preserves external source and exports edited rescue");
        tree->clear(); tree->SlotOpenPro(album); app.processEvents();
        check(tree->topLevelItemCount() == 1 && tree->topLevelItem(0)->childCount() == 0,
              "reopening album does not recreate recycled material folder");
        check(tree->RegisterMaterial(album,outside,&material,&error) && QFileInfo::exists(material)
              && !tree->topLevelItem(0)->child(0)->isHidden(),
              "saving another material recreates directory and visible tree node");
        auto *reopenedRoot = tree->topLevelItem(0);
        tree->HideFolder(folderPath);
        tree->ShowHiddenFolders(album);
        check(tree->TrashPhoto(material,&error) && !reopenedRoot->child(0)->isHidden(),
              "explicit show-empty choice keeps empty material folder accessible for management");
        check(tree->TrashFolder(folderPath,&error) && !QFileInfo::exists(folderPath)
              && reopenedRoot->childCount() == 0,
              "empty material folder can be recycled independently of parent album");
        isolated.clear();
    }
    {
        const QString album = temporary.filePath("material-album");
        QDir().mkpath(album);
        const QString sourcePath = album + "/source.png";
        QImage source(140,100,QImage::Format_ARGB32);
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
                source.setPixelColor(x,y,QColor(x,y,40,150));
        check(source.save(sourcePath), "create material source");
        const auto originalBytes = readFile(sourcePath);
        PicShow viewer; viewer.resize(1200,800); viewer.show(); viewer.SlotSelectItem(sourcePath);
        viewer.SetMaterialAlbumRoot(album);
        app.processEvents();
        auto *history = viewer.findChild<QUndoStack*>();
        auto *saveButton = viewer.findChild<QPushButton*>("photoSaveMaterialButton");
        check(saveButton && !saveButton->isEnabled(), "material save unavailable before crop");
        const auto crop = [&](const QRect &rect) {
            QTimer::singleShot(20, &viewer, [&, rect]() {
                if (auto *dialog = qobject_cast<CropDialog*>(QApplication::activeModalWidget())) {
                    dialog->findChild<CropCanvas*>()->setSelection(rect);
                    dialog->accept();
                }
            });
            QMetaObject::invokeMethod(&viewer,"SlotCrop",Qt::DirectConnection);
        };
        crop(QRect(5,8,60,40));
        viewer.findChildren<QSlider*>().first()->setValue(15);
        const int historyIndex = history->index(), historyCount = history->count();
        const auto editedPreview = viewer.findChild<QLabel*>("label")->grab().toImage();
        const QString first = MaterialSaveDialog::suggestedPath(sourcePath);
        check(first.endsWith("source_crop_001.png") && saveButton->isEnabled(), "crop enables material action and numbered PNG name");
        QString emittedPath, emittedAlbum;
        int notifications = 0;
        QObject::connect(&viewer, &PicShow::SigMaterialSaved, &viewer, [&](const QString &path, const QString &root) {
            emittedPath = path; emittedAlbum = root; ++notifications;
        });
        const auto save = [&](bool accept, bool add, const QString &path) {
            QTimer automation;
            QObject::connect(&automation, &QTimer::timeout, &viewer, [&]() {
                if (auto *dialog = qobject_cast<MaterialSaveDialog*>(QApplication::activeModalWidget())) {
                    if (!accept) { dialog->reject(); return; }
                    dialog->findChild<QLineEdit*>("materialOutputPath")->setText(path);
                    dialog->findChild<QCheckBox*>("materialAddToAlbum")->setChecked(add);
                    dialog->accept();
                } else if (auto *message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    if (message->standardButtons() & QMessageBox::No) message->done(QMessageBox::No);
                    else message->accept();
                }
            });
            automation.start(10);
            QMetaObject::invokeMethod(&viewer,"SlotSaveMaterial",Qt::DirectConnection);
        };
        save(false, false, first);
        check(!QFileInfo::exists(first) && history->index() == historyIndex, "cancel material save keeps crop and history");
        save(true, true, first);
        ImageProcessor::Parameters params; params.brightness = 15;
        check(QImage(first) == ImageProcessor::Apply(source.copy(QRect(5,8,60,40)), params)
              && emittedPath == first && emittedAlbum == album && notifications == 1,
              "material save writes full crop colors alpha and optional album notification");
        check(history->index() == historyIndex && history->count() == historyCount && viewer.isModified()
              && viewer.findChild<QLabel*>("label")->grab().toImage() == editedPreview,
              "material save preserves document preview history and modification state");
        save(true, true, temporary.filePath("missing-material-folder/fail.png"));
        check(notifications == 1 && history->index() == historyIndex
              && !QFileInfo::exists(temporary.filePath("missing-material-folder/fail.png")),
              "material write failure preserves edits and emits no registration");
        save(true, true, first);
        check(notifications == 1, "declining replacement does not register material again");
        const QString second = MaterialSaveDialog::suggestedPath(sourcePath);
        check(second.endsWith("source_crop_002.png"), "material default advances past existing files");
        QMetaObject::invokeMethod(&viewer,"SlotUndo",Qt::DirectConnection); // color
        QMetaObject::invokeMethod(&viewer,"SlotUndo",Qt::DirectConnection); // crop
        crop(QRect(70,20,50,60));
        save(true, false, second);
        check(QImage(second) == source.copy(QRect(70,20,50,60)) && notifications == 1
              && readFile(sourcePath) == originalBytes, "extract second independent region without changing original or registering unchecked export");
        save(true, true, sourcePath);
        check(readFile(sourcePath) == originalBytes, "material UI cannot overwrite original");
        viewer.Clear();
        check(!saveButton->isEnabled(), "clear disables material action");
        viewer.hide();

        QSettings isolated(QStringLiteral("AlbumFeatureTests"), QStringLiteral("AlbumApp")); isolated.clear();
        MainWindow materialWindow;
        materialWindow.resize(1200,800);
        auto &tree = *materialWindow.findChild<ProTreeWidget*>();
        auto *materialWall = materialWindow.findChild<PhotoWallWidget*>();
        auto *root = new ProTreeItem(&tree, "material album", album, TreeItemPro);
        auto *original = new ProTreeItem(root,"source",sourcePath,root,TreeItemPic);
        tree.setCurrentItem(original);
        QString registered, error;
        check(tree.AlbumRootForPhoto(sourcePath) == album, "resolve material album by source rather than unrelated tree selection");
        check(tree.RegisterMaterial(album, first, &registered, &error)
              && tree.VisiblePhotos(album).size() == 2 && tree.currentItem() == original,
              "register local material adds node without switching editor selection");
        app.processEvents();
        check(materialWall->findChild<QListView*>()->model()->rowCount() == 2,
              "material registration refreshes integrated photo wall");
        tree.SelectPhotoByPath(sourcePath);
        auto *integratedViewer = materialWindow.findChild<PicShow*>();
        QTimer::singleShot(20, integratedViewer, [&]() {
            if (auto *dialog = qobject_cast<CropDialog*>(QApplication::activeModalWidget())) {
                dialog->findChild<CropCanvas*>()->setSelection(QRect(10,10,50,40));
                dialog->accept();
            }
        });
        QMetaObject::invokeMethod(integratedViewer,"SlotCrop",Qt::DirectConnection);
        bool albumOptionAvailable = false;
        QTimer::singleShot(20, integratedViewer, [&]() {
            if (auto *dialog = qobject_cast<MaterialSaveDialog*>(QApplication::activeModalWidget())) {
                albumOptionAvailable = dialog->findChild<QCheckBox*>("materialAddToAlbum")->isEnabled();
                check(dialog->grab().save(outputDir + "/material-save-dialog.png"), "material save dialog screenshot");
                dialog->reject();
            }
        });
        QMetaObject::invokeMethod(integratedViewer,"SlotSaveMaterial",Qt::DirectConnection);
        check(albumOptionAvailable, "main window loads source album context into material dialog");
        check(tree.RegisterMaterial(album, first, &registered, &error) && tree.VisiblePhotos(album).size() == 2,
              "material registration deduplicates existing path");
        const QString external = temporary.filePath("external.png"); source.save(external);
        check(tree.RegisterMaterial(album, external, &registered, &error)
              && registered.startsWith(album + "/") && readFile(registered) == readFile(external),
              "external material copied into album for persistent reopening");
        const QString copied = registered;
        check(tree.RegisterMaterial(album, external, &registered, &error) && registered == copied
              && tree.VisiblePhotos(album).size() == 3, "repeated external registration reuses identical copy");
        source.fill(Qt::red); source.save(external);
        check(tree.RegisterMaterial(album, external, &registered, &error) && registered != copied
              && QImage(copied) != QImage(registered), "different external content avoids same-name overwrite");
        auto *photo = root->GetFirstPicChild();
        int traversed = 0; ProTreeItem *previous = nullptr;
        while (photo && traversed < 10) {
            check(photo->GetPreItem() == previous, "registered materials have consistent previous links");
            previous = photo; photo = photo->GetNextItem(); ++traversed;
        }
        check(traversed == 4 && !photo, "all material nodes participate in navigation");
        check(!tree.RegisterMaterial(album + "/closed", first, &registered, &error)
              && !error.isEmpty() && QFileInfo::exists(first), "registration failure preserves saved material");
        tree.clear();
        tree.SlotOpenPro(album);
        check(tree.VisiblePhotos(album).size() == 5, "reopen scans saved materials including unregistered on-disk export");
        isolated.clear();
    }
    {
        QImage source(1900, 1000, QImage::Format_ARGB32);
        source.fill(QColor(30, 80, 150, 120));
        const QString sourcePath = temporary.filePath("export-source.png");
        check(source.save(sourcePath), "create full resolution export source");
        const auto sourceBytes = readFile(sourcePath);
        PhotoEditDocument document;
        check(document.load(sourcePath), "load export document");
        auto state = document.state();
        state.crop = QRect(30, 50, 1800, 800);
        state.rotation = 90;
        state.flipHorizontal = true;
        state.flipVertical = true;
        state.parameters = {15, -10, 20, ImageProcessor::FilterInvert};
        document.setState(state);
        const QImage expected = document.Render();
        check(document.Render(1600).size() != expected.size(), "export fixture has smaller drag preview");
        const QString png = temporary.filePath("完整尺寸.PNG");
        auto result = ImageExportService::exportDocument(document, png);
        check(result.success && QImage(png) == expected && QImage(png).size() == QSize(800,1800),
              "PNG export preserves full dimensions transformed pixels colors and alpha");
        check(document.state() == state && document.isModified(), "export preserves document edits and content modified state");
        auto next = state; next.parameters.brightness = -30; document.setState(next);
        result = ImageExportService::exportDocument(document, png);
        check(result.success && QImage(png) == document.Render(), "safe export replaces existing output successfully");
        for (const auto &path : {sourcePath, temporary.path() + "/./export-source.png"}) {
            result = ImageExportService::exportDocument(document, path);
            check(!result.success && readFile(sourcePath) == sourceBytes, "export rejects source path and normalized alias");
        }
#ifdef Q_OS_WIN
        result = ImageExportService::exportDocument(document, sourcePath.toUpper());
        check(!result.success && readFile(sourcePath) == sourceBytes, "source protection ignores Windows path case");
#endif
        const QString unsupported = temporary.filePath("existing.gif");
        QFile sentinel(unsupported); sentinel.open(QIODevice::WriteOnly); sentinel.write("keep-existing"); sentinel.close();
        result = ImageExportService::exportDocument(document, unsupported);
        check(!result.success && readFile(unsupported) == QByteArray("keep-existing"), "unsupported format preserves existing output");
        const auto previous = readFile(png);
        result = ImageExportService::writeImage({}, png, {sourcePath});
        check(!result.success && readFile(png) == previous, "empty image failure preserves existing output");
        check(!ImageExportService::exportDocument(document, temporary.filePath("missing/output.png")).success,
              "invalid destination reports failure");
        const QString directoryTarget = temporary.filePath("directory.png");
        QDir().mkpath(directoryTarget);
        QFile marker(directoryTarget + "/keep"); marker.open(QIODevice::WriteOnly); marker.write("keep"); marker.close();
        check(!ImageExportService::exportDocument(document, directoryTarget).success
              && readFile(directoryTarget + "/keep") == QByteArray("keep"), "failed target replacement preserves directory contents");
        QImage transparent(64,64,QImage::Format_ARGB32); transparent.fill(Qt::transparent);
        const QString jpeg = temporary.filePath("transparent.jpeg");
        result = ImageExportService::writeImage(transparent, jpeg, {});
        const QImage decoded(jpeg);
        check(result.success && decoded.size() == transparent.size() && !decoded.hasAlphaChannel()
              && decoded.pixelColor(32,32) == QColor(Qt::white), "JPEG transparent pixels use white background");
        ImageExportService::Options options; options.jpegQuality = 100; options.jpegBackground = QColor(40,80,120);
        result = ImageExportService::writeImage(transparent, jpeg, {}, options);
        const QColor background = QImage(jpeg).pixelColor(32,32);
        check(result.success && qAbs(background.red()-40) <= 3 && qAbs(background.green()-80) <= 3
              && qAbs(background.blue()-120) <= 3, "JPEG honors quality and opaque background options");
        const auto jpegBefore = readFile(jpeg); options.jpegQuality = 101;
        check(!ImageExportService::writeImage(transparent, jpeg, {}, options).success && readFile(jpeg) == jpegBefore,
              "invalid JPEG options preserve existing file");
        const QString bmp = temporary.filePath("export.bmp");
        check(ImageExportService::writeImage(decoded, bmp, {}).success && QImage(bmp) == decoded,
              "BMP export remains available");
        check(readFile(sourcePath) == sourceBytes, "all export paths preserve source bytes");
    }
    {
        QImage input(5, 5, QImage::Format_ARGB32);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x) input.setPixel(x, y, qRgba(x * 40, y * 40, 60, 80 + x));
        check(ImageProcessor::Apply(input, {}) == input, "image defaults preserve pixels");
        ImageProcessor::Parameters parameters;
        parameters.filter = ImageProcessor::FilterInvert;
        const QImage inverted = ImageProcessor::Apply(input, parameters);
        check(inverted.pixel(2, 2) == qRgba(175, 175, 195, 82), "image inversion preserves alpha");
        bool filtersValid = true;
        for (int filter = 0; filter <= ImageProcessor::FilterEmboss; ++filter) {
            parameters = {15, -10, 20, static_cast<ImageProcessor::FilterType>(filter)};
            const QImage processed = ImageProcessor::Apply(input, parameters);
            filtersValid &= processed.size() == input.size() && qAlpha(processed.pixel(2, 2)) == 82;
        }
        check(filtersValid && ImageProcessor::Apply({}, {}).isNull(), "all image filters retain dimensions and transparency");
        const QVector<DuplicatePhotoFinder::PhotoHash> hashes = {
            {"a", "album1", 0}, {"b", "album1", 1}, {"c", "album2", 0}, {"a", "album1", 0}};
        const auto matches = DuplicatePhotoFinder::FindMatches(hashes);
        check(matches.size() == 1 && matches[0].keepIndex == 0 && matches[0].duplicateIndex == 1
              && matches[0].distance == 1, "duplicate matching preserves album and path boundaries");
        check(DuplicatePhotoFinder::HammingDistance(0, ~quint64(0)) == 64, "hash distance covers all 64 bits");
        const QString organization = QStringLiteral("AlbumRefactorTests-%1").arg(QCoreApplication::applicationPid());
        QVariantMap values{{"opened_projects", QStringList{"one", "two"}}, {"music_volume", 37}};
        WorkspaceSettings::Save(values, organization);
        check(WorkspaceSettings::Load(organization) == values, "workspace settings round trip preserves key types");
        QSettings(organization, QStringLiteral("AlbumApp")).clear();
        const QString invalidAudio = temporary.filePath(QStringLiteral("renamed.mp3"));
        { QFile file(invalidAudio); file.open(QIODevice::WriteOnly); QByteArray bytes(2048, '\0');
          bytes.replace(4, 4, "ftyp"); file.write(bytes); }
        check(!AudioFileProbe::IsAudioFileValid(invalidAudio), "audio probe rejects disguised MP4");
        QWidget owner;
        MusicController controller(&owner);
        int dirtyCount = 0;
        QObject::connect(&controller, &MusicController::SettingsDirty, &owner, [&]() { ++dirtyCount; });
        controller.SetMusicVolume(137);
        controller.SetPlayImmediately(false);
        controller.SetAutoPlayOnSlideshow(false);
        controller.SetAutoStopOnSlideshowExit(false);
        QVariantMap stored;
        controller.WriteSettings(stored);
        check(controller.GetMusicVolume() == 100 && stored.value("music_volume").toInt() == 100
              && !stored.value("music_play_immediately").toBool() && dirtyCount == 4,
              "music controller persists clamped volume and playback policies");
        QTimer::singleShot(100, &owner, []() {
            if (auto *dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->accept();
        });
        controller.SlotSetMusic();
        check(!QApplication::activeModalWidget(), "extracted music settings opens and closes");
        controller.SlotPauseMusic();
        controller.SlotStopMusic();
        check(!controller.IsMusicPlaying(), "music stop leaves controller idle");
    }

    {
        QImage source(80, 60, QImage::Format_ARGB32);
        source.fill(qRgba(70, 100, 130, 255));
        const QString first = temporary.filePath("document-first.png");
        const QString second = temporary.filePath("document-second.png");
        check(source.save(first) && source.save(second), "create editor fixtures");
        PhotoEditDocument document;
        check(document.load(first) && document.Render() == source, "document loads original without changes");
        document.setParameters({25, -15, 30, ImageProcessor::FilterInvert});
        check(document.Render() == ImageProcessor::Apply(source, document.parameters())
              && document.sourceImage() == source, "document renders edits without changing original");
        check(document.load(second) && document.parameters().brightness == 0
              && document.parameters().filter == ImageProcessor::FilterNone,
              "switching source resets document parameters");
        auto parameters = document.parameters();
        parameters.brightness = 50;
        document.setParameters(parameters);
        check(!document.load(temporary.filePath("missing.png"))
              && document.sourceImage().isNull() && document.sourcepath().isEmpty()
              && document.Render().isNull() && document.parameters().brightness == 0,
              "failed load clears old source and edits");
        document.load(first);
        parameters = document.parameters();
        parameters.brightness = 50;
        document.setParameters(parameters);
        document.clear();
        check(document.sourceImage().isNull() && document.Render().isNull()
              && document.sourcepath().isEmpty() && document.parameters().brightness == 0,
              "clear removes all document data");

        PicShow viewer;
        viewer.resize(1100, 750);
        viewer.show();
        viewer.SlotSelectItem(first);
        app.processEvents();
        auto *canvas = viewer.findChild<QLabel*>("label");
        const auto sliders = viewer.findChildren<QSlider*>();
        auto *filter = viewer.findChild<QComboBox*>("studioCombo");
        check(canvas && sliders.size() == 3 && filter, "editor controls available");
        check(viewer.findChild<PhotoCanvas*>("label") && viewer.findChild<PhotoAdjustmentPanel*>()
              && viewer.grab().save(outputDir + "/editor-components.png"),
              "editor embeds independent canvas and adjustment panel with retained screenshot");
        if (canvas && sliders.size() == 3 && filter) {
            const auto centerPixel = [&]() {
                const QImage rendered = canvas->grab().toImage();
                return rendered.pixelColor(rendered.width() / 2, rendered.height() / 2);
            };
            const QColor original = centerPixel();
            bool adjustmentsChangePixels = true;
            for (auto *slider : sliders) {
                slider->setValue(40);
                adjustmentsChangePixels &= centerPixel() != original;
                QMetaObject::invokeMethod(&viewer, "SlotResetAlgorithms", Qt::DirectConnection);
                adjustmentsChangePixels &= slider->value() == 0 && centerPixel() == original;
            }
            check(adjustmentsChangePixels, "all sliders affect rendered pixels and reset restores source");
            filter->setCurrentIndex(filter->findData(ImageProcessor::FilterInvert));
            check(centerPixel() != original, "filter affects document-driven preview");
            viewer.SlotUpdatePic(second);
            check(filter->currentData().toInt() == ImageProcessor::FilterNone
                  && centerPixel() == original, "switching image clears filter and rendered effect");
            sliders.first()->setValue(40);
            viewer.Clear();
            const QColor empty = centerPixel();
            check(empty != original && sliders.first()->value() == 0,
                  "clear removes preview and resets controls");
            viewer.SlotSelectItem(first);
            sliders.first()->setValue(40);
            viewer.SlotSelectItem(temporary.filePath("missing.png"));
            check(centerPixel() == empty && sliders.first()->value() == 0,
                  "failed viewer load removes stale preview");
            viewer.SlotSelectItem(first);
            check(centerPixel() == original, "viewer recovers after failed load");
        }

        if (canvas && sliders.size() == 3 && filter) {
            auto *history = viewer.findChild<QUndoStack*>("photoUndoStack");
            auto *undoButton = viewer.findChild<QPushButton*>("photoUndoButton");
            auto *redoButton = viewer.findChild<QPushButton*>("photoRedoButton");
            const auto invoke = [&](const char *slot) {
                return QMetaObject::invokeMethod(&viewer, slot, Qt::DirectConnection);
            };
            viewer.SlotSelectItem(first);
            check(history && undoButton && redoButton && !undoButton->isEnabled()
                  && !redoButton->isEnabled(), "history starts empty with disabled buttons");
            if (history && undoButton && redoButton) {
                const auto preview = [&]() { return canvas->grab().toImage(); };
                const QImage original = preview();
                // Simulate a slider gesture with multiple live preview updates.
                sliders.first()->setSliderDown(true);
                sliders.first()->setValue(10);
                sliders.first()->setValue(25);
                sliders.first()->setValue(40);
                sliders.first()->setSliderDown(false);
                const QImage adjusted = preview();
                check(history->count() == 1 && adjusted != original && undoButton->isEnabled(),
                      "one slider drag produces one history command");
                undoButton->click();
                check(sliders.first()->value() == 0 && preview() == original
                      && redoButton->isEnabled(), "undo restores controls and rendered pixels");
                redoButton->click();
                check(sliders.first()->value() == 40 && preview() == adjusted,
                      "redo restores slider result");
                filter->setCurrentIndex(filter->findData(ImageProcessor::FilterInvert));
                const QImage filtered = preview();
                invoke("SlotResetAlgorithms");
                check(preview() == original && filter->currentData().toInt() == ImageProcessor::FilterNone,
                      "reset restores defaults");
                invoke("SlotUndo");
                check(preview() == filtered && sliders.first()->value() == 40,
                      "reset itself can be undone");
                invoke("SlotUndo"); // filter
                invoke("SlotUndo"); // slider
                check(preview() == original && history->index() == 0, "multiple undo reaches original");
                sliders.first()->setValue(-20);
                check(!history->canRedo() && history->count() == 1, "new branch discards redo history");
                const int count = history->count();
                sliders.first()->setSliderDown(true);
                sliders.first()->setValue(20);
                sliders.first()->setValue(-20);
                sliders.first()->setSliderDown(false);
                check(history->count() == count, "drag returning to start creates no command");
                viewer.SlotSelectItem(second);
                check(history->count() == 0 && !undoButton->isEnabled() && !redoButton->isEnabled(),
                      "image switch clears history");

                viewer.activateWindow();
                sliders.first()->setFocus();
                app.processEvents();
                QTest::keyClick(sliders.first(), Qt::Key_Right);
                const int steppedValue = sliders.first()->value();
                QTest::keyClick(sliders.first(), Qt::Key_Z, Qt::ControlModifier);
                check(steppedValue != 0 && sliders.first()->value() == 0,
                      "Ctrl+Z works while slider has focus");
                QTest::keyClick(sliders.first(), Qt::Key_Y, Qt::ControlModifier);
                check(sliders.first()->value() == steppedValue,
                      "Ctrl+Y restores edit while slider has focus");
                QTest::keyClick(sliders.first(), Qt::Key_Z, Qt::ControlModifier);
                QTest::keyClick(sliders.first(), Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
                check(sliders.first()->value() == steppedValue, "Ctrl+Shift+Z also redoes edit");
                // Use an asymmetric image to verify rotation and mirror history.
                QImage geometry(80, 60, QImage::Format_RGB32);
                for (int y = 0; y < geometry.height(); ++y)
                    for (int x = 0; x < geometry.width(); ++x)
                        geometry.setPixelColor(x, y, QColor(x * 3, y * 4, (x + y) % 256));
                const QString geometryPath = temporary.filePath("history-geometry.png");
                check(geometry.save(geometryPath), "create asymmetric history fixture");
                viewer.SlotSelectItem(geometryPath);
                for (const char *slot : {"SlotRotateLeft", "SlotRotateRight", "SlotFlipH", "SlotFlipV"}) {
                    const QImage before = preview();
                    invoke(slot);
                    const QImage after = preview();
                    check(after != before, "geometry edit changes preview");
                    invoke("SlotUndo");
                    check(preview() == before, "geometry undo restores preview");
                    invoke("SlotRedo");
                    check(preview() == after, "geometry redo restores preview");
                }
                viewer.Clear();
                check(history->count() == 0, "clear removes history");
                viewer.SlotSelectItem(first);
                sliders.first()->setValue(30);
                viewer.SlotSelectItem(temporary.filePath("history-missing.png"));
                check(history->count() == 0 && !history->canUndo(), "failed image load clears history");
            }
        }
        viewer.hide();
    }


    {
        QImage image(12, 8, QImage::Format_RGB32); image.fill(Qt::red);
        const QString path = temporary.filePath("modified-state.png");
        check(image.save(path), "create modification fixture");
        PhotoEditDocument doc;
        check(!doc.isModified() && doc.load(path) && !doc.isModified(), "empty and loaded document are unmodified");
        const auto baseline = doc.state();
        for (int edit = 0; edit < 7; ++edit) {
            auto state = baseline;
            if (edit == 0) state.parameters.brightness = 10;
            if (edit == 1) state.parameters.contrast = 10;
            if (edit == 2) state.parameters.saturation = 10;
            if (edit == 3) state.parameters.filter = ImageProcessor::FilterInvert;
            if (edit == 4) state.rotation = 90;
            if (edit == 5) state.flipHorizontal = true;
            if (edit == 6) state.flipVertical = true;
            doc.setState(state);
            check(doc.isModified(), "each edit kind changes modification state");
            doc.setState(baseline);
            check(!doc.isModified(), "restoring baseline clears modification state");
        }
        auto state = baseline; state.rotation = 90; state.parameters.brightness = 30;
        doc.setState(state); doc.setParameters({});
        check(doc.isModified(), "resetting color retains geometry modification");
        doc.clear(); check(!doc.isModified(), "clear resets modification state");
        doc.load(path); state = baseline; state.rotation = 90; doc.setState(state);
        check(doc.load(path) && !doc.isModified(), "reloading resets modification baseline");
        doc.setState(state); doc.load(temporary.filePath("missing-dirty.png"));
        check(!doc.isModified(), "failed load clears modification baseline");
        PicShow viewer; viewer.SlotSelectItem(path);
        const auto sliders = viewer.findChildren<QSlider*>();
        auto *history = viewer.findChild<QUndoStack*>("photoUndoStack");
        check(!viewer.isModified(), "viewer initially clean");
        if (!sliders.isEmpty() && history) {
            sliders.first()->setValue(20);
            check(viewer.isModified(), "viewer reports parameter edit");
            history->undo(); check(!viewer.isModified() && history->count() != 0,
                                   "undo baseline clean despite nonempty history");
            history->redo(); check(viewer.isModified(), "redo marks viewer modified");
            QMetaObject::invokeMethod(&viewer, "SlotResetAlgorithms", Qt::DirectConnection);
            check(!viewer.isModified() && history->count() > 1, "reset baseline clean despite history index");
            history->undo(); check(viewer.isModified(), "undo reset restores modified state");
            history->undo(); sliders.first()->setValue(-10);
            check(viewer.isModified() && !history->canRedo(), "branch edit updates modified state");
            viewer.SlotSelectItem(path); check(!viewer.isModified(), "switching resets viewer modification");
        }
    }

    {
        QImage source(120, 80, QImage::Format_ARGB32);
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
                source.setPixel(x, y, qRgba(x * 2, y * 3, (x + y) % 256, 80 + x));
        const QString path = temporary.filePath("crop-source.png");
        check(source.save(path), "create crop source fixture");
        PhotoEditDocument document;
        document.load(path);
        bool mappingCorrect = true;
        for (int angle : {0, 90, 180, 270}) {
            for (bool horizontal : {false, true}) for (bool vertical : {false, true}) {
                for (const QRect &prior : {QRect(), QRect(10, 12, 90, 60)}) {
                    PhotoEditDocument::EditState state;
                    state.crop = prior; state.rotation = angle;
                    state.flipHorizontal = horizontal; state.flipVertical = vertical;
                    document.setState(state);
                    const QImage oriented = document.Render();
                    const QRect selected(4, 6, 30, 20);
                    const QImage expected = oriented.copy(selected);
                    state.crop = document.sourceCropFromOrientedRect(selected);
                    document.setState(state);
                    mappingCorrect &= document.Render() == expected
                        && document.outputSize() == expected.size() && document.isModified();
                }
            }
        }
        check(mappingCorrect, "crop pixels correct for all rotations mirrors and repeated crops");
        document.load(path);
        auto state = document.state(); state.crop = source.rect(); document.setState(state);
        check(!document.isModified() && document.state().crop.isEmpty(), "full image crop is canonical unmodified state");
        state.crop = QRect(5, 6, 40, 30); state.parameters.brightness = 15;
        document.setState(state);
        check(document.Render() == ImageProcessor::Apply(source.copy(state.crop), state.parameters),
              "crop precedes color operations and preserves alpha");
        const auto bytesBefore = readFile(path);
        CropCanvas canvas(source); canvas.resize(640, 480); canvas.show(); app.processEvents();
        const QRect desired(20, 16, 60, 40);
        const auto sendMouse = [&](QEvent::Type type, const QPointF &pos, Qt::MouseButton button, Qt::MouseButtons held) {
            QMouseEvent event(type, pos, canvas.mapToGlobal(pos.toPoint()), button, held, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &event);
        };
        const auto point = [&](const QPointF &imagePoint) {
            const auto display = canvas.imageRect();
            return QPointF(display.x() + imagePoint.x() * display.width() / source.width(),
                           display.y() + imagePoint.y() * display.height() / source.height());
        };
        bool handleChecks = true;
        const QVector<QPointF> handles = {QPointF(20,16), QPointF(50,16), QPointF(80,16),
            QPointF(80,36), QPointF(80,56), QPointF(50,56), QPointF(20,56), QPointF(20,36)};
        for (const auto &handle : handles) {
            canvas.setSelection(desired);
            const QPointF start = point(handle), finish = point(handle + QPointF(3, 3));
            sendMouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            sendMouse(QEvent::MouseMove, finish, Qt::NoButton, Qt::LeftButton);
            sendMouse(QEvent::MouseButtonRelease, finish, Qt::LeftButton, Qt::NoButton);
            handleChecks &= canvas.selection() != desired && source.rect().contains(canvas.selection())
                && !canvas.selection().isEmpty();
        }
        check(handleChecks, "all eight crop handles resize valid image-space selection");
        bool ratiosCorrect = true;
        for (const QSize ratio : {QSize(1,1), QSize(4,3), QSize(3,2), QSize(16,9)}) {
            canvas.setAspectRatio(ratio);
            auto rect = canvas.selection();
            ratiosCorrect &= rect.width() * ratio.height() == rect.height() * ratio.width();
            const auto start = point(QPointF(rect.x()+rect.width(), rect.y()+rect.height()));
            sendMouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            sendMouse(QEvent::MouseMove, point(QPointF(400,400)), Qt::NoButton, Qt::LeftButton);
            sendMouse(QEvent::MouseButtonRelease, point(QPointF(400,400)), Qt::LeftButton, Qt::NoButton);
            rect = canvas.selection();
            ratiosCorrect &= !rect.isEmpty() && source.rect().contains(rect)
                && rect.width() * ratio.height() == rect.height() * ratio.width();
        }
        check(ratiosCorrect, "fixed crop ratios stay valid at image boundary");

        canvas.setAspectRatio(QSize(4001,3001));
        canvas.setSelection(desired);
        const auto coprimeBefore = canvas.selection();
        const auto coprimeCorner = point(QPointF(coprimeBefore.x()+coprimeBefore.width(),
                                                coprimeBefore.y()+coprimeBefore.height()));
        sendMouse(QEvent::MouseButtonPress, coprimeCorner, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, point(QPointF(coprimeBefore.x()+30, coprimeBefore.y()+23)),
                  Qt::NoButton, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, point(QPointF(coprimeBefore.x()+30, coprimeBefore.y()+23)),
                  Qt::LeftButton, Qt::NoButton);
        const auto coprimeAfter = canvas.selection();
        check(!coprimeAfter.isEmpty() && coprimeAfter.width() < coprimeBefore.width()
              && qAbs(coprimeAfter.height() - coprimeAfter.width() * 3001.0/4001) <= 1.0,
              "coprime original aspect allows smaller crops with pixel rounding");
        canvas.setAspectRatio({});
        canvas.setSelection(desired);
        const auto center = point(QRectF(desired).center());
        sendMouse(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, point(QPointF(300,300)), Qt::NoButton, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, point(QPointF(300,300)), Qt::LeftButton, Qt::NoButton);
        check(canvas.selection().size() == desired.size() && source.rect().contains(canvas.selection()),
              "moving crop clamps to image boundary without resizing");
        canvas.setSelection(desired);
        const QRect beforeZoom = canvas.selection();
        QWheelEvent wheel(QPointF(260,200), canvas.mapToGlobal(QPoint(260,200)),
                          QPoint(), QPoint(0,120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&canvas, &wheel);
        sendMouse(QEvent::MouseButtonPress, QPointF(200,200), Qt::RightButton, Qt::RightButton);
        sendMouse(QEvent::MouseMove, QPointF(230,220), Qt::NoButton, Qt::RightButton);
        sendMouse(QEvent::MouseButtonRelease, QPointF(230,220), Qt::RightButton, Qt::NoButton);
        check(canvas.selection() == beforeZoom, "zoom and pan leave crop image coordinates unchanged");
        for (const auto button : {Qt::LeftButton, Qt::MiddleButton}) {
            const QRectF displayBefore = canvas.imageRect();
            if (button == Qt::LeftButton) QTest::keyPress(&canvas, Qt::Key_Space);
            sendMouse(QEvent::MouseButtonPress, QPointF(200,200), button, button);
            // Releasing Space first must not turn this gesture into a crop edit.
            if (button == Qt::LeftButton) QTest::keyRelease(&canvas, Qt::Key_Space);
            sendMouse(QEvent::MouseMove, QPointF(230,220), Qt::NoButton, button);
            sendMouse(QEvent::MouseButtonRelease, QPointF(230,220), button, Qt::NoButton);
            check(canvas.imageRect().topLeft() == displayBefore.topLeft() + QPointF(30,20)
                  && canvas.selection() == beforeZoom,
                  "space-left and middle drag pan canvas without editing crop");
        }
        QTest::keyPress(&canvas, Qt::Key_Space);
        QFocusEvent focusOut(QEvent::FocusOut);
        QApplication::sendEvent(&canvas, &focusOut);
        for (const bool outsideImage : {false, true}) {
            const QRectF displayBefore = canvas.imageRect();
            const QPointF start = outsideImage ? displayBefore.topLeft() - QPointF(15,15)
                                              : point(QPointF(5,5));
            const QPointF finish = point(QRectF(desired).center());
            sendMouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
            sendMouse(QEvent::MouseMove, finish, Qt::NoButton, Qt::LeftButton);
            sendMouse(QEvent::MouseButtonRelease, finish, Qt::LeftButton, Qt::NoButton);
            check((canvas.imageRect().topLeft() - displayBefore.topLeft() - (finish - start)).manhattanLength() < 0.001
                  && canvas.selection() == beforeZoom,
                  "plain left drag outside crop pans canvas even when crossing selection");
        }
        canvas.resize(900, 600); app.processEvents();
        const QPointF resizedCenter = point(QRectF(desired).center());
        sendMouse(QEvent::MouseButtonPress, resizedCenter, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, point(QRectF(desired).center()+QPointF(2,3)), Qt::NoButton, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, point(QRectF(desired).center()+QPointF(2,3)), Qt::LeftButton, Qt::NoButton);
        check(canvas.selection() == desired.translated(2,3), "crop mapping survives resize zoom and pan");
        canvas.hide();

        PicShow viewer; viewer.resize(1100,750); viewer.show(); viewer.SlotSelectItem(path); app.processEvents();
        auto *history = viewer.findChild<QUndoStack*>("photoUndoStack");
        const auto cropWith = [&](bool accept, const QRect &selection) {
            QTimer::singleShot(30, &viewer, [&, accept, selection]() {
                auto *dialog = qobject_cast<CropDialog*>(QApplication::activeModalWidget());
                check(dialog != nullptr, "crop dialog opens");
                if (!dialog) return;

                dialog->findChild<CropCanvas*>()->setSelection(selection);
                if (accept) {
                    check(dialog->grab().save(outputDir + "/crop-dialog.png"), "crop dialog screenshot");
                }
                if (accept) QTest::keyClick(dialog, Qt::Key_Return);
                else QTest::keyClick(dialog, Qt::Key_Escape);
            });
            QMetaObject::invokeMethod(&viewer, "SlotCrop", Qt::DirectConnection);
        };
        cropWith(false, desired);
        check(history && history->count() == 0 && !viewer.isModified(), "cancel crop leaves document history and modification unchanged");

        cropWith(true, desired);
        auto *cropPreview = viewer.findChild<QLabel*>("label");
        const auto croppedPreview = cropPreview->grab().toImage();
        check(history && history->count() == 1 && viewer.isModified(), "confirm crop creates one modified history step");
        if (history) {

            history->undo(); check(!viewer.isModified() && cropPreview->grab().toImage() != croppedPreview,
                                  "undo crop restores unmodified baseline");
            history->redo(); check(viewer.isModified() && cropPreview->grab().toImage() == croppedPreview,
                                  "redo restores crop modification and rendered pixels");
            QMetaObject::invokeMethod(&viewer, "SlotResetAlgorithms", Qt::DirectConnection);
            check(viewer.isModified(), "reset colors does not clear crop modification");
        }
        check(readFile(path) == bytesBefore, "crop and history preserve original file");
        viewer.hide();
    }

    {
        QImage source(180, 100, QImage::Format_RGB32);
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
                source.setPixelColor(x, y, QColor(x, y * 2, (x + y) % 256));
        const QString path = temporary.filePath("compare-original.png");
        check(source.save(path), "create comparison fixture");
        PicShow viewer; viewer.resize(1200, 800); viewer.show(); app.processEvents();
        auto *button = viewer.findChild<QPushButton*>("photoCompareButton");
        auto *restore = viewer.findChild<QPushButton*>("photoRestoreOriginalButton");
        auto *canvas = viewer.findChild<QLabel*>("label");
        auto *history = viewer.findChild<QUndoStack*>("photoUndoStack");
        check(button && !button->isEnabled(), "comparison disabled without source");
        check(restore && !restore->isEnabled(), "restore original disabled without source");
        viewer.SlotSelectItem(path); app.processEvents();
        const auto sourceBytes = readFile(path);
        const auto invoke = [&](const char *slot) { QMetaObject::invokeMethod(&viewer, slot, Qt::DirectConnection); };
        if (button && canvas && history) {
            const auto pixel = [&](double x, double y) {
                const QImage image = canvas->grab().toImage();
                return image.pixelColor(qRound(image.width()*x), qRound(image.height()*y));
            };
            const QColor originalCenter = pixel(0.5, 0.5), originalLeft = pixel(0.3, 0.5);
            const QImage originalPreview = canvas->grab().toImage();
            const auto checkCompare = [&]() {
                const QImage edited = canvas->grab().toImage();
                const int count = history->count(), index = history->index();
                const bool modified = viewer.isModified();
                QTest::mousePress(button, Qt::LeftButton);
                check(pixel(0.5,0.5) == originalCenter && pixel(0.3,0.5) == originalLeft,
                      "held comparison shows source pixels fitted independently");
                check(history->count() == count && history->index() == index && viewer.isModified() == modified,
                      "comparison preserves history and modification state");
                QTest::mouseRelease(button, Qt::LeftButton);
                check(canvas->grab().toImage() == edited, "release restores edited pixels and viewport");
            };
            viewer.findChildren<QSlider*>().first()->setValue(40);
            checkCompare();
            invoke("SlotRotateRight"); invoke("SlotFlipH");
            checkCompare();
            QTimer::singleShot(30, &viewer, [&]() {
                if (auto *dialog = qobject_cast<CropDialog*>(QApplication::activeModalWidget())) {
                    dialog->findChild<CropCanvas*>()->setSelection(QRect(10,20,50,80));
                    dialog->accept();
                }
            });
            invoke("SlotCrop"); invoke("SlotZoomIn");
            checkCompare();
            invoke("SlotFlipV");
            auto sliders = viewer.findChildren<QSlider*>();
            for (auto *slider : sliders) slider->setValue(25);
            auto *filter = viewer.findChild<QComboBox*>();
            filter->setCurrentIndex(filter->findData(ImageProcessor::FilterInvert));
            invoke("SlotZoomFit");
            const auto beforeRestore = canvas->grab().toImage();
            const int restoreIndex = history->index();
            if (restore) QTest::mouseClick(restore, Qt::LeftButton);
            check(!viewer.isModified() && !viewer.property("photoModified").toBool()
                  && history->index() == restoreIndex + 1
                  && history->undoText() == QString::fromUtf8("恢复全部原图")
                  && canvas->grab().toImage() == originalPreview,
                  "restore clears crop geometry and colors in one history step");
            bool controlsReset = filter->currentData().toInt() == ImageProcessor::FilterNone;
            for (auto *slider : sliders) controlsReset &= slider->value() == 0;
            check(controlsReset, "restore synchronizes all color controls");
            const int restoredCount = history->count();
            invoke("SlotRestoreOriginal");
            check(history->count() == restoredCount, "restoring original twice adds no empty command");
            invoke("SlotUndo");
            check(viewer.isModified() && canvas->grab().toImage() == beforeRestore
                  && filter->currentData().toInt() == ImageProcessor::FilterInvert,
                  "undo restore recovers complete edited pixels and controls");
            invoke("SlotRedo");
            check(!viewer.isModified() && canvas->grab().toImage() == originalPreview,
                  "redo restore returns to original pixels and clean state");
            check(readFile(path) == sourceBytes, "restore never writes the source file");
            QTest::mousePress(button, Qt::LeftButton);
            viewer.SlotSelectItem(path);
            QTest::mouseRelease(button, Qt::LeftButton);
            check(!viewer.isModified() && history->count() == 0 && pixel(0.5,0.5) == originalCenter,
                  "switching photo cancels held comparison");
            viewer.findChildren<QSlider*>().first()->setValue(20);
            const auto edited = canvas->grab().toImage();
            QTest::mousePress(button, Qt::LeftButton);
            QEvent deactivate(QEvent::WindowDeactivate); QApplication::sendEvent(&viewer, &deactivate);
            check(canvas->grab().toImage() == edited, "deactivation cancels comparison");
            QTest::mouseRelease(button, Qt::LeftButton);
            QTest::mousePress(button, Qt::LeftButton);
            viewer.hide(); viewer.show(); app.processEvents();
            check(canvas->grab().toImage() == edited, "hiding viewer cancels comparison");
            QTest::mouseRelease(button, Qt::LeftButton);
            QTest::mousePress(button, Qt::LeftButton);
            viewer.Clear();
            check(!button->isEnabled() && !viewer.isModified(), "clear ends comparison and disables button");
            check(restore && !restore->isEnabled(), "clear disables restore original");
            viewer.SlotSelectItem(path);
            viewer.findChildren<QSlider*>().first()->setValue(20);
            check(pixel(0.5,0.5) != originalCenter, "clear does not leave original comparison active");
            QTest::mousePress(button, Qt::LeftButton);
            viewer.SlotSelectItem(temporary.filePath("compare-missing.png"));
            check(!button->isEnabled() && history->count() == 0, "failed load cancels comparison");
            invoke("SlotRestoreOriginal");
            check(restore && !restore->isEnabled() && history->count() == 0,
                  "failed load disables restore and empty restore leaves history unchanged");
            QTest::mouseRelease(button, Qt::LeftButton);
        }
        viewer.hide();
    }
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
