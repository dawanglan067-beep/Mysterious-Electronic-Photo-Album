#include "mainwindow.h"

#include <QApplication>
#include <QFile>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 加载 QSS 深色主题样式
    QFile qss(":/style/style.qss");
    if (qss.open(QFile::ReadOnly)) {
        a.setStyleSheet(QString::fromUtf8(qss.readAll()));
        qss.close();
    }
    else
    {
        qDebug()<<"(Open qss filed)";
        return 0;
    }

    MainWindow w;
    w.setWindowTitle("Album");
    w.showMaximized();
    return a.exec();
}
