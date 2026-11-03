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
#include <QLayoutItem>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
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
      refreshCandidatesButton_(new QPushButton("Open folder", this)),
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
      manifestWatcher_(new QFutureWatcher<QString>(this)) {
    setWindowTitle("FileRelic — Disk Image Inspector and Recovery");
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
    layout->addWidget(imageCard);

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
    browseControls->addWidget(new QLabel("Partition", browserCard));
    browseControls->addWidget(partitionSelector_);
    browseControls->addWidget(candidatePath_, 1);
    browseControls->addWidget(refreshCandidatesButton_);
    browserLayout->addLayout(browseControls);
    auto* candidateControls = new QHBoxLayout();
    candidateControls->addWidget(showDeleted_);
    candidateControls->addStretch();
    candidateControls->addWidget(extractButton_);
    browserLayout->addLayout(candidateControls);
    candidateList_->setMinimumHeight(100);
    candidateList_->setMaximumHeight(160);
    candidateList_->setEnabled(false);
    browserLayout->addWidget(candidateList_);
    auto* browserNote = new QLabel(
        "File browsing and extraction are available for ext2/3/4 partitions only. "
        "Deleted-name matches are candidates, not proof of recoverability.",
        browserCard);
    browserNote->setObjectName("muted");
    browserNote->setWordWrap(true);
    browserLayout->addWidget(browserNote);
    layout->addWidget(browserCard);

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
        QFrame#resultCard[tone="problem"] { background: #2d1417; border-color: #c04b55; }
        QFrame#resultCard[tone="problem"] QLabel#resultTitle { color: #ffb3b9; }
        QLabel#rowLabel { color: #8fa3b8; font-size: 13px; }
        QLabel#rowValue { color: #e3ecf5; font-size: 14px; }
        QLabel#rowTech { color: #7f93a8; font-size: 12px; }
        QCheckBox#detailsToggle { color: #a8c9e9; font-size: 13px; }
        QCheckBox { color: #a8c9e9; }
        QComboBox, QLineEdit, QListWidget { background: #0c1118; border: 1px solid #273747;
            border-radius: 5px; color: #e3ecf5; padding: 6px; }
        QListWidget::item:selected { background: #236a9f; }
        QLabel#resultTitle { color: #edf5fc; font-size: 16px; font-weight: 700; }
        QLabel#resultContent { color: #c4d1df; font-size: 14px; line-height: 1.35; }
        QToolButton#rawLogToggle { color: #a8c9e9; border: none; font-size: 13px;
            font-weight: 600; padding: 7px 0; text-align: left; }
        QPlainTextEdit#rawLog { background: #0c1118; border: 1px solid #273747; border-radius: 6px;
            color: #d9e5f0; font-family: monospace; font-size: 13px; padding: 10px; }
    )");

    connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::chooseImage);
    connect(analyzeButton_, &QPushButton::clicked, this, &MainWindow::analyzeImage);
    connect(destinationButton_, &QPushButton::clicked, this, &MainWindow::chooseOutputDirectory);
    connect(reportButton_, &QPushButton::clicked, this, &MainWindow::exportCaseReport);
    connect(refreshCandidatesButton_, &QPushButton::clicked, this, &MainWindow::refreshCandidates);
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
        status_->setText("Image selected — ready for read-only inspection.");
        updateWorkflowState();
    }
}

void MainWindow::updateWorkflowState() {
    const bool haveImage = !selectedImagePath_.isEmpty();
    const bool busy = inspectionWatcher_->isRunning() || extractionWatcher_->isRunning()
        || manifestWatcher_->isRunning();
    const bool canBrowse = haveReport_ && report_.succeeded() && !busy
        && partitionSelector_->count() != 0;
    chooseButton_->setEnabled(!busy);
    analyzeButton_->setEnabled(haveImage && !busy);
    destinationButton_->setEnabled(haveImage && !busy);
    reportButton_->setEnabled(haveImage && haveReport_ && !busy);
    partitionSelector_->setEnabled(canBrowse);
    candidatePath_->setEnabled(canBrowse);
    refreshCandidatesButton_->setEnabled(canBrowse);
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
        status_->setText(haveReport_
                             ? (saved ? "Saving investigation manifest to " + automaticManifestPath_
                                      : "Could not start saving the investigation manifest.")
                             : "Output directory ready — the investigation manifest will be saved after inspection.");
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
    status_->setText("Inspecting read-only…");
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
    status_->setText(saveStarted
        ? "Inspection complete — saving investigation manifest to " + automaticManifestPath_
        : "Could not start saving the investigation manifest. Check the selected output folder.");
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
        status_->setText("Inspection complete — no supported ext partition is available to browse.");
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
    const auto listing = filerelic::list_path(image, partition, path.toStdString());
    if (!listing.ok) {
        auto* item = new QListWidgetItem("Could not list folder: " + QString::fromStdString(listing.error),
                                         candidateList_);
        item->setFlags(Qt::NoItemFlags);
        status_->setText("Could not read the selected folder; see the message in the file list.");
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
        item->setData(candidate_path_role, fullPath);
        item->setData(candidate_inode_role, entry.inode_number);
        item->setData(candidate_deleted_role, false);
        item->setData(candidate_directory_role, directory);
    }

    if (showDeleted_->isChecked()) {
        const auto deleted = filerelic::list_deleted_path(image, partition, path.toStdString());
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
    status_->setText("Folder loaded — select a file, folder, or deleted-name candidate.");
    updateWorkflowState();
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
    status_->setText("Extracting selected item to the chosen output folder…");
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
        status_->setText("Extraction finished, but could not start updating the investigation manifest.");
    } else if (result.read.status == filerelic::ReadStatus::complete) {
        status_->setText("Extraction complete — saving result and SHA-256 to the investigation manifest.");
    } else if (result.read.status == filerelic::ReadStatus::partial) {
        status_->setText("Extraction is PARTIAL — saving result and notes to the investigation manifest.");
    } else {
        status_->setText("Extraction failed — details are recorded in the investigation manifest.");
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
    status_->setText(automatic ? "Saving investigation manifest…" : "Exporting case manifest…");
    updateWorkflowState();
}

void MainWindow::manifestWriteFinished() {
    const QString writtenPath = manifestWatcher_->result();
    if (writtenPath.isEmpty()) {
        if (pendingManifestAutomatic_) automaticManifestPath_.clear();
        status_->setText("Could not write the case manifest. Check the output path and permissions.");
    } else {
        status_->setText("Case manifest saved to " + writtenPath);
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
