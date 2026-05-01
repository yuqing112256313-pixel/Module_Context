#include "mainwindow.h"
#include "taskflowdemotypes.h"

#include <QtWidgets/QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    qRegisterMetaType<QueueStatus>("QueueStatus");
    qRegisterMetaType<DemoSettings>("DemoSettings");
    qRegisterMetaType<TaskUiState>("TaskUiState");
    qRegisterMetaType<ProgressState>("ProgressState");
    qRegisterMetaType<RunResultState>("RunResultState");

    MainWindow window;
    window.show();
    return app.exec();
}
