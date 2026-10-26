#include "main_window.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
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

QString formatSize(qint64 bytes) {
    constexpr qint64 kibibyte = 1024;
    constexpr qint64 mebibyte = 1024 * kibibyte;
    if (bytes >= mebibyte) {
        return QString::number(static_cast<double>(bytes) / mebibyte, 'f', 2) + " MiB";
    }
    if (bytes >= kibibyte) {
        return QString::number(static_cast<double>(bytes) / kibibyte, 'f', 2) + " KiB";
    }
    return QString::number(bytes) + " bytes";
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      selectedImage_(new QLabel("No image selected", this)),
      imageDetails_(new QLabel("Choose a .raw, .img, .dd, or .dmg image to begin.", this)),
      status_(new QLabel("Ready — no image data will be modified.", this)),
      chooseButton_(new QPushButton("Choose image", this)),
      analyzeButton_(new QPushButton("Analyze image", this)),
      report_(new QTextEdit(this)),
      process_(new QProcess(this)) {
    setWindowTitle("FileRelic — Disk Image Inspector");
    resize(1000, 700);
    setMinimumSize(760, 540);

    auto* central = new QWidget(this);
    central->setObjectName("central");
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(30, 28, 30, 28);
    layout->setSpacing(16);

    auto* title = new QLabel("FileRelic", central);
    title->setObjectName("title");
    auto* subtitle = new QLabel(
        "Read-only forensic inspection for disk images and partition layouts", central);
    subtitle->setObjectName("subtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto* safetyNotice = new QLabel(
        "READ-ONLY MODE   •   FileRelic never mounts, changes, or writes to the selected image.",
        central);
    safetyNotice->setObjectName("safetyNotice");
    layout->addWidget(safetyNotice);

    auto* imageCard = new QFrame(central);
    imageCard->setObjectName("card");
    auto* imageLayout = new QVBoxLayout(imageCard);
    imageLayout->setContentsMargins(20, 18, 20, 18);
    imageLayout->setSpacing(6);
    auto* imageLabel = new QLabel("SELECTED DISK IMAGE", imageCard);
    imageLabel->setObjectName("sectionLabel");
    selectedImage_->setObjectName("imagePath");
    selectedImage_->setWordWrap(true);
    imageDetails_->setObjectName("muted");
    analyzeButton_->setObjectName("analyzeButton");
    analyzeButton_->setEnabled(false);
    auto* controls = new QHBoxLayout();
    controls->setSpacing(10);
    controls->addWidget(chooseButton_);
    controls->addWidget(analyzeButton_);
    controls->addStretch();
    imageLayout->addWidget(imageLabel);
    imageLayout->addWidget(selectedImage_);
    imageLayout->addWidget(imageDetails_);
    imageLayout->addLayout(controls);
    layout->addWidget(imageCard);

    report_->setReadOnly(true);
    report_->setObjectName("report");
    report_->setPlaceholderText(
        "Choose an image, then select Analyze image.\n\n"
        "The inspection report will show partition layout, validation warnings, "
        "and probable filesystem signatures here.");
    auto* reportCard = new QFrame(central);
    reportCard->setObjectName("card");
    auto* reportLayout = new QVBoxLayout(reportCard);
    reportLayout->setContentsMargins(20, 18, 20, 20);
    reportLayout->setSpacing(10);
    auto* reportLabel = new QLabel("INSPECTION REPORT", reportCard);
    reportLabel->setObjectName("sectionLabel");
    reportLayout->addWidget(reportLabel);
    reportLayout->addWidget(report_, 1);
    layout->addWidget(reportCard, 1);

    status_->setObjectName("status");
    layout->addWidget(status_);
    setCentralWidget(central);

    setStyleSheet(R"(
        QWidget#central { background: #10151d; color: #e7edf5; }
        QLabel#title { color: #f6f8fb; font-size: 30px; font-weight: 700; }
        QLabel#subtitle, QLabel#muted { color: #97a6b8; font-size: 14px; }
        QLabel#safetyNotice { background: #13342d; border: 1px solid #23634e;
            border-radius: 7px; color: #a9edcc; font-size: 12px; font-weight: 600;
            padding: 10px 12px; }
        QFrame#card { background: #18212c; border: 1px solid #293848; border-radius: 10px; }
        QLabel#sectionLabel { color: #7ea8d8; font-size: 11px; font-weight: 700; }
        QLabel#imagePath { color: #f2f5f8; font-family: monospace; font-size: 14px; }
        QLabel#status { color: #b9c7d6; padding-left: 2px; }
        QPushButton { background: #263749; border: 1px solid #38506a; border-radius: 6px;
            color: #e8f0f8; font-weight: 600; padding: 9px 15px; }
        QPushButton:hover { background: #314963; }
        QPushButton:disabled { background: #202a35; color: #738193; }
        QPushButton#analyzeButton { background: #236a9f; border-color: #3a8fc9; }
        QPushButton#analyzeButton:hover { background: #2e7db8; }
        QTextEdit#report { background: #0c1118; border: 1px solid #273747; border-radius: 6px;
            color: #d9e5f0; font-family: monospace; font-size: 13px; padding: 10px; }
    )");

    connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::chooseImage);
    connect(analyzeButton_, &QPushButton::clicked, this, &MainWindow::analyzeImage);
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
        const QFileInfo info(imagePath);
        selectedImagePath_ = imagePath;
        selectedImage_->setText(imagePath);
        imageDetails_->setText(info.fileName() + "  •  " + formatSize(info.size()));
        analyzeButton_->setEnabled(true);
        status_->setText("Image selected — ready for read-only inspection.");
    }
}

void MainWindow::analyzeImage() {
    if (!selectedImagePath_.isEmpty()) {
        inspectImage(selectedImagePath_);
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

    selectedImage_->setText(imagePath);
    status_->setText("Inspecting read-only…");
    chooseButton_->setEnabled(false);
    analyzeButton_->setEnabled(false);
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
    analyzeButton_->setEnabled(!selectedImagePath_.isEmpty());
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        status_->setText("Inspection complete — no image data was modified.");
    } else {
        status_->setText("Inspection failed; see the report for details.");
    }
}
