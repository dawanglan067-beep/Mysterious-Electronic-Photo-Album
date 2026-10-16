#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QDebug>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon/app/album.ico")));

    // 加载 QSS 深色主题样式
    QFile qss(":/style/style.qss");
    if (qss.open(QFile::ReadOnly)) {
        qDebug() << "Open qss success!";
        a.setStyleSheet(QString::fromUtf8(qss.readAll()));
        qss.close();
    } else {
        qDebug() << "Open qss failed, using default style.";
    }

    MainWindow w;
    w.setWindowTitle("Album");
    w.showMaximized();
    return a.exec();
}
