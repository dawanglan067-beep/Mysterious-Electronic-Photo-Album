#pragma once
#include <QDialog>
class QLineEdit;
class QCheckBox;
class QSpinBox;

class MaterialSaveDialog : public QDialog
{
    Q_OBJECT
public:
    MaterialSaveDialog(const QString &sourcePath, const QSize &outputSize,
                       const QString &albumRoot, QWidget *parent = nullptr);
    QString outputPath() const;
    bool addToAlbum() const;
    int jpegQuality() const;
    static QString suggestedPath(const QString &sourcePath);
private:
    QLineEdit *_path;
    QCheckBox *_add;
    QSpinBox *_quality;
};
