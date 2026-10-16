#ifndef ALBUMEXPORTDIALOG_H
#define ALBUMEXPORTDIALOG_H

#include <QDialog>
#include "photoentry.h"
class QLineEdit;
class QComboBox;
class QTableWidget;

class AlbumExportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AlbumExportDialog(const QStringList &paths, QWidget *parent = nullptr);
    AlbumExportOptions Options() const;
private:
    QLineEdit *_title;
    QLineEdit *_output;
    QComboBox *_layout;
    QTableWidget *_photos;
};

#endif
