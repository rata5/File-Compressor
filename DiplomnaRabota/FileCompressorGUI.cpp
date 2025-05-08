#include "FileCompressorGUI.h"
#include "DragAndDropList.h"
#include "Compressor.h"

#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QWidget>

FileCompressorGUI::FileCompressorGUI(QWidget* parent)
    : QMainWindow(parent) {
    setupUI();
}

void FileCompressorGUI::setupUI() {
    QWidget* central = new QWidget(this);
    this->setCentralWidget(central);

    dragAndDropList = new DragAndDropList(this);
    addFileBtn = new QPushButton("Add File(s)", this);
    removeFileBtn = new QPushButton("Remove Selected", this);
    browseBtn = new QPushButton("Browse...", this);

    startBtn = new QPushButton("Start Compression", this);
    startBtn->setFixedHeight(40);

    outputPathEdit = new QLineEdit(this);
    outputPathEdit->setReadOnly(true);
    progressBar = new QProgressBar(this);
    statusLabel = new QLabel("Status: Idle", this);

    auto* fileBtns = new QHBoxLayout;
    fileBtns->addWidget(addFileBtn);
    fileBtns->addWidget(removeFileBtn);

    auto* outputDirLayout = new QHBoxLayout;
    outputDirLayout->addWidget(outputPathEdit);
    outputDirLayout->addWidget(browseBtn);

    auto* mainLayout = new QVBoxLayout;
    mainLayout->addWidget(new QLabel("Selected File(s):"));
    mainLayout->addWidget(dragAndDropList);
    mainLayout->addLayout(fileBtns);
    mainLayout->addWidget(new QLabel("Output Directory:"));
    mainLayout->addLayout(outputDirLayout);
    mainLayout->addWidget(startBtn);
    mainLayout->addWidget(progressBar);
    mainLayout->addWidget(statusLabel);

    central->setLayout(mainLayout);

    setWindowTitle("File Compressor");
    resize(600, 400);

    connect(addFileBtn, &QPushButton::clicked, this, &FileCompressorGUI::addFiles);
    connect(removeFileBtn, &QPushButton::clicked, this, &FileCompressorGUI::removeSelectedFiles);
    connect(browseBtn, &QPushButton::clicked, this, &FileCompressorGUI::chooseOutputDirectory);
    connect(startBtn, &QPushButton::clicked, this, &FileCompressorGUI::startCompression);
}

void FileCompressorGUI::addFiles() {
    QStringList files = QFileDialog::getOpenFileNames(this, "Select Files");
    dragAndDropList->addItems(files);
}

void FileCompressorGUI::removeSelectedFiles() {
    qDeleteAll(dragAndDropList->selectedItems());
}

void FileCompressorGUI::chooseOutputDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Output Directory");
    if (!dir.isEmpty()) {
        outputPathEdit->setText(dir);
    }
}

void FileCompressorGUI::startCompression() {
    if (dragAndDropList->count() == 0 || outputPathEdit->text().isEmpty()) {
        QMessageBox::warning(this, "Input Error", "Please select files and output directory.");
        return;
    }

    QString outputDir = outputPathEdit->text();
    QDir dir(outputDir);
    if (!dir.exists()) {
        QMessageBox::warning(this, "Directory Error", "Output directory does not exist.");
        return;
    }

    statusLabel->setText("Status: Compressiong...");
    progressBar->setValue(0);
    int fileCount = dragAndDropList->count();

    for (int i = 0; i < fileCount; ++i) {
        QListWidgetItem* item = dragAndDropList->item(i);
        QString inputFilePath = item->text();
        QFileInfo inputInfo(inputFilePath);
        QString outputFilePath = dir.filePath(inputInfo.completeBaseName() + ".srr");

        try {
            Compressor::compress(inputFilePath.toStdString(), outputFilePath.toStdString());
        }
        catch (const std::exception& e) {
            QMessageBox::critical(this, "Compression Error", QString("Failed to compress %1: %2")
                .arg(inputFilePath, e.what()));
            statusLabel->setText("Status Failed!");
            return;
        }
        progressBar->setValue((i + 1) * 100 / fileCount);
    }
    statusLabel->setText("Status: Done!");
}