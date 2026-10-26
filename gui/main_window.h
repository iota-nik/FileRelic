#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QString>

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
    void appendOutput();
    void inspectionFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void toggleRawLog(bool visible);

private:
    QString cliPath() const;
    void inspectImage(const QString& imagePath);
    void renderResults(bool succeeded);
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
    QProcess* process_;
    QString selectedImagePath_;
    QString rawReport_;
};
