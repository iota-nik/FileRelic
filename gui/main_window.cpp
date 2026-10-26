#include "main_window.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
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
      rawLog_(new QPlainTextEdit(this)),
      rawLogToggle_(new QToolButton(this)),
      resultsContainer_(new QWidget(this)),
      resultsLayout_(new QVBoxLayout(resultsContainer_)),
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

    resultsLayout_->setContentsMargins(0, 0, 0, 0);
    resultsLayout_->setSpacing(10);
    auto* resultsScroll = new QScrollArea(central);
    resultsScroll->setObjectName("resultsScroll");
    resultsScroll->setWidgetResizable(true);
    resultsScroll->setWidget(resultsContainer_);

    rawLog_->setReadOnly(true);
    rawLog_->setObjectName("rawLog");
    rawLog_->setVisible(false);
    rawLogToggle_->setObjectName("rawLogToggle");
    rawLogToggle_->setText("Show raw diagnostic log");
    rawLogToggle_->setCheckable(true);
    auto* reportCard = new QFrame(central);
    reportCard->setObjectName("card");
    auto* reportLayout = new QVBoxLayout(reportCard);
    reportLayout->setContentsMargins(20, 18, 20, 20);
    reportLayout->setSpacing(10);
    auto* reportLabel = new QLabel("INSPECTION RESULTS", reportCard);
    reportLabel->setObjectName("sectionLabel");
    reportLayout->addWidget(reportLabel);
    reportLayout->addWidget(resultsScroll, 1);
    reportLayout->addWidget(rawLogToggle_);
    reportLayout->addWidget(rawLog_);
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
        QScrollArea#resultsScroll { background: transparent; border: none; }
        QScrollArea#resultsScroll > QWidget > QWidget { background: transparent; }
        QFrame#resultCard { background: #111923; border: 1px solid #2a3b4d; border-radius: 8px; }
        QFrame#resultCard[tone="success"] { border-color: #287353; }
        QFrame#resultCard[tone="warning"] { border-color: #8f6830; }
        QLabel#resultTitle { color: #dbe9f7; font-size: 14px; font-weight: 700; }
        QLabel#resultContent { color: #aebfd0; font-family: monospace; font-size: 12px; }
        QToolButton#rawLogToggle { color: #8eafd0; border: none; padding: 5px 0; text-align: left; }
        QPlainTextEdit#rawLog { background: #0c1118; border: 1px solid #273747; border-radius: 6px;
            color: #d9e5f0; font-family: monospace; font-size: 13px; padding: 10px; }
    )");

    connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::chooseImage);
    connect(analyzeButton_, &QPushButton::clicked, this, &MainWindow::analyzeImage);
    connect(process_, &QProcess::readyReadStandardOutput, this, &MainWindow::appendOutput);
    connect(process_, &QProcess::readyReadStandardError, this, &MainWindow::appendOutput);
    connect(process_, &QProcess::finished, this, &MainWindow::inspectionFinished);
    connect(rawLogToggle_, &QToolButton::toggled, this, &MainWindow::toggleRawLog);

    addResultCard("Ready to inspect", "Choose a disk image and select Analyze image.\n"
                  "Results will be organized by image, partition layout, and filesystem.");
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
        rawReport_ = "Build FileRelic first, then either place filerelic beside filerelic-gui "
                     "or configure FILERELIC_CLI_PATH while building the GUI.";
        rawLog_->setPlainText(rawReport_);
        renderResults(false);
        return;
    }

    selectedImage_->setText(imagePath);
    status_->setText("Inspecting read-only…");
    chooseButton_->setEnabled(false);
    analyzeButton_->setEnabled(false);
    rawReport_.clear();
    rawLog_->clear();
    rawLogToggle_->setChecked(false);
    clearResults();
    addResultCard("Inspection in progress", "Reading the selected image in read-only mode.");
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    process_->start(cliPath(), {"inspect-image", imagePath});
}

void MainWindow::appendOutput() {
    rawReport_ += QString::fromUtf8(process_->readAllStandardOutput());
    rawReport_ += QString::fromUtf8(process_->readAllStandardError());
    rawLog_->setPlainText(rawReport_);
}

void MainWindow::inspectionFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    appendOutput();
    chooseButton_->setEnabled(true);
    analyzeButton_->setEnabled(!selectedImagePath_.isEmpty());
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        status_->setText("Inspection complete — no image data was modified.");
        renderResults(true);
    } else {
        status_->setText("Inspection failed; see the report for details.");
        renderResults(false);
    }
}

void MainWindow::toggleRawLog(bool visible) {
    rawLog_->setVisible(visible);
    rawLogToggle_->setText(visible ? "Hide raw diagnostic log" : "Show raw diagnostic log");
}

void MainWindow::clearResults() {
    while (QLayoutItem* item = resultsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

void MainWindow::addResultCard(const QString& title, const QString& content, const QString& tone) {
    auto* card = new QFrame(resultsContainer_);
    card->setObjectName("resultCard");
    if (!tone.isEmpty()) {
        card->setProperty("tone", tone);
    }
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(5);
    auto* heading = new QLabel(title, card);
    heading->setObjectName("resultTitle");
    auto* body = new QLabel(content, card);
    body->setObjectName("resultContent");
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(heading);
    layout->addWidget(body);
    resultsLayout_->insertWidget(resultsLayout_->count(), card);
}

void MainWindow::renderResults(bool succeeded) {
    clearResults();
    const QStringList lines = rawReport_.split('\n', Qt::SkipEmptyParts);
    QString imageInfo;
    QString layoutInfo;
    QStringList warnings;
    QStringList partitionBlocks;
    QString currentPartition;

    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith("Image file:") || line.startsWith("Size:")) {
            imageInfo += line + '\n';
        }
        if (line.startsWith("MBR:") || line.startsWith("GPT:")) {
            layoutInfo += line + '\n';
        }
        if (line.contains("warning", Qt::CaseInsensitive)
            || line.contains("invalid", Qt::CaseInsensitive)
            || line.startsWith("Error:")) {
            warnings.append(line);
        }

        const bool startsPartition = line.startsWith("Partition ")
            || line.startsWith("Logical partition ")
            || line.startsWith("GPT partition ");
        if (startsPartition) {
            if (!currentPartition.isEmpty()) {
                partitionBlocks.append(currentPartition);
            }
            currentPartition = line + '\n';
        } else if (!currentPartition.isEmpty()
                   && (rawLine.startsWith(' ') || rawLine.startsWith('\t'))) {
            currentPartition += line + '\n';
        }
    }
    if (!currentPartition.isEmpty()) {
        partitionBlocks.append(currentPartition);
    }

    addResultCard(succeeded ? "Inspection complete" : "Inspection needs attention",
                  succeeded ? "Image was inspected read-only. No image data was modified."
                            : "FileRelic could not complete this inspection. Review warnings or the raw log.",
                  succeeded ? "success" : "warning");
    if (!imageInfo.isEmpty()) {
        addResultCard("Image", imageInfo.trimmed());
    }
    if (!layoutInfo.isEmpty()) {
        addResultCard("Partition layout", layoutInfo.trimmed(),
                      succeeded ? QStringLiteral("success") : QString{});
    }
    for (const QString& partition : partitionBlocks) {
        const int newline = partition.indexOf('\n');
        const QString title = newline >= 0 ? partition.left(newline) : "Partition";
        const QString details = newline >= 0 ? partition.mid(newline + 1).trimmed() : QString{};
        addResultCard(title, details);
    }
    if (!warnings.isEmpty()) {
        addResultCard("Warnings", warnings.join('\n'), "warning");
    }
    if (partitionBlocks.isEmpty() && imageInfo.isEmpty() && !rawReport_.isEmpty()) {
        addResultCard("Diagnostic", rawReport_.trimmed(), "warning");
    }
    resultsLayout_->addStretch();
}
