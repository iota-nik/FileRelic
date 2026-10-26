#include "main_window.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QTextEdit>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString defaultCliPath() {
#ifdef FILERELIC_CLI_PATH
    return QStringLiteral(FILERELIC_CLI_PATH);
#else
    // The presentation build can place filerelic-gui next to filerelic.
    return QCoreApplication::applicationDirPath() + "/filerelic";
#endif
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      selectedImage_(new QLabel("No image selected", this)),
      status_(new QLabel("Ready — FileRelic always requests read-only inspection.", this)),
      chooseButton_(new QPushButton("Choose disk image…", this)),
      report_(new QTextEdit(this)),
      process_(new QProcess(this)) {
    setWindowTitle("FileRelic — Disk Image Inspector");
    resize(900, 620);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(new QLabel("FileRelic forensic disk-image inspector", central));
    layout->addWidget(chooseButton_);
    layout->addWidget(selectedImage_);
    layout->addWidget(status_);

    report_->setReadOnly(true);
    report_->setPlaceholderText("Choose a raw disk image to display FileRelic's inspection report.");
    layout->addWidget(report_, 1);
    setCentralWidget(central);

    connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::chooseImage);
    connect(process_, &QProcess::readyReadStandardOutput, this, &MainWindow::appendOutput);
    connect(process_, &QProcess::readyReadStandardError, this, &MainWindow::appendOutput);
    connect(process_, &QProcess::finished, this, &MainWindow::inspectionFinished);
}

QString MainWindow::cliPath() const {
    return defaultCliPath();
}

void MainWindow::chooseImage() {
    const QString imagePath = QFileDialog::getOpenFileName(
        this,
        "Choose a disk image",
        QString(),
        "Disk images (*.raw *.img *.dd *.dmg);;All files (*)");
    if (!imagePath.isEmpty()) {
        inspectImage(imagePath);
    }
}

void MainWindow::inspectImage(const QString& imagePath) {
    const QFileInfo executable(cliPath());
    if (!executable.isFile() || !executable.isExecutable()) {
        status_->setText("Could not find the filerelic CLI: " + cliPath());
        report_->setPlainText(
            "Build FileRelic first, then either place filerelic beside filerelic-gui "
            "or configure FILERELIC_CLI_PATH while building the GUI.");
        return;
    }

    selectedImage_->setText("Selected image: " + imagePath);
    status_->setText("Inspecting read-only…");
    chooseButton_->setEnabled(false);
    report_->clear();
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    process_->start(cliPath(), {"inspect-image", imagePath});
}

void MainWindow::appendOutput() {
    report_->moveCursor(QTextCursor::End);
    report_->insertPlainText(process_->readAllStandardOutput());
    report_->insertPlainText(process_->readAllStandardError());
}

void MainWindow::inspectionFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    appendOutput();
    chooseButton_->setEnabled(true);
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        status_->setText("Inspection complete — no image data was modified.");
    } else {
        status_->setText("Inspection failed; see the report for details.");
    }
}
