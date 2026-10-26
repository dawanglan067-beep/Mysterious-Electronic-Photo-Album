#include "albumpdfexporter.h"
#include "photowallwidget.h"
#include "photowallmodel.h"
#include "thumbnailloader.h"
#include "mainwindow.h"
#include "protreewidget.h"
#include "protreeitem.h"
#include "picshow.h"
#include "cropdialog.h"
#include <QDialogButtonBox>
#include <QWheelEvent>
#include <QTransform>
#include "imageprocessor.h"
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
