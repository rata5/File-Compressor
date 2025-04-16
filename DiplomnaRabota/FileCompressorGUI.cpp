#include "FileCompressorGUI.h"
#include "DragAndDropList.h"

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

    statusLabel->setText("Status: Compressing...");
    progressBar->setValue(0);

    // Simulated progress
    progressBar->setValue(100);
    statusLabel->setText("Status: Done!");
}