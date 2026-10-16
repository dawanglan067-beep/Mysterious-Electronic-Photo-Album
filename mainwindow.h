#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE
class QStackedWidget;
class PhotoWallWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
protected:
    virtual void resizeEvent(QResizeEvent*event) override;
    virtual void keyPressEvent(QKeyEvent *event) override;
private:
    Ui::MainWindow *ui;
    QWidget *_protree;
    QWidget*_picshow;
    QStackedWidget *_photo_pages = nullptr;
    PhotoWallWidget *_photo_wall = nullptr;
private slots:
    void SlotCreatePro(bool checked);
    void SlotOpenPro(bool);
signals:
    void SigOpenPro(const QString &path);
};
#endif // MAINWINDOW_H
