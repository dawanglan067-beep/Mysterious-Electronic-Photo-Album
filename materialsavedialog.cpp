#include "materialsavedialog.h"
#include "imageexportservice.h"
#include "materialfilepicker.h"
#include <QGuiApplication>
#include <QMessageBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

QString MaterialSaveDialog::suggestedPath(const QString &sourcePath, const QString &directory,
                                        const QString &extension)
{
    const QFileInfo source(sourcePath);
    const QDir target(directory.isEmpty() ? source.absolutePath() : directory);
    for (int number = 1; ; ++number) {
        const QString path = target.absoluteFilePath(source.completeBaseName()
            + QString("_crop_%1.%2").arg(number, 3, 10, QLatin1Char('0')).arg(extension));
        if (!QFileInfo::exists(path)) return path;
    }
}

MaterialSaveDialog::MaterialSaveDialog(const QString &sourcePath, const QSize &outputSize,
                                     const QString &albumRoot, QWidget *parent)
    : QDialog(parent)
{
    setObjectName("materialSaveDialog");
    setWindowTitle(tr("另存为素材"));
    resize(620, 260);
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(tr("保存当前效果：%1 × %2 像素，包含裁剪、旋转、镜像、调色和滤镜。保存后保留当前原图与编辑历史。")
        .arg(outputSize.width()).arg(outputSize.height()), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *row = new QHBoxLayout;
    _path = new QLineEdit(suggestedPath(sourcePath), this);
    _path->setObjectName("materialOutputPath");
    row->addWidget(_path);
    auto *browse = new QPushButton(tr("选择位置…"), this);
    row->addWidget(browse);
    layout->addLayout(row);
    connect(browse, &QPushButton::clicked, this, [this, sourcePath]() {
        if (QGuiApplication::platformName() != "offscreen" && QGuiApplication::platformName() != "minimal") {
            const auto result = MaterialFilePicker::choose(sourcePath, _path->text(), this);
            if (!result.error.isEmpty()) QMessageBox::warning(this, tr("保存位置选择失败"), result.error);
            else if (!result.path.isEmpty()) _path->setText(result.path);
            return;
        }
        const auto extensions = ImageExportService::supportedExtensions();
        QStringList filters;
        if (extensions.contains("png")) filters << tr("PNG 图像 (*.png)");
        if (extensions.contains("jpg")) filters << tr("JPEG 图像 (*.jpg *.jpeg)");
        if (extensions.contains("bmp")) filters << tr("BMP 图像 (*.bmp)");
        QFileDialog chooser(this, tr("素材保存位置"));
        chooser.setObjectName("materialLocationDialog");
        // 无桌面的自动测试使用 Qt 后备窗口，正常 Windows 界面走原生对话框。
        chooser.setOption(QFileDialog::DontUseNativeDialog);
        chooser.setAcceptMode(QFileDialog::AcceptSave);
        chooser.setFileMode(QFileDialog::AnyFile);
        chooser.setNameFilters(filters);
        const QFileInfo initial(_path->text());
        chooser.setDirectory(initial.absolutePath());
        const QString suffix = initial.suffix().toLower();
        if (suffix == "jpg" || suffix == "jpeg") chooser.selectNameFilter(tr("JPEG 图像 (*.jpg *.jpeg)"));
        else if (suffix == "bmp") chooser.selectNameFilter(tr("BMP 图像 (*.bmp)"));
        const QRegularExpression numberedName("^" + QRegularExpression::escape(QFileInfo(sourcePath).completeBaseName())
                                             + "_crop_[0-9]+$");
        bool automaticName = numberedName.match(initial.completeBaseName()).hasMatch();
        const auto updateName = [&]() {
            if (!automaticName) return;
            const QString filter = chooser.selectedNameFilter();
            const QString extension = filter == tr("JPEG 图像 (*.jpg *.jpeg)") ? "jpg"
                : (filter == tr("BMP 图像 (*.bmp)") ? "bmp" : "png");
            chooser.selectFile(QFileInfo(suggestedPath(sourcePath, chooser.directory().absolutePath(), extension)).fileName());
        };
        chooser.selectFile(initial.fileName());
        connect(&chooser, &QFileDialog::directoryEntered, &chooser, updateName);
        connect(&chooser, &QFileDialog::filterSelected, &chooser, updateName);
        if (auto *nameEdit = chooser.findChild<QLineEdit*>("fileNameEdit"))
            connect(nameEdit, &QLineEdit::textEdited, &chooser, [&]() { automaticName = false; });
        updateName();
        if (chooser.exec() != QDialog::Accepted || chooser.selectedFiles().isEmpty()) return;
        QString path = chooser.selectedFiles().first();
        const QString selected = chooser.selectedNameFilter();
        if (QFileInfo(path).suffix().isEmpty())
            path += selected == tr("JPEG 图像 (*.jpg *.jpeg)") ? ".jpg"
                : (selected == tr("BMP 图像 (*.bmp)") ? ".bmp" : ".png");
        _path->setText(path);
    });
    _add = new QCheckBox(tr("同时加入当前相册"), this);
    _add->setObjectName("materialAddToAlbum");
    _add->setEnabled(!albumRoot.isEmpty());
    layout->addWidget(_add);
    auto *albumHint = new QLabel(albumRoot.isEmpty() ? tr("当前图片未关联已打开的相册，可另存到磁盘。")
        : tr("目标相册：%1\n保存到相册外时，将另复制一份到相册的“素材”目录。").arg(albumRoot), this);
    albumHint->setWordWrap(true);
    layout->addWidget(albumHint);
    auto *qualityRow = new QHBoxLayout;
    qualityRow->addWidget(new QLabel(tr("JPEG 质量（透明区域使用白色背景）："), this));
    _quality = new QSpinBox(this);
    _quality->setObjectName("materialJpegQuality");
    _quality->setRange(0,100);
    _quality->setValue(95);
    qualityRow->addWidget(_quality);
    layout->addLayout(qualityRow);
    const auto updateQuality = [this]() {
        const QString suffix = QFileInfo(_path->text()).suffix().toLower();
        _quality->setEnabled(suffix == "jpg" || suffix == "jpeg");
    };
    connect(_path, &QLineEdit::textChanged, this, updateQuality);
    updateQuality();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存素材"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString MaterialSaveDialog::outputPath() const { return _path->text().trimmed(); }
bool MaterialSaveDialog::addToAlbum() const { return _add->isEnabled() && _add->isChecked(); }
int MaterialSaveDialog::jpegQuality() const { return _quality->value(); }
