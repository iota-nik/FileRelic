#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QString>

class QLabel;
class QPushButton;
class QTextEdit;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void chooseImage();
    void analyzeImage();
    void appendOutput();
    void inspectionFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QString cliPath() const;
    void inspectImage(const QString& imagePath);

    QLabel* selectedImage_;
    QLabel* imageDetails_;
    QLabel* status_;
    QPushButton* chooseButton_;
    QPushButton* analyzeButton_;
    QTextEdit* report_;
    QProcess* process_;
    QString selectedImagePath_;
};
