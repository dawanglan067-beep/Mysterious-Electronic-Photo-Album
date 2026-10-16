#include "albumexportdialog.h"
#include <QLineEdit>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>

AlbumExportDialog::AlbumExportDialog(const QStringList &paths, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("导出 PDF 纪念相册"));
    setObjectName("albumExportDialog");
    resize(640, 520);
    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    _title = new QLineEdit(tr("我的纪念相册"), this);
    _title->setMaxLength(100);
    form->addRow(tr("相册标题"), _title);
    _layout = new QComboBox(this);
    for (int value : {1, 2, 4}) _layout->addItem(tr("每页 %1 张").arg(value), value);
    _layout->setCurrentIndex(1);
    form->addRow(tr("页面布局"), _layout);
    auto *outputRow = new QHBoxLayout;
    _output = new QLineEdit(this);
    _output->setReadOnly(true);
    auto *browse = new QPushButton(tr("选择位置…"), this);
    outputRow->addWidget(_output);
    outputRow->addWidget(browse);
    form->addRow(tr("PDF 文件"), outputRow);
    root->addLayout(form);
    root->addWidget(new QLabel(tr("按照片墙顺序导出磁盘上的照片；双击说明列可以编辑文字。"), this));
    _photos = new QTableWidget(int(paths.size()), 2, this);
    _photos->setHorizontalHeaderLabels({tr("照片"), tr("说明")});
    _photos->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int i = 0; i < paths.size(); ++i) {
        const QFileInfo info(paths.at(i));
        auto *name = new QTableWidgetItem(info.fileName());
        name->setData(Qt::UserRole, paths.at(i));
        name->setToolTip(paths.at(i));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        _photos->setItem(i, 0, name);
        _photos->setItem(i, 1, new QTableWidgetItem(info.completeBaseName()));
    }
    root->addWidget(_photos);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("开始导出"));
    root->addWidget(buttons);
    connect(browse, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, tr("保存纪念相册"), QStringLiteral("album.pdf"), tr("PDF 文件 (*.pdf)"));
        if (!path.isEmpty()) {
            if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) path += QStringLiteral(".pdf");
            _output->setText(path);
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (_title->text().trimmed().isEmpty() || _output->text().isEmpty()) {
            QMessageBox::information(this, tr("导出设置"), tr("请填写标题并选择保存位置。"));
            return;
        }
        if (QFileInfo::exists(_output->text()) && QMessageBox::question(this, tr("覆盖文件"),
            tr("目标文件已存在，是否替换？\n%1").arg(_output->text()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        accept();
    });
}

AlbumExportOptions AlbumExportDialog::Options() const
{
    AlbumExportOptions options;
    options.title = _title->text().trimmed();
    options.outputPath = _output->text();
    options.photosPerPage = _layout->currentData().toInt();
    for (int row = 0; row < _photos->rowCount(); ++row)
        options.photos.append({ _photos->item(row, 0)->data(Qt::UserRole).toString(), _photos->item(row, 1)->text().left(300) });
    return options;
}
