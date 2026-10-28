#include "mainwindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LicensePlateDetector"));

    MainWindow window;
    window.resize(1120, 720);
    window.show();
    return app.exec();
}
