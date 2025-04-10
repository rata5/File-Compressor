#ifndef FILECOMPRESSORGUI_H
#define FILECOMPRESSORGUI_H

#include <QMainWindow>
#include <QFileDialog>
#include <QMessageBox>

#include "ui_FileCompressorGUI.h"

QT_BEGIN_NAMESPACE
namespace Ui { class FileCompressorGUIClass; }
QT_END_NAMESPACE

class FileCompressorGUI : public QMainWindow
{
    Q_OBJECT

public:
    explicit FileCompressorGUI(QWidget* parent = nullptr);
    ~FileCompressorGUI();

private slots:
    void on_selectFileButton_clicked();
    void on_compressButton_clicked();

private:
    Ui::FileCompressorGUIClass* ui;
    QString selectedFilePath;
};

#endif 