#include "main_window.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <filesystem>

namespace {

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
      inspectionWatcher_(new QFutureWatcher<filerelic::InspectionReport>(this)) {
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
    resultsLayout_->setSpacing(12);
    resultsLayout_->setAlignment(Qt::AlignTop);
    auto* resultsScroll = new QScrollArea(central);
    resultsScroll->setObjectName("resultsScroll");
    resultsScroll->setWidgetResizable(true);
    resultsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    resultsScroll->setWidget(resultsContainer_);

    rawLog_->setReadOnly(true);
    rawLog_->setObjectName("rawLog");
    rawLog_->setVisible(false);
    rawLog_->setMinimumHeight(170);
    rawLog_->setMaximumHeight(260);
    rawLogToggle_->setObjectName("rawLogToggle");
    rawLogToggle_->setText("Advanced: show raw diagnostic log");
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
        QLabel#title { color: #f6f8fb; font-size: 32px; font-weight: 700; }
        QLabel#subtitle, QLabel#muted { color: #a9b8c9; font-size: 15px; }
        QLabel#safetyNotice { background: #13342d; border: 1px solid #23634e;
            border-radius: 7px; color: #a9edcc; font-size: 12px; font-weight: 600;
            padding: 10px 12px; }
        QFrame#card { background: #18212c; border: 1px solid #293848; border-radius: 10px; }
        QLabel#sectionLabel { color: #8bb9e8; font-size: 12px; font-weight: 700; }
        QLabel#imagePath { color: #f6f8fb; font-size: 15px; font-weight: 600; }
        QLabel#status { color: #c1d0de; font-size: 14px; padding-left: 2px; }
        QPushButton { background: #263749; border: 1px solid #38506a; border-radius: 6px;
            color: #e8f0f8; font-weight: 600; padding: 9px 15px; }
        QPushButton:hover { background: #314963; }
        QPushButton:disabled { background: #202a35; color: #738193; }
        QPushButton#analyzeButton { background: #236a9f; border-color: #3a8fc9; }
        QPushButton#analyzeButton:hover { background: #2e7db8; }
        QScrollArea#resultsScroll { background: transparent; border: none; }
        QScrollArea#resultsScroll > QWidget > QWidget { background: transparent; }
        QFrame#resultCard { background: #111923; border: 1px solid #2a3b4d; border-radius: 9px; }
        QFrame#resultCard[tone="success"] { background: #10231e; border-color: #2b8862; }
        QFrame#resultCard[tone="warning"] { background: #2b2113; border-color: #b47b2f; }
        QFrame#resultCard[tone="success"] QLabel#resultTitle { color: #a9edcc; }
        QFrame#resultCard[tone="warning"] QLabel#resultTitle { color: #ffd18a; }
        QLabel#resultTitle { color: #edf5fc; font-size: 16px; font-weight: 700; }
        QLabel#resultContent { color: #c4d1df; font-size: 14px; line-height: 1.35; }
        QToolButton#rawLogToggle { color: #a8c9e9; border: none; font-size: 13px;
            font-weight: 600; padding: 7px 0; text-align: left; }
        QPlainTextEdit#rawLog { background: #0c1118; border: 1px solid #273747; border-radius: 6px;
            color: #d9e5f0; font-family: monospace; font-size: 13px; padding: 10px; }
    )");

    connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::chooseImage);
    connect(analyzeButton_, &QPushButton::clicked, this, &MainWindow::analyzeImage);
    connect(inspectionWatcher_, &QFutureWatcher<filerelic::InspectionReport>::finished,
            this, &MainWindow::inspectionFinished);
    connect(rawLogToggle_, &QToolButton::toggled, this, &MainWindow::toggleRawLog);

    addResultCard("Ready to inspect", "Choose a disk image, then select Analyze image.\n"
                  "You will see the image, partition layout, filesystem findings, and any warnings here.");
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
    selectedImage_->setText(imagePath);
    status_->setText("Inspecting read-only…");
    chooseButton_->setEnabled(false);
    analyzeButton_->setEnabled(false);
    rawReport_.clear();
    rawLog_->clear();
    rawLogToggle_->setChecked(false);
    clearResults();
    addResultCard("Inspection in progress", "Reading the selected image in read-only mode.");
    const std::filesystem::path nativePath =
#ifdef _WIN32
        std::filesystem::path(imagePath.toStdWString());
#else
        std::filesystem::path(imagePath.toUtf8().constData());
#endif
    inspectionWatcher_->setFuture(QtConcurrent::run([nativePath] {
        return filerelic::inspect_image(nativePath);
    }));
}

void MainWindow::inspectionFinished() {
    const filerelic::InspectionReport report = inspectionWatcher_->result();
    rawReport_ = QString::fromStdString(report.rendered_text);
    rawLog_->setPlainText(rawReport_);
    chooseButton_->setEnabled(true);
    analyzeButton_->setEnabled(!selectedImagePath_.isEmpty());
    if (report.succeeded()) {
        status_->setText("Inspection complete — no image data was modified.");
        renderResults(report);
    } else {
        status_->setText("Inspection failed; see the report for details.");
        renderResults(report);
    }
}

void MainWindow::toggleRawLog(bool visible) {
    rawLog_->setVisible(visible);
    rawLogToggle_->setText(visible ? "Advanced: hide raw diagnostic log"
                                    : "Advanced: show raw diagnostic log");
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
    body->setTextFormat(Qt::PlainText);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(heading);
    layout->addWidget(body);
    resultsLayout_->insertWidget(resultsLayout_->count(), card);
}

void MainWindow::renderResults(const filerelic::InspectionReport& report) {
    clearResults();
    const bool succeeded = report.succeeded();
    addResultCard(succeeded ? "Inspection complete" : "Inspection needs attention",
                  succeeded ? "Image was inspected read-only. No image data was modified."
                            : "FileRelic could not complete this inspection. Review the report or raw diagnostic log.",
                  succeeded ? "success" : "warning");

    for (const auto& section : report.sections) {
        const QString title = QString::fromStdString(section.title);
        const QString details = QString::fromStdString(section.details);
        QString tone;
        if (section.has_warning
            || section.kind == filerelic::ReportSectionKind::warnings
            || section.kind == filerelic::ReportSectionKind::diagnostic) {
            tone = QStringLiteral("warning");
        } else if (section.kind == filerelic::ReportSectionKind::partition_layout && succeeded) {
            tone = QStringLiteral("success");
        }
        addResultCard(title, details, tone);
    }
    resultsLayout_->addStretch();
}
