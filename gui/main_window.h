#pragma once

#include <QMainWindow>
#include <QFutureWatcher>
#include <QProgressBar>
#include <QString>

#include "case_manifest.h"
#include "image_inspector.h"

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class QWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    // Selects and inspects an image (used by `filerelic-gui <image>`).
    void loadImage(const QString& imagePath);

private slots:
    void chooseImage();
    void analyzeImage();
    void chooseOutputDirectory();
    void exportCaseReport();
    void inspectionFinished();
    void toggleRawLog(bool visible);
    void toggleDetails(bool shown);

private:
    void inspectImage(const QString& imagePath);
    void renderResults(const filerelic::InspectionReport& report);
    void clearResults();
    void addSectionCard(const filerelic::ReportSection& section);
    void addResultCard(const QString& title, const QString& content, const QString& tone = {});
    void updateWorkflowState();

    QLabel* selectedImage_;
    QLabel* imageDetails_;
    QLabel* status_;
    QPushButton* chooseButton_;
    QPushButton* analyzeButton_;
    QPushButton* reportButton_;
    QPushButton* destinationButton_;
    QPlainTextEdit* rawLog_;
    QToolButton* rawLogToggle_;
    QCheckBox* detailsToggle_;
    QProgressBar* workflowProgress_;
    QLabel* exportLocation_;
    QWidget* resultsContainer_;
    QVBoxLayout* resultsLayout_;
    QFutureWatcher<filerelic::InspectionReport>* inspectionWatcher_;
    QString selectedImagePath_;
    QString exportDirectory_;
    QString rawReport_;
    filerelic::InspectionReport report_;
    bool haveReport_ = false;
};
