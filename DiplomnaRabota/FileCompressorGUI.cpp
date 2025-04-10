#include "FileCompressorGUI.h"
#include "ui_FileCompressorGUI.h"

FileCompressorGUI::FileCompressorGUI(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::FileCompressorGUIClass)
{
    ui->setupUi(this);
}

FileCompressorGUI::~FileCompressorGUI()
{
    delete ui;
}

void FileCompressorGUI::on_selectFileButton_clicked()
{
    selectedFilePath = QFileDialog::getOpenFileName(this, "Select a file", "", "All Files (*.*)");
    if (!selectedFilePath.isEmpty()) {
        ui->lineEdit->setText(selectedFilePath);
    }
}

void FileCompressorGUI::on_compressButton_clicked()
{
    if (selectedFilePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first.");
        return;
    }

    // Example conversion process (actual logic should be implemented)
    QMessageBox::information(this, "Conversion", "File converted successfully!");
}
