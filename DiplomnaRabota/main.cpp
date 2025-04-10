#include "FileCompressorGUI.h"
#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    FileCompressorGUI w;
    w.show();
    return a.exec();
}
