#include "albumpdfexporter.h"
#include <QPdfWriter>
#include <QPainter>
#include <QPageSize>
#include <QSaveFile>
#include <QImageReader>
#include <QFileInfo>
#include <QDir>
#include <QFont>

AlbumPdfExporter::AlbumPdfExporter(const AlbumExportOptions &options, QObject *parent)
    : QThread(parent), _options(options) {}

AlbumPdfExporter::~AlbumPdfExporter()
{
    Cancel();
    wait();
}

void AlbumPdfExporter::run()
{
    const auto fail = [this](const QString &message) { emit SigResult(false, false, message); };
    if (_options.photos.isEmpty() || !QList<int>{1, 2, 4}.contains(_options.photosPerPage)) {
        fail(tr("请选择照片和有效的页面布局。"));
        return;
    }
    const QFileInfo target(_options.outputPath);
    for (const auto &photo : _options.photos) {
        const QFileInfo source(photo.path);
        if (QDir::cleanPath(source.absoluteFilePath()).compare(QDir::cleanPath(target.absoluteFilePath()), Qt::CaseInsensitive) == 0
            || (target.exists() && !source.canonicalFilePath().isEmpty() && source.canonicalFilePath() == target.canonicalFilePath())) {
            fail(tr("输出文件不能覆盖源照片。"));
            return;
        }
    }
    QSaveFile file(_options.outputPath);
    if (!file.open(QIODevice::WriteOnly)) { fail(file.errorString()); return; }
    QString error;
    {
        QPdfWriter writer(&file);
        writer.setResolution(144);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(15, 15, 15, 15));
        writer.setTitle(_options.title);
        writer.setCreator(QStringLiteral("Album"));
        QPainter painter;
        if (!painter.begin(&writer)) { error = tr("无法创建 PDF 页面。"); }
        else {
            const int width = writer.width(), height = writer.height();
            const int count = int(_options.photos.size());
            const int perPage = _options.photosPerPage;
            const int pages = 1 + (count + perPage - 1) / perPage;
            painter.setPen(QColor(30, 40, 60));
            painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 28, QFont::Bold));
            painter.drawText(QRect(30, height / 4, width - 60, height / 3), Qt::AlignCenter | Qt::TextWordWrap, _options.title);
            painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 12));
            painter.drawText(QRect(0, height * 2 / 3, width, 80), Qt::AlignCenter, tr("%1 张照片").arg(count));
            painter.drawText(QRect(0, height - 40, width, 40), Qt::AlignCenter, QStringLiteral("1 / %1").arg(pages));
            for (int i = 0; i < count && error.isEmpty() && !_cancelled.load(); ++i) {
                if (i % perPage == 0) {
                    if (!writer.newPage()) { error = tr("无法写入下一页。"); break; }
                    painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
                    painter.drawText(QRect(0, height - 40, width, 40), Qt::AlignCenter,
                                     QStringLiteral("%1 / %2").arg(2 + i / perPage).arg(pages));
                }
                const int columns = perPage == 4 ? 2 : 1;
                const int rows = perPage == 1 ? 1 : 2;
                const int gap = 24;
                const int cellWidth = (width - (columns - 1) * gap) / columns;
                const int cellHeight = (height - 60 - (rows - 1) * gap) / rows;
                const int slot = i % perPage;
                const QRect cell((slot % columns) * (cellWidth + gap), (slot / columns) * (cellHeight + gap), cellWidth, cellHeight);
                const QRect imageBox = cell.adjusted(0, 0, 0, -70);
                const auto &photo = _options.photos.at(i);
                QImageReader reader(photo.path);
                reader.setAutoTransform(true);
                const QSize originalSize = reader.size();
                const QSize decodeBox(imageBox.height() > imageBox.width() ? imageBox.height() : imageBox.width(),
                                      imageBox.height() > imageBox.width() ? imageBox.height() : imageBox.width());
                if (originalSize.isValid()) reader.setScaledSize(originalSize.scaled(decodeBox, Qt::KeepAspectRatio));
                const QImage image = reader.read();
                if (image.isNull()) { error = tr("无法读取照片：%1\n%2").arg(photo.path, reader.errorString()); break; }
                if (_cancelled.load()) break;
                const QSize fitted = image.size().scaled(imageBox.size(), Qt::KeepAspectRatio);
                const QRect destination(imageBox.center().x() - fitted.width() / 2, imageBox.center().y() - fitted.height() / 2, fitted.width(), fitted.height());
                painter.drawImage(destination, image);
                painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 10));
                painter.drawText(QRect(cell.left(), imageBox.bottom() + 8, cell.width(), 60),
                                 Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, photo.caption);
                emit SigProgress(i + 1, count);
            }
            if (!painter.end() && error.isEmpty()) error = tr("PDF 写入未完成。");
        }
    }
    if (_cancelled.load()) { file.cancelWriting(); emit SigResult(false, true, tr("已取消导出。")); return; }
    if (!error.isEmpty()) { file.cancelWriting(); fail(error); return; }
    if (file.error() != QFileDevice::NoError || !file.commit()) { fail(file.errorString()); return; }
    emit SigResult(true, false, _options.outputPath);
}
