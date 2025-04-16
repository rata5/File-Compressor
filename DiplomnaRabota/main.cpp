#include <QApplication>

#include "FileCompressorGUI.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    FileCompressorGUI window;
    window.show();
    return app.exec();
}