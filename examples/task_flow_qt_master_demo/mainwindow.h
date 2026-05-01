#pragma once

#include "taskflowdemotypes.h"

#include <QtCore/QMap>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QMainWindow>

namespace Ui {
class MainWindow;
}

class MasterWorker;
class QThread;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = NULL);
    ~MainWindow();

private slots:
    void SelectImageDirectory();
    void SelectOutputDirectory();
    void SelectPluginDirectory();
    void StartRun();
    void StopRun();
    void ClearRuntime();
    void ClearLog();
    void OnLogMessage(const QString& level, const QString& message);
    void OnQueueStatus(const QueueStatus& status);
    void OnTaskPublished(const TaskUiState& task);
    void OnTaskUpdated(const TaskUiState& task);
    void OnProgress(const ProgressState& progress);
    void OnPreviewChanged(
        const QString& image_path,
        const QString& image_name,
        qint64 image_bytes,
        int source_index);
    void OnRunFinished(const RunResultState& result);
    void UpdateClock();

private:
    void SetupDynamicControls();
    void SetupTables();
    void LoadSettings();
    void SaveSettings();
    DemoSettings ReadSettings() const;
    void SetRunning(bool running);
    void SetRunState(const QString& state, const QString& text);
    void SetRabbitState(bool connected, const QString& text);
    void SetRuntimeState(bool normal, const QString& text);
    void AddLogRow(const QString& level, const QString& message);
    int EnsureTaskRow(const TaskUiState& task);
    void UpdateTaskRow(int row, const TaskUiState& task);
    void UpdatePreview(const QString& image_path);
    QString HostIpText() const;
    qint64 DirectorySize(const QString& path) const;

    Ui::MainWindow* ui_;
    QSettings settings_;
    QGraphicsScene preview_scene_;
    QGraphicsScene result_scene_;
    QTimer clock_timer_;
    QThread* worker_thread_;
    MasterWorker* worker_;
    QMap<QString, int> task_rows_;
    QString image_directory_;
    QString current_preview_path_;
};
