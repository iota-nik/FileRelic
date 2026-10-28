#pragma once

#include <QMainWindow>
#include <QFutureWatcher>
#include <QString>

#include "image_inspector.h"

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

private slots:
    void chooseImage();
    void analyzeImage();
    void inspectionFinished();
    void toggleRawLog(bool visible);

private:
    void inspectImage(const QString& imagePath);
    void renderResults(const filerelic::InspectionReport& report);
    void clearResults();
    void addResultCard(const QString& title, const QString& content, const QString& tone = {});

    QLabel* selectedImage_;
    QLabel* imageDetails_;
    QLabel* status_;
    QPushButton* chooseButton_;
    QPushButton* analyzeButton_;
    QPlainTextEdit* rawLog_;
    QToolButton* rawLogToggle_;
    QWidget* resultsContainer_;
    QVBoxLayout* resultsLayout_;
    QFutureWatcher<filerelic::InspectionReport>* inspectionWatcher_;
    QString selectedImagePath_;
    QString rawReport_;
};
