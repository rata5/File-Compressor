#pragma once
#ifndef FILECOMPRESSORGUI_H
#define FILECOMPRESSORGUI_H

#include <QMainWindow>

class QListWidget;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QLabel;

class FileCompressorGUI : public QMainWindow
{
    Q_OBJECT

public:
    explicit FileCompressorGUI(QWidget* parent = nullptr);

private slots:
    void addFiles();
    void removeSelectedFiles();
    void chooseOutputDirectory();
    void startCompression();

private:
    QListWidget* fileList;
    QLineEdit* outputPathEdit;
    QProgressBar* progressBar;
    QLabel* statusLabel;
    QPushButton* addFileBtn;
    QPushButton* removeFileBtn;
    QPushButton* browseBtn;
    QPushButton* startBtn;

    void setupUI();
};

#endif 