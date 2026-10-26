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
    void appendOutput();
    void inspectionFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QString cliPath() const;
    void inspectImage(const QString& imagePath);

    QLabel* selectedImage_;
    QLabel* status_;
    QPushButton* chooseButton_;
    QTextEdit* report_;
    QProcess* process_;
};
