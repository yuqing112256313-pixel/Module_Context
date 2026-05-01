#pragma once

#include "taskflowdemotypes.h"

#include "../task_flow/master_runner.h"

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <atomic>

class QProcess;

class MasterWorker : public QObject {
    Q_OBJECT

public:
    explicit MasterWorker(QObject* parent = NULL);

public slots:
    void Run(const DemoSettings& settings);
    void RequestStop();

signals:
    void LogMessage(const QString& level, const QString& message);
    void QueueStatusChanged(const QueueStatus& status);
    void TaskPublished(const TaskUiState& task);
    void TaskUpdated(const TaskUiState& task);
    void ProgressChanged(const ProgressState& progress);
    void PreviewChanged(
        const QString& image_path,
        const QString& image_name,
        qint64 image_bytes,
        int source_index);
    void Finished(const RunResultState& result);

private:
    bool PrepareRuntime(
        const DemoSettings& settings,
        QString* report_dir,
        QString* module_config_path,
        QString* run_id,
        QString* error_message);
    bool WriteMasterModuleConfig(
        const DemoSettings& settings,
        const QString& module_config_path,
        QString* error_message) const;
    bool WriteLocalWorkerModuleConfig(
        const DemoSettings& settings,
        const QString& worker_id,
        const QString& worker_config_path,
        QString* error_message) const;
    bool StartLocalWorkers(
        const DemoSettings& settings,
        const QString& report_dir,
        QList<QProcess*>* workers,
        QString* error_message);
    QString ResolvePluginPath(
        const DemoSettings& settings,
        const QString& file_name) const;
    QString ResolveApplicationFile(const QStringList& names) const;
    QString BuildRabbitMqUri(const DemoSettings& settings) const;
    QString BuildWorkerRabbitMqUri(const DemoSettings& settings) const;
    module_context::examples::task_flow::MasterRunConfig BuildRunConfig(
        const DemoSettings& settings,
        const QString& module_config_path,
        const QString& report_dir,
        const QString& run_id) const;

    std::atomic_bool cancel_requested_;
};
