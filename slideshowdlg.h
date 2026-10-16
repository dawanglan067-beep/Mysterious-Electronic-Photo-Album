#ifndef SLIDESHOWDLG_H
#define SLIDESHOWDLG_H

#include <QDialog>
#include <QTreeWidgetItem>

namespace Ui {
class SlideShowDlg;
}

class SlideShowDlg : public QDialog
{
    Q_OBJECT

public:
    explicit SlideShowDlg(QWidget *parent, QTreeWidgetItem* first_item, QTreeWidgetItem* last_item);
    ~SlideShowDlg();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    Ui::SlideShowDlg *ui;
    QTreeWidgetItem* _first_item;
    QTreeWidgetItem* _last_item;

public slots:
    void SlotSlidNext();
    void SlotSlidPre();
};

#endif // SLIDESHOWDLG_H
