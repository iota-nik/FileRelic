#include "main_window.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QLayoutItem>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <filesystem>

#include "report_format.h"

namespace {

constexpr int candidate_path_role = Qt::UserRole;
constexpr int candidate_inode_role = Qt::UserRole + 1;
constexpr int candidate_deleted_role = Qt::UserRole + 2;
constexpr int candidate_directory_role = Qt::UserRole + 3;

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

std::filesystem::path nativePath(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().constData());
#endif
}

QString qPath(const std::filesystem::path& path) {
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromUtf8(path.string().c_str());
#endif
}

QString createAutomaticManifestPath(const QString& directory, const QString& imagePath) {
    const QString stem = QFileInfo(imagePath).completeBaseName();
    const QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss");
    QString path = QDir(directory).filePath(stem + ".investigation-" + timestamp + ".manifest.json");
    for (int suffix = 1; QFileInfo::exists(path); ++suffix) {
        path = QDir(directory).filePath(
            stem + ".investigation-" + timestamp + "-" + QString::number(suffix) + ".manifest.json");
    }
    return path;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      selectedImage_(new QLabel("No image selected", this)),
      imageDetails_(new QLabel("Choose a .raw, .img, .dd, or .dmg image to begin.", this)),
      status_(new QLabel("Ready — no image data will be modified.", this)),
      chooseButton_(new QPushButton("Choose image", this)),
      analyzeButton_(new QPushButton("Analyze image", this)),
      reportButton_(new QPushButton("Export case report", this)),
      destinationButton_(new QPushButton("Choose output folder", this)),
      refreshCandidatesButton_(new QPushButton("Go", this)),
      upDirectoryButton_(new QPushButton("Up", this)),
      extractButton_(new QPushButton("Extract selected", this)),
      partitionSelector_(new QComboBox(this)),
      candidatePath_(new QLineEdit("/", this)),
      showDeleted_(new QCheckBox("Include deleted names", this)),
      candidateList_(new QListWidget(this)),
      rawLog_(new QPlainTextEdit(this)),
      rawLogToggle_(new QToolButton(this)),
      detailsToggle_(new QCheckBox("Show technical details (sector numbers, IDs, features)", this)),
      workflowProgress_(new QProgressBar(this)),
      exportLocation_(new QLabel("Output: default current project directory", this)),
      resultsContainer_(new QWidget(this)),
      resultsLayout_(new QVBoxLayout(resultsContainer_)),
      inspectionWatcher_(new QFutureWatcher<filerelic::InspectionReport>(this)),
      extractionWatcher_(new QFutureWatcher<filerelic::ExtractionResult>(this)),
      manifestWatcher_(new QFutureWatcher<QString>(this)),
      candidatesWatcher_(new QFutureWatcher<CandidateLoadResult>(this)) {
    setWindowTitle("FileRelic — Disk Image Inspector and Recovery");
    resize(1180, 780);
    setMinimumSize(800, 560);

    auto* central = new QWidget(this);
    central->setObjectName("central");
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(12);

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
    extractButton_->setObjectName("extractButton");
    chooseButton_->setShortcut(QKeySequence::Open);
    analyzeButton_->setShortcut(QKeySequence(Qt::Key_F5));
    chooseButton_->setToolTip("Choose a disk image to inspect (Ctrl+O)");
    analyzeButton_->setToolTip("Inspect the selected image (F5)");
    reportButton_->setToolTip("Save a separate copy of the current investigation manifest");
    destinationButton_->setToolTip("Choose where manifests and extracted files are saved");
    refreshCandidatesButton_->setToolTip("Load the folder path shown here");
    upDirectoryButton_->setToolTip("Move to the parent folder (Alt+Up)");
    upDirectoryButton_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    extractButton_->setToolTip("Extract the selected live item or attempt recovery of a deleted candidate");
    showDeleted_->setToolTip("Search this folder's directory metadata for deleted-name candidates");
    candidateList_->setAlternatingRowColors(true);
    analyzeButton_->setEnabled(false);
    reportButton_->setEnabled(false);
    destinationButton_->setEnabled(false);
    exportLocation_->setObjectName("muted");
    auto* controls = new QHBoxLayout();
    controls->setSpacing(10);
    controls->addWidget(chooseButton_);
    controls->addWidget(analyzeButton_);
    controls->addStretch();
    imageLayout->addWidget(imageLabel);
    imageLayout->addWidget(selectedImage_);
    imageLayout->addWidget(imageDetails_);
    imageLayout->addLayout(controls);
    imageLayout->addWidget(exportLocation_);
    auto* workflowControls = new QHBoxLayout();
    workflowControls->setSpacing(10);
    workflowControls->addWidget(destinationButton_);
    workflowControls->addWidget(reportButton_);
    workflowControls->addStretch();
    imageLayout->addLayout(workflowControls);
    imageLayout->addWidget(workflowProgress_);
    workflowProgress_->setRange(0, 100);
    workflowProgress_->setValue(0);
    workflowProgress_->setTextVisible(false);

    auto* browserCard = new QFrame(central);
    browserCard->setObjectName("card");
    auto* browserLayout = new QVBoxLayout(browserCard);
    browserLayout->setContentsMargins(20, 18, 20, 18);
    browserLayout->setSpacing(8);
    auto* browserLabel = new QLabel("EXT FILES AND RECOVERY CANDIDATES", browserCard);
    browserLabel->setObjectName("sectionLabel");
    browserLayout->addWidget(browserLabel);
    partitionSelector_->setEnabled(false);
    candidatePath_->setEnabled(false);
    candidatePath_->setPlaceholderText("Image path, e.g. / or /docs");
    refreshCandidatesButton_->setEnabled(false);
    showDeleted_->setEnabled(false);
    extractButton_->setEnabled(false);
    auto* browseControls = new QHBoxLayout();
    auto* partitionLabel = new QLabel("Partition", browserCard);
    partitionLabel->setBuddy(partitionSelector_);
    browseControls->addWidget(partitionLabel);
    browseControls->addWidget(partitionSelector_);
    browseControls->addWidget(candidatePath_, 1);
    browseControls->addWidget(upDirectoryButton_);
    browseControls->addWidget(refreshCandidatesButton_);
    browserLayout->addLayout(browseControls);
    auto* candidateControls = new QHBoxLayout();
    candidateControls->addWidget(showDeleted_);
    candidateControls->addStretch();
    candidateControls->addWidget(extractButton_);
    browserLayout->addLayout(candidateControls);
    candidateList_->setMinimumHeight(180);
    candidateList_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    candidateList_->setUniformItemSizes(true);
    candidateList_->setEnabled(false);
    browserLayout->addWidget(candidateList_);
    auto* browserNote = new QLabel(
        "File browsing and extraction are available for ext2/3/4 partitions only. "
        "Deleted-name matches are candidates, not proof of recoverability.",
        browserCard);
    browserNote->setObjectName("muted");
    browserNote->setWordWrap(true);
    browserLayout->addWidget(browserNote);
    auto* leftPanel = new QWidget(central);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 6, 0);
    leftLayout->setSpacing(12);
    leftLayout->addWidget(imageCard);
    leftLayout->addWidget(browserCard, 1);
    leftPanel->setMinimumWidth(310);

    auto* leftScroll = new QScrollArea(central);
    leftScroll->setObjectName("leftScroll");
    leftScroll->setWidgetResizable(true);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setWidget(leftPanel);

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
    detailsToggle_->setObjectName("detailsToggle");
    reportLayout->addWidget(detailsToggle_);
    reportLayout->addWidget(resultsScroll, 1);
    reportLayout->addWidget(rawLogToggle_);
    reportLayout->addWidget(rawLog_);
    reportCard->setMinimumWidth(360);

    auto* workspace = new QSplitter(Qt::Horizontal, central);
    workspace->setObjectName("workspace");
    workspace->setChildrenCollapsible(false);
    workspace->setHandleWidth(7);
    workspace->addWidget(leftScroll);
    workspace->addWidget(reportCard);
    workspace->setStretchFactor(0, 4);
    workspace->setStretchFactor(1, 5);
    workspace->setSizes({480, 580});
    layout->addWidget(workspace, 1);

    status_->setObjectName("status");
    status_->setWordWrap(true);
    status_->setMinimumHeight(28);
    layout->addWidget(status_);
    setCentralWidget(central);

    setStyleSheet(R"(
        QWidget#central { background: #0c1118; color: #e6edf5; }
        QLabel#title { color: #f4f7fb; font-size: 30px; font-weight: 700; }
        QLabel#subtitle, QLabel#muted { color: #9cabbc; font-size: 13px; }
        QLabel#safetyNotice { background: #102923; border: 1px solid #245d49;
            border-radius: 7px; color: #a9e8c8; font-size: 12px; font-weight: 600;
            padding: 9px 12px; }
        QFrame#card { background: #141c26; border: 1px solid #293545; border-radius: 10px; }
        QLabel#sectionLabel { color: #7db8ed; font-size: 11px; font-weight: 700; }
        QLabel#imagePath { color: #f4f7fb; font-size: 14px; font-weight: 600; }
        QLabel#status { color: #c2cedb; font-size: 12px; padding: 4px 2px; }
        QLabel#status[tone="success"] { color: #8ed5b0; }
        QLabel#status[tone="warning"] { color: #e8c278; }
        QLabel#status[tone="problem"] { color: #ef9aa4; }
        QPushButton { background: #202b39; border: 1px solid #344357; border-radius: 6px;
            color: #e6edf5; font-weight: 600; min-height: 20px; padding: 8px 13px; }
        QPushButton:hover { background: #2a394b; border-color: #526a84; }
        QPushButton:pressed { background: #18222e; }
        QPushButton:focus, QComboBox:focus, QLineEdit:focus, QListWidget:focus {
            border: 1px solid #55a8eb; }
        QPushButton:disabled { background: #171f29; border-color: #252f3c; color: #748193; }
        QPushButton#analyzeButton, QPushButton#extractButton {
            background: #1769aa; border-color: #2d85c8; color: #ffffff; }
        QPushButton#analyzeButton:hover, QPushButton#extractButton:hover {
            background: #217fca; }
        QProgressBar { background: #1a2531; border: none; border-radius: 3px;
            min-height: 5px; max-height: 5px; }
        QProgressBar::chunk { background: #39a2e8; border-radius: 3px; }
        QSplitter#workspace::handle { background: #0c1118; }
        QSplitter#workspace::handle:hover { background: #30455d; }
        QScrollArea#leftScroll, QScrollArea#resultsScroll { background: transparent; border: none; }
        QScrollArea#leftScroll > QWidget > QWidget,
        QScrollArea#resultsScroll > QWidget > QWidget { background: transparent; }
        QFrame#resultCard { background: #101720; border: 1px solid #273544; border-radius: 8px; }
        QFrame#resultCard[tone="success"] { background: #10241d; border-color: #2c7959; }
        QFrame#resultCard[tone="warning"] { background: #292116; border-color: #9b7134; }
        QFrame#resultCard[tone="success"] QLabel#resultTitle { color: #a9e8c8; }
        QFrame#resultCard[tone="warning"] QLabel#resultTitle { color: #f5cd87; }
        QFrame#resultCard[tone="problem"] { background: #2a171b; border-color: #a94755; }
        QFrame#resultCard[tone="problem"] QLabel#resultTitle { color: #f2aeb5; }
        QLabel#rowLabel { color: #91a2b5; font-size: 12px; }
        QLabel#rowValue { color: #e1e9f2; font-size: 13px; }
        QLabel#rowTech { color: #8293a7; font-size: 11px; }
        QCheckBox#detailsToggle, QCheckBox { color: #b8c9dc; spacing: 7px; }
        QCheckBox::indicator { width: 15px; height: 15px; }
        QComboBox, QLineEdit, QListWidget {
            background: #0d141c; border: 1px solid #2b394a; border-radius: 5px;
            color: #dce5ef; padding: 7px;
            alternate-background-color: #121b25;
            selection-background-color: #245174;
            selection-color: #f4f8fc; }
        QComboBox QAbstractItemView { background: #141c26; color: #e1e9f2;
            selection-background-color: #1769aa; }
        QListWidget::item { background: #0d141c; color: #dce5ef;
            min-height: 26px; padding: 4px; border-radius: 4px; }
        QListWidget::item:alternate { background: #121b25; color: #dce5ef; }
        QListWidget::item:disabled { background: #17212c; color: #91a0b1; }
        QListWidget::item:selected { background: #245174; color: #f4f8fc; }
        QListWidget::item:selected:!active { background: #1d405c; color: #eef4fa; }
        QLabel#resultTitle { color: #eaf1f8; font-size: 14px; font-weight: 700; }
        QLabel#resultContent { color: #bfccd9; font-size: 12px; }
        QToolButton#rawLogToggle { color: #9ec9ed; border: none; font-size: 12px;
            font-weight: 600; padding: 6px 0; text-align: left; }
        QPlainTextEdit#rawLog { background: #0b1016; border: 1px solid #273544; border-radius: 6px;
            color: #d4dfeb; font-family: monospace; font-size: 12px; padding: 8px; }
    )");

    connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::chooseImage);
    connect(analyzeButton_, &QPushButton::clicked, this, &MainWindow::analyzeImage);
    connect(destinationButton_, &QPushButton::clicked, this, &MainWindow::chooseOutputDirectory);
    connect(reportButton_, &QPushButton::clicked, this, &MainWindow::exportCaseReport);
    connect(refreshCandidatesButton_, &QPushButton::clicked, this, &MainWindow::refreshCandidates);
    connect(upDirectoryButton_, &QPushButton::clicked, this, &MainWindow::navigateUp);
    connect(extractButton_, &QPushButton::clicked, this, &MainWindow::extractSelectedCandidate);
    connect(partitionSelector_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::refreshCandidates);
    connect(showDeleted_, &QCheckBox::toggled, this, &MainWindow::refreshCandidates);
    connect(candidatePath_, &QLineEdit::returnPressed, this, &MainWindow::refreshCandidates);
    connect(candidateList_, &QListWidget::currentRowChanged, this, [this](int) {
        updateWorkflowState();
    });
    connect(candidateList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item->data(candidate_directory_role).toBool()
            && !item->data(candidate_deleted_role).toBool()) {
            currentDirectory_ = item->data(candidate_path_role).toString();
            candidatePath_->setText(currentDirectory_);
            refreshCandidates();
        }
    });
    connect(inspectionWatcher_, &QFutureWatcher<filerelic::InspectionReport>::finished,
            this, &MainWindow::inspectionFinished);
    connect(extractionWatcher_, &QFutureWatcher<filerelic::ExtractionResult>::finished,
            this, &MainWindow::extractionFinished);
    connect(manifestWatcher_, &QFutureWatcher<QString>::finished,
            this, &MainWindow::manifestWriteFinished);
    connect(candidatesWatcher_, &QFutureWatcher<CandidateLoadResult>::finished,
            this, &MainWindow::candidatesFinished);
    connect(rawLogToggle_, &QToolButton::toggled, this, &MainWindow::toggleRawLog);
    connect(detailsToggle_, &QCheckBox::toggled, this, &MainWindow::toggleDetails);

    exportDirectory_ = QDir::homePath() + "/FileRelicReports";
    updateWorkflowState();
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
        haveReport_ = false;
        caseActions_.clear();
        automaticManifestPath_.clear();
        candidateList_->clear();
        partitionSelector_->clear();
        rawLog_->clear();
        clearResults();
        addResultCard("Image selected", "Select Analyze image to inspect this disk image read-only.");
        setStatusMessage("Image selected — ready for read-only inspection.");
        updateWorkflowState();
    }
}

void MainWindow::updateWorkflowState() {
    const bool haveImage = !selectedImagePath_.isEmpty();
    const bool busy = inspectionWatcher_->isRunning() || extractionWatcher_->isRunning()
        || manifestWatcher_->isRunning() || candidatesWatcher_->isRunning();
    const bool canBrowse = haveReport_ && report_.succeeded() && !busy
        && partitionSelector_->count() != 0;
    chooseButton_->setEnabled(!busy);
    analyzeButton_->setEnabled(haveImage && !busy);
    destinationButton_->setEnabled(haveImage && !busy);
    reportButton_->setEnabled(haveImage && haveReport_ && !busy);
    partitionSelector_->setEnabled(canBrowse);
    candidatePath_->setEnabled(canBrowse);
    refreshCandidatesButton_->setEnabled(canBrowse);
    upDirectoryButton_->setEnabled(canBrowse && currentDirectory_ != "/");
    showDeleted_->setEnabled(canBrowse);
    candidateList_->setEnabled(canBrowse);
    const QListWidgetItem* selectedCandidate = candidateList_->currentItem();
    const bool hasExtractableSelection = selectedCandidate != nullptr
        && selectedCandidate->flags().testFlag(Qt::ItemIsSelectable)
        && selectedCandidate->data(candidate_path_role).isValid();
    extractButton_->setEnabled(canBrowse && hasExtractableSelection);
    exportLocation_->setText("Output: " + exportDirectory_);
    if (busy) {
        workflowProgress_->setRange(0, 0);
    } else {
        workflowProgress_->setRange(0, 100);
        workflowProgress_->setValue(haveReport_ ? 100 : 0);
    }
    workflowProgress_->setVisible(haveImage);
}

void MainWindow::setStatusMessage(const QString& message, const QString& tone) {
    status_->setText(message);
    status_->setProperty("tone", tone);
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
}

void MainWindow::chooseOutputDirectory() {
    QString directory = QFileDialog::getExistingDirectory(
        this,
        "Choose the case-output directory",
        exportDirectory_);
    if (!directory.isEmpty()) {
        exportDirectory_ = directory;
        automaticManifestPath_.clear();
        bool saved = true;
        if (haveReport_) {
            saved = saveAutomaticManifest();
        }
        updateWorkflowState();
        setStatusMessage(haveReport_
                             ? (saved ? "Saving investigation manifest to " + automaticManifestPath_
                                      : "Could not start saving the investigation manifest.")
                             : "Output directory ready — the investigation manifest will be saved after inspection.",
                         saved ? QString() : QStringLiteral("problem"));
    }
}

void MainWindow::exportCaseReport() {
    if (selectedImagePath_.isEmpty() || !haveReport_) {
        return;
    }
    const QString manifestPath = QFileDialog::getSaveFileName(
        this,
        "Save JSON case manifest",
        exportDirectory_ + "/" + QFileInfo(selectedImagePath_).completeBaseName() + ".manifest.json",
        "JSON files (*.json);;All files (*)");
    if (manifestPath.isEmpty()) {
        return;
    }
    const std::filesystem::path output = nativePath(manifestPath);
    const filerelic::CaseManifestOptions options{
        "GUI case",
        "GUI user",
        output.parent_path().string(),
        caseActions_,
    };
    startManifestWrite(manifestPath, options, false);
}

void MainWindow::analyzeImage() {
    if (!selectedImagePath_.isEmpty()) {
        inspectImage(selectedImagePath_);
    }
}

void MainWindow::inspectImage(const QString& imagePath) {
    selectedImage_->setText(imagePath);
    setStatusMessage("Inspecting read-only…");
    rawReport_.clear();
    rawLog_->clear();
    rawLogToggle_->setChecked(false);
    haveReport_ = false;
    caseActions_.clear();
    automaticManifestPath_.clear();
    candidateList_->clear();
    partitionSelector_->clear();
    clearResults();
    addResultCard("Inspection in progress", "Reading the selected image in read-only mode.");
    const std::filesystem::path nativePath = ::nativePath(imagePath);
    inspectionWatcher_->setFuture(QtConcurrent::run([nativePath] {
        return filerelic::inspect_image(nativePath);
    }));
    updateWorkflowState();
}

void MainWindow::inspectionFinished() {
    report_ = inspectionWatcher_->result();
    haveReport_ = true;
    const filerelic::InspectionReport& report = report_;
    // The Advanced log is the scanner's step trace; all results shown in the
    // cards come from the typed report fields, not from this text.
    rawReport_ = QString::fromStdString(filerelic::render_trace(report));
    rawLog_->setPlainText(rawReport_);
    populateFileSystems();
    const bool saveStarted = saveAutomaticManifest();
    updateWorkflowState();
    setStatusMessage(saveStarted
        ? "Inspection complete — saving investigation manifest to " + automaticManifestPath_
        : "Could not start saving the investigation manifest. Check the selected output folder.",
        saveStarted ? QString() : QStringLiteral("problem"));
    renderResults(report);
}

void MainWindow::toggleDetails(bool) {
    if (haveReport_) {
        renderResults(report_);
    }
}

void MainWindow::loadImage(const QString& imagePath) {
    const QFileInfo info(imagePath);
    selectedImagePath_ = imagePath;
    imageDetails_->setText(info.fileName() + "  •  " + formatSize(info.size()));
    selectedImage_->setText(imagePath);
    haveReport_ = false;
    caseActions_.clear();
    automaticManifestPath_.clear();
    updateWorkflowState();
    inspectImage(imagePath);
}

void MainWindow::populateFileSystems() {
    const QSignalBlocker blocker(partitionSelector_);
    partitionSelector_->clear();
    for (const filerelic::PartitionInfo& partition : report_.partitions) {
        if (partition.file_system.kind != filerelic::FileSystemKind::ext_family
            || !partition.range_valid) {
            continue;
        }
        const QString label = "Partition " + QString::number(partition.number) + " ("
            + QString::fromStdString(filerelic::describe_file_system(partition.file_system)) + ")";
        partitionSelector_->addItem(label, QVariant::fromValue<qulonglong>(partition.number));
    }
    currentDirectory_ = "/";
    candidatePath_->setText(currentDirectory_);
    candidateList_->clear();
    if (partitionSelector_->count() == 0 && report_.succeeded()) {
        setStatusMessage("Inspection complete — no supported ext partition is available to browse.",
                         "warning");
    } else if (partitionSelector_->count() != 0 && report_.succeeded()) {
        refreshCandidates();
    }
}

void MainWindow::refreshCandidates() {
    candidateList_->clear();
    if (!haveReport_ || !report_.succeeded() || partitionSelector_->currentIndex() < 0) {
        updateWorkflowState();
        return;
    }

    QString path = candidatePath_->text().trimmed();
    if (path.isEmpty()) path = "/";
    if (!path.startsWith('/')) path.prepend('/');
    currentDirectory_ = path;
    candidatePath_->setText(path);

    const auto image = nativePath(selectedImagePath_);
    const auto partition = partitionSelector_->currentData().toULongLong();
    const bool includeDeleted = showDeleted_->isChecked();
    pendingBrowsePath_ = path;
    setStatusMessage(includeDeleted ? "Loading folder and deleted-name candidates…"
                                    : "Loading folder…");
    candidatesWatcher_->setFuture(QtConcurrent::run([image, partition, path, includeDeleted] {
        CandidateLoadResult result;
        result.live = filerelic::list_path(image, partition, path.toStdString());
        result.included_deleted = includeDeleted;
        if (includeDeleted && result.live.ok
            && result.live.kind == filerelic::InodeKind::directory) {
            result.deleted = filerelic::list_deleted_path(image, partition, path.toStdString());
        }
        return result;
    }));
    updateWorkflowState();
}

void MainWindow::candidatesFinished() {
    const CandidateLoadResult loaded = candidatesWatcher_->result();
    const QString path = pendingBrowsePath_;
    const auto& listing = loaded.live;
    if (!listing.ok) {
        auto* item = new QListWidgetItem("Could not list folder: " + QString::fromStdString(listing.error),
                                         candidateList_);
        item->setFlags(Qt::NoItemFlags);
        setStatusMessage("Could not read the selected folder; see the message in the file list.", "problem");
        updateWorkflowState();
        return;
    }
    if (listing.kind != filerelic::InodeKind::directory) {
        auto* item = new QListWidgetItem("The selected path is not a folder.", candidateList_);
        item->setFlags(Qt::NoItemFlags);
        updateWorkflowState();
        return;
    }

    for (const filerelic::DirectoryEntryInfo& entry : listing.listing.entries) {
        if (entry.name == "." || entry.name == "..") continue;
        QString fullPath = path;
        if (!fullPath.endsWith('/')) fullPath += '/';
        fullPath += QString::fromStdString(entry.name);
        const bool directory = entry.inode.kind == filerelic::InodeKind::directory;
        QString label = directory ? "[Folder] " : "[File] ";
        label += QString::fromStdString(entry.name);
        if (entry.inode.size_bytes && !directory) {
            label += "  (" + QString::fromStdString(filerelic::format_size_short(*entry.inode.size_bytes)) + ")";
        }
        auto* item = new QListWidgetItem(label, candidateList_);
        item->setIcon(style()->standardIcon(directory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon));
        item->setData(candidate_path_role, fullPath);
        item->setData(candidate_inode_role, entry.inode_number);
        item->setData(candidate_deleted_role, false);
        item->setData(candidate_directory_role, directory);
    }

    if (loaded.included_deleted) {
        const auto& deleted = loaded.deleted;
        if (!deleted.ok) {
            auto* item = new QListWidgetItem("Deleted names unavailable: " + QString::fromStdString(deleted.error),
                                             candidateList_);
            item->setFlags(Qt::NoItemFlags);
        } else {
            for (const filerelic::DeletedEntryInfo& entry : deleted.listing.entries) {
                QString fullPath = path;
                if (!fullPath.endsWith('/')) fullPath += '/';
                fullPath += QString::fromStdString(entry.name);
                QString label = "[Deleted candidate] " + QString::fromStdString(entry.name)
                    + " — " + QString::fromStdString(filerelic::describe_deleted_status(entry.status));
                auto* item = new QListWidgetItem(label, candidateList_);
                item->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
                item->setData(candidate_path_role, fullPath);
                item->setData(candidate_inode_role, entry.inode_number);
                item->setData(candidate_deleted_role, true);
                item->setData(candidate_directory_role, entry.kind == filerelic::InodeKind::directory);
                item->setToolTip(QString::fromStdString(entry.note));
                if (entry.kind != filerelic::InodeKind::regular_file) {
                    item->setFlags(Qt::NoItemFlags);
                    item->setText(label + " — only deleted regular files can be recovered");
                }
            }
            if (!deleted.listing.limitation.empty()) {
                auto* item = new QListWidgetItem(
                    "Warning: deleted-name scan may be incomplete — "
                        + QString::fromStdString(deleted.listing.limitation), candidateList_);
                item->setFlags(Qt::NoItemFlags);
            }
        }
    }
    if (candidateList_->count() == 0) {
        auto* item = new QListWidgetItem("No entries in this folder.", candidateList_);
        item->setFlags(Qt::NoItemFlags);
    }
    setStatusMessage("Folder loaded — select an item to extract; double-click a folder to open it.",
                     "success");
    updateWorkflowState();
}

void MainWindow::navigateUp() {
    QString path = candidatePath_->text().trimmed();
    while (path.size() > 1 && path.endsWith('/')) path.chop(1);
    const qsizetype separator = path.lastIndexOf('/');
    path = separator <= 0 ? "/" : path.left(separator);
    candidatePath_->setText(path);
    refreshCandidates();
}

void MainWindow::extractSelectedCandidate() {
    QListWidgetItem* item = candidateList_->currentItem();
    if (item == nullptr || item->flags() == Qt::NoItemFlags) return;

    pendingExtractionPath_ = item->data(candidate_path_role).toString();
    pendingExtractionDeleted_ = item->data(candidate_deleted_role).toBool();
    const std::uint32_t inodeNumber = item->data(candidate_inode_role).toUInt();
    const bool isDirectory = item->data(candidate_directory_role).toBool();
    pendingExtractionDirectory_ = isDirectory && !pendingExtractionDeleted_;
    const std::filesystem::path image = nativePath(selectedImagePath_);
    const std::filesystem::path outputDirectory = nativePath(exportDirectory_);
    const std::uint64_t partition = partitionSelector_->currentData().toULongLong();
    const std::string sourcePath = pendingExtractionPath_.toStdString();
    const bool deleted = pendingExtractionDeleted_;
    setStatusMessage("Extracting selected item to the chosen output folder…");
    workflowProgress_->setRange(0, 0);
    extractionWatcher_->setFuture(QtConcurrent::run(
        [image, outputDirectory, partition, sourcePath, inodeNumber, deleted, isDirectory] {
            if (deleted) {
                return filerelic::extract_deleted_file(image, partition, sourcePath,
                                                       inodeNumber, outputDirectory);
            }
            if (isDirectory) {
                return filerelic::extract_directory(image, partition, sourcePath, outputDirectory);
            }
            return filerelic::extract_file(image, partition, sourcePath, outputDirectory);
        }));
    updateWorkflowState();
}

void MainWindow::extractionFinished() {
    const filerelic::ExtractionResult result = extractionWatcher_->result();
    const char* status = result.read.status == filerelic::ReadStatus::complete ? "complete"
        : result.read.status == filerelic::ReadStatus::partial ? "partial" : "failed";
    std::string note = result.read.error;
    for (const std::string& entry : result.read.notes) {
        if (!note.empty()) note += "; ";
        note += entry;
    }
    if (pendingExtractionDirectory_) {
        if (!note.empty()) note += "; ";
        note += "directory extraction does not currently aggregate per-file byte counts or hashes";
    }
    caseActions_.push_back({
        pendingExtractionDeleted_ ? "recover_deleted" : "extract",
        pendingExtractionPath_.toStdString(),
        status,
        result.output_path.string(),
        pendingExtractionDirectory_ ? std::nullopt
                                    : std::optional<std::uint64_t>(result.read.bytes_delivered),
        pendingExtractionDirectory_ ? std::string() : result.read.sha256_hex,
        note,
    });
    const bool manifestSaveStarted = saveAutomaticManifest();
    QString resultText = "Result: ";
    resultText += result.read.status == filerelic::ReadStatus::complete ? "COMPLETE"
        : result.read.status == filerelic::ReadStatus::partial ? "PARTIAL" : "FAILED";
    if (!result.output_path.empty()) {
        resultText += "\nSaved to: " + qPath(result.output_path);
    }
    if (pendingExtractionDirectory_) {
        resultText += "\nDirectory tree extracted; byte count and hash are not aggregated.";
    } else {
        resultText += "\nBytes written: " + QString::number(result.read.bytes_delivered)
            + "\nSHA-256: " + QString::fromStdString(result.read.sha256_hex);
    }
    if (!note.empty()) resultText += "\nNote: " + QString::fromStdString(note);
    if (!manifestSaveStarted) resultText += "\nWarning: could not start updating the investigation manifest.";
    const QString tone = result.read.status == filerelic::ReadStatus::complete ? "success"
        : result.read.status == filerelic::ReadStatus::partial ? "warning" : "problem";
    addResultCard("Extraction result — " + pendingExtractionPath_, resultText, tone);
    updateWorkflowState();
    refreshCandidates();
    if (!manifestSaveStarted) {
        setStatusMessage("Extraction finished, but could not start updating the investigation manifest.", "problem");
    } else if (result.read.status == filerelic::ReadStatus::complete) {
        setStatusMessage("Extraction complete — saving result and SHA-256 to the investigation manifest.", "success");
    } else if (result.read.status == filerelic::ReadStatus::partial) {
        setStatusMessage("Extraction is PARTIAL — saving result and notes to the investigation manifest.", "warning");
    } else {
        setStatusMessage("Extraction failed — details are recorded in the investigation manifest.", "problem");
    }
}

bool MainWindow::saveAutomaticManifest() {
    if (!haveReport_) return false;
    if (automaticManifestPath_.isEmpty()) {
        automaticManifestPath_ = createAutomaticManifestPath(exportDirectory_, selectedImagePath_);
    }
    const filerelic::CaseManifestOptions options{
        "GUI investigation",
        "GUI user",
        exportDirectory_.toStdString(),
        caseActions_,
    };
    startManifestWrite(automaticManifestPath_, options, true);
    return true;
}

void MainWindow::startManifestWrite(const QString& path,
                                   const filerelic::CaseManifestOptions& options,
                                   bool automatic) {
    pendingManifestAutomatic_ = automatic;
    const std::filesystem::path output = nativePath(path);
    const filerelic::InspectionReport report = report_;
    const filerelic::CaseManifestOptions manifestOptions = options;
    manifestWatcher_->setFuture(QtConcurrent::run([report, output, manifestOptions, path] {
        return filerelic::write_case_manifest_json(report, output, manifestOptions)
            ? path : QString();
    }));
    setStatusMessage(automatic ? "Saving investigation manifest…" : "Exporting case manifest…");
    updateWorkflowState();
}

void MainWindow::manifestWriteFinished() {
    const QString writtenPath = manifestWatcher_->result();
    if (writtenPath.isEmpty()) {
        if (pendingManifestAutomatic_) automaticManifestPath_.clear();
        setStatusMessage("Could not write the case manifest. Check the output path and permissions.", "problem");
    } else {
        setStatusMessage("Case manifest saved to " + writtenPath, "success");
    }
    updateWorkflowState();
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

void MainWindow::addSectionCard(const filerelic::ReportSection& section) {
    using filerelic::Severity;
    const bool technical = detailsToggle_->isChecked();
    QString tone;
    switch (section.severity) {
        case Severity::good:    tone = QStringLiteral("success"); break;
        case Severity::warning: tone = QStringLiteral("warning"); break;
        case Severity::problem: tone = QStringLiteral("problem"); break;
        case Severity::info:    break;
    }
    auto* card = new QFrame(resultsContainer_);
    card->setObjectName("resultCard");
    if (!tone.isEmpty()) card->setProperty("tone", tone);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(6);

    auto* heading = new QLabel(QString::fromStdString(section.title), card);
    heading->setObjectName("resultTitle");
    heading->setWordWrap(true);
    layout->addWidget(heading);

    const auto makeText = [card](const QString& text, const char* name) {
        auto* label = new QLabel(text, card);
        label->setObjectName(name);
        label->setWordWrap(true);
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };

    if (!section.summary.empty()) {
        layout->addWidget(makeText(QString::fromStdString(section.summary), "resultContent"));
    }

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(1, 1);
    int row = 0;
    for (const filerelic::ReportRow& item : section.rows) {
        if (item.technical && !technical) continue;
        QString value = QString::fromStdString(item.value);
        auto* label = makeText(QString::fromStdString(item.label), "rowLabel");
        label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        grid->addWidget(label, row, 0, Qt::AlignTop);
        grid->addWidget(makeText(value, "rowValue"), row, 1);
        ++row;
        if (technical && !item.detail.empty()) {
            grid->addWidget(makeText(QString::fromStdString(item.detail), "rowTech"), row, 1);
            ++row;
        }
    }
    if (row > 0) layout->addLayout(grid);

    for (const std::string& note : section.notes) {
        layout->addWidget(makeText(QString::fromUtf8("•  ") + QString::fromStdString(note), "resultContent"));
    }
    resultsLayout_->addWidget(card);
}

void MainWindow::renderResults(const filerelic::InspectionReport& report) {
    clearResults();
    for (const auto& section : report.sections) {
        addSectionCard(section);
    }
    resultsLayout_->addStretch();
}
