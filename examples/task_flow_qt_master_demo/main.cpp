#include "mainwindow.h"
#include "taskflowdemotypes.h"

#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtCore/QSize>
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

QSize ScreenshotSizeFromArgs(int argc, char* argv[]) {
    const QByteArray size_prefix("--screenshot-size=");
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        QByteArray value;
        if (arg == "--screenshot-size" && i + 1 < argc) {
            value = argv[i + 1];
        } else if (arg.startsWith(size_prefix)) {
            value = arg.mid(size_prefix.size());
        }
        if (value.isEmpty()) {
            continue;
        }

        const int separator = value.indexOf('x');
        if (separator <= 0) {
            continue;
        }
        const int width = value.left(separator).toInt();
        const int height = value.mid(separator + 1).toInt();
        if (width >= 800 && height >= 600) {
            return QSize(width, height);
        }
    }
    return QSize(1920, 1080);
}

bool SaveWindowScreenshot(MainWindow* window, const QString& path, const QSize& size) {
    const QFileInfo file_info(path);
    const QDir directory = file_info.absoluteDir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        return false;
    }

    window->setAttribute(Qt::WA_DontShowOnScreen, true);
    window->setMinimumSize(size);
    window->setMaximumSize(size);
    window->resize(size);
    window->ensurePolished();
    window->show();
    QApplication::processEvents(QEventLoop::AllEvents, 100);
    window->resize(size);
    QApplication::processEvents(QEventLoop::AllEvents, 100);

    QPixmap pixmap(size);
    pixmap.fill(window->palette().color(QPalette::Window));
    window->render(&pixmap);
    return pixmap.save(path, "PNG");
}

}  // namespace

int main(int argc, char* argv[]) {
    const QString screenshot_path = ScreenshotPathFromArgs(argc, argv);
    const QSize screenshot_size = ScreenshotSizeFromArgs(argc, argv);

    QApplication app(argc, argv);

    qRegisterMetaType<QueueStatus>("QueueStatus");
    qRegisterMetaType<DemoSettings>("DemoSettings");
    qRegisterMetaType<TaskUiState>("TaskUiState");
    qRegisterMetaType<ProgressState>("ProgressState");
    qRegisterMetaType<RunResultState>("RunResultState");

    MainWindow window;
    if (!screenshot_path.isEmpty()) {
        QTimer::singleShot(500, &app, [&window, screenshot_path, screenshot_size, &app]() {
            app.exit(SaveWindowScreenshot(&window, screenshot_path, screenshot_size) ? 0 : 2);
        });
        return app.exec();
    }

    window.show();
    return app.exec();
}
