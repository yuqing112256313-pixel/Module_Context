#include "mainwindow.h"
#include "taskflowdemotypes.h"

#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtGui/QPalette>
#include <QtGui/QPixmap>

namespace {

QString ScreenshotPathFromArgs(int argc, char* argv[]) {
    const QByteArray screenshot_prefix("--screenshot=");
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--screenshot" && i + 1 < argc) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
        if (arg.startsWith(screenshot_prefix)) {
            return QString::fromLocal8Bit(arg.mid(screenshot_prefix.size()));
        }
    }
    return QString();
}

bool SaveWindowScreenshot(MainWindow* window, const QString& path) {
    const QFileInfo file_info(path);
    const QDir directory = file_info.absoluteDir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        return false;
    }

    window->resize(1280, 900);
    window->ensurePolished();
    window->show();
    QApplication::processEvents();

    QPixmap pixmap(window->size());
    pixmap.fill(window->palette().color(QPalette::Window));
    window->render(&pixmap);
    return pixmap.save(path, "PNG");
}

}  // namespace

int main(int argc, char* argv[]) {
    const QString screenshot_path = ScreenshotPathFromArgs(argc, argv);
    if (!screenshot_path.isEmpty() && qgetenv("QT_QPA_PLATFORM").isEmpty()) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }

    QApplication app(argc, argv);

    qRegisterMetaType<QueueStatus>("QueueStatus");
    qRegisterMetaType<DemoSettings>("DemoSettings");
    qRegisterMetaType<TaskUiState>("TaskUiState");
    qRegisterMetaType<ProgressState>("ProgressState");
    qRegisterMetaType<RunResultState>("RunResultState");

    MainWindow window;
    if (!screenshot_path.isEmpty()) {
        QTimer::singleShot(500, &app, [&window, screenshot_path, &app]() {
            app.exit(SaveWindowScreenshot(&window, screenshot_path) ? 0 : 2);
        });
        return app.exec();
    }

    window.show();
    return app.exec();
}
