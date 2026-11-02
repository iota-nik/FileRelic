#include <QApplication>
#include <QStringList>

#include "main_window.h"

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    MainWindow window;
    window.show();
    const QStringList arguments = application.arguments();
    if (arguments.size() > 1) {
        window.loadImage(arguments.at(1));   // optional: filerelic-gui <image>
    }
    return application.exec();
}
