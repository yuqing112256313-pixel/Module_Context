#include "masterworker.h"

#include "bmpframesource.h"
#include "rabbitmqadminclient.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>
#include <QtCore/QUuid>

using module_context::examples::task_flow::IMasterRunObserver;
using module_context::examples::task_flow::MasterRunConfig;
using module_context::examples::task_flow::MasterRunProgress;
using module_context::examples::task_flow::MasterRunResult;
using module_context::examples::task_flow::MasterSourceFrame;
using module_context::examples::task_flow::MasterTaskSnapshot;
using module_context::examples::task_flow::RunTaskFlowMaster;

namespace {

QString FromStdString(const std::string& value) {
    return QString::fromUtf8(value.c_str());
}

std::string ToStdString(const QString& value) {
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

TaskUiState ToTaskUiState(const MasterTaskSnapshot& snapshot) {
    TaskUiState state;
    state.sourceIndex = snapshot.source_index;
    state.taskId = FromStdString(snapshot.task_id);
    state.imageId = FromStdString(snapshot.image_id);
    state.workerId = FromStdString(snapshot.worker_id);
    state.status = FromStdString(snapshot.status);
    state.detail = FromStdString(snapshot.detail_message);
    state.sourcePath = FromStdString(snapshot.source_path);
    state.sourceName = FromStdString(snapshot.source_name);
    state.imageBytes = static_cast<qint64>(snapshot.image_bytes);
    state.processedBytes = static_cast<qint64>(snapshot.processed_bytes);
    state.imageFetchMs = snapshot.image_fetch_ms;
    state.httpFirstByteMs = snapshot.http_first_byte_ms;
    state.httpBodyMs = snapshot.http_body_ms;
    state.httpCopyMs = snapshot.http_chunk_callback_ms;
    state.httpTotalMs = snapshot.http_total_ms;
    state.algorithmMs = snapshot.algorithm_ms;
    state.masterEndToEndMs = snapshot.master_end_to_end_ms;
    state.finished = snapshot.finished;
    return state;
}

ProgressState ToProgressState(const MasterRunProgress& progress) {
    ProgressState state;
    state.sentCount = progress.sent_count;
    state.finishedCount = progress.finished_count;
    state.successCount = progress.success_count;
    state.failureCount = progress.failure_count;
    state.taskCount = progress.task_count;
    state.elapsedMs = progress.elapsed_ms;
    return state;
}

RunResultState ToRunResultState(const MasterRunResult& result) {
    RunResultState state;
    state.success = result.success;
    state.timedOut = result.timed_out;
    state.cancelled = result.cancelled;
    state.exitCode = result.exit_code;
    state.sentCount = result.sent_count;
    state.finishedCount = result.finished_count;
    state.successCount = result.success_count;
    state.failureCount = result.failure_count;
    state.imageStoreRemainingCount =
        static_cast<qint64>(result.image_store_remaining_count);
    return state;
}

class QtMasterObserver : public IMasterRunObserver {
public:
    explicit QtMasterObserver(MasterWorker* worker)
        : worker_(worker),
          has_result_(false),
          result_() {
    }

    void OnLog(
        const std::string& level,
        const std::string& message) override {
        emit worker_->LogMessage(FromStdString(level), FromStdString(message));
    }

    void OnTaskPublished(const MasterTaskSnapshot& task) override {
        emit worker_->TaskPublished(ToTaskUiState(task));
    }

    void OnTaskUpdated(const MasterTaskSnapshot& task) override {
        emit worker_->TaskUpdated(ToTaskUiState(task));
    }

    void OnProgress(const MasterRunProgress& progress) override {
        emit worker_->ProgressChanged(ToProgressState(progress));
    }

    void OnRunFinished(const MasterRunResult& result) override {
        result_ = ToRunResultState(result);
        has_result_ = true;
    }

    RunResultState TakeResult(int exit_code) const {
        RunResultState result = result_;
        if (!has_result_) {
            result.exitCode = exit_code;
            result.success = exit_code == 0;
        }
        return result;
    }

private:
    MasterWorker* worker_;
    bool has_result_;
    RunResultState result_;
};

QJsonObject ConnectionConfig(const QString& uri) {
    QJsonObject object;
    object.insert("uri", uri);
    object.insert("heartbeat_seconds", 10);
    object.insert("connect_timeout_ms", 5000);
    object.insert("socket_timeout_ms", 10);
    QJsonObject reconnect;
    reconnect.insert("enabled", true);
    reconnect.insert("initial_delay_ms", 200);
    reconnect.insert("max_delay_ms", 2000);
    object.insert("reconnect", reconnect);
    return object;
}

QJsonObject ExchangeConfig(
    const QString& name,
    const QString& type,
    bool durable,
    bool passive) {
    QJsonObject object;
    object.insert("name", name);
    object.insert("type", type);
    object.insert("durable", durable);
    object.insert("passive", passive);
    return object;
}

QJsonObject QueueConfig(
    const QString& name,
    bool durable,
    bool passive) {
    QJsonObject object;
    object.insert("name", name);
    object.insert("durable", durable);
    object.insert("passive", passive);
    return object;
}

QJsonObject QueueConfigEx(
    const QString& name,
    bool durable,
    bool auto_delete,
    bool exclusive,
    bool passive) {
    QJsonObject object;
    object.insert("name", name);
    object.insert("durable", durable);
    object.insert("auto_delete", auto_delete);
    object.insert("exclusive", exclusive);
    object.insert("passive", passive);
    return object;
}

QJsonObject BindingConfig(
    const QString& exchange,
    const QString& queue,
    const QString& routing_key) {
    QJsonObject object;
    object.insert("exchange", exchange);
    object.insert("queue", queue);
    object.insert("routing_key", routing_key);
    return object;
}

QJsonObject PublisherConfig(
    const QString& name,
    const QString& exchange,
    const QString& routing_key) {
    QJsonObject object;
    object.insert("name", name);
    object.insert("exchange", exchange);
    object.insert("routing_key", routing_key);
    object.insert("persistent", false);
    object.insert("content_type", "text/plain");
    return object;
}

QJsonObject ConsumerConfig(
    const QString& name,
    const QString& queue,
    int prefetch_count) {
    QJsonObject object;
    object.insert("name", name);
    object.insert("queue", queue);
    object.insert("prefetch_count", prefetch_count);
    object.insert("auto_ack", false);
    return object;
}

QString SanitizeName(const QString& value) {
    QString sanitized;
    for (int index = 0; index < value.size(); ++index) {
        const QChar ch = value.at(index);
        if (ch.isLetterOrNumber() ||
            ch == QLatin1Char('_') ||
            ch == QLatin1Char('-') ||
            ch == QLatin1Char('.')) {
            sanitized.append(ch);
        } else {
            sanitized.append('_');
        }
    }
    return sanitized.isEmpty() ? QString("local") : sanitized;
}

QString ResolveRuntimeFile(const QString& directory, const QStringList& names) {
    QDir dir(directory);
    for (int index = 0; index < names.size(); ++index) {
        const QString path = dir.absoluteFilePath(names.at(index));
        if (QFile::exists(path)) {
            return path;
        }
    }
    return QString();
}

void StopLocalWorkers(QList<QProcess*>* workers) {
    if (workers == NULL) {
        return;
    }
    for (int index = 0; index < workers->size(); ++index) {
        QProcess* process = workers->at(index);
        if (process == NULL) {
            continue;
        }
        if (process->state() != QProcess::NotRunning) {
            if (!process->waitForFinished(5000)) {
                process->terminate();
            }
            if (process->state() != QProcess::NotRunning &&
                !process->waitForFinished(3000)) {
                process->kill();
                process->waitForFinished(3000);
            }
        }
        delete process;
    }
    workers->clear();
}

}  // namespace

MasterWorker::MasterWorker(QObject* parent)
    : QObject(parent),
      cancel_requested_(false) {
}

void MasterWorker::Run(const DemoSettings& settings) {
    cancel_requested_.store(false);
    QList<QProcess*> local_workers;

    QString report_dir;
    QString module_config_path;
    QString run_id;
    QString error_message;
    if (!PrepareRuntime(
            settings,
            &report_dir,
            &module_config_path,
            &run_id,
            &error_message)) {
        emit LogMessage("error", error_message);
        RunResultState result;
        result.exitCode = 1;
        emit Finished(result);
        return;
    }

    emit LogMessage("info", QString("本轮运行目录：%1").arg(report_dir));

    RabbitMqAdminClient admin;
    connect(
        &admin,
        SIGNAL(LogMessage(QString, QString)),
        this,
        SIGNAL(LogMessage(QString, QString)));
    connect(
        &admin,
        SIGNAL(QueueStatusChanged(QueueStatus)),
        this,
        SIGNAL(QueueStatusChanged(QueueStatus)));
    admin.Configure(
        settings.managementApiUrl,
        settings.adminUser,
        settings.adminPassword);

    if (!admin.BootstrapTopology(
            settings.vhost,
            settings.masterUser,
            settings.masterPassword,
            settings.workerUser,
            settings.workerPassword,
            &error_message)) {
        emit LogMessage("error", error_message);
        RunResultState result;
        result.exitCode = 1;
        emit Finished(result);
        return;
    }

    if (!StartLocalWorkers(
            settings,
            report_dir,
            &local_workers,
            &error_message)) {
        emit LogMessage("error", error_message);
        StopLocalWorkers(&local_workers);
        RunResultState result;
        result.exitCode = 1;
        emit Finished(result);
        return;
    }

    if (!admin.WaitForTaskConsumers(
            settings.vhost,
            settings.expectedWorkerCount,
            300000,
            &cancel_requested_,
            &error_message)) {
        emit LogMessage("error", error_message);
        StopLocalWorkers(&local_workers);
        RunResultState result;
        result.exitCode = 1;
        emit Finished(result);
        return;
    }

    if (!WriteMasterModuleConfig(settings, module_config_path, &error_message)) {
        emit LogMessage("error", error_message);
        StopLocalWorkers(&local_workers);
        RunResultState result;
        result.exitCode = 1;
        emit Finished(result);
        return;
    }

    BmpFrameSource source(settings.imageDirectory);
    source.SetFrameSelectedCallback(
        [this](int source_index, const MasterSourceFrame& frame) {
            emit PreviewChanged(
                FromStdString(frame.source_path),
                FromStdString(frame.display_name),
                frame.data ? static_cast<qint64>(frame.data->size()) : 0,
                source_index);
        });

    QtMasterObserver observer(this);
    MasterRunConfig config =
        BuildRunConfig(settings, module_config_path, report_dir, run_id);
    emit LogMessage("info", "启动主机 E2E 内核");
    const int exit_code =
        RunTaskFlowMaster(config, &source, &observer, &cancel_requested_);
    if (exit_code != 0) {
        emit LogMessage("warn", QString("主机 E2E 结束，退出码：%1").arg(exit_code));
    }

    QueueStatus final_status;
    if (admin.GetQueues(settings.vhost, &final_status, NULL)) {
        emit QueueStatusChanged(final_status);
    }
    StopLocalWorkers(&local_workers);
    emit Finished(observer.TakeResult(exit_code));
}

void MasterWorker::RequestStop() {
    cancel_requested_.store(true);
    emit LogMessage("warn", "收到停止请求，正在让主机内核收尾");
}

bool MasterWorker::PrepareRuntime(
    const DemoSettings& settings,
    QString* report_dir,
    QString* module_config_path,
    QString* run_id,
    QString* error_message) {
    const QString id =
        "task-flow-gui-" +
        QDateTime::currentDateTime().toString("yyyyMMdd-hhmmsszzz");
    const QString root =
        settings.outputRoot.isEmpty()
            ? QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
                  "runtime/master")
            : settings.outputRoot;
    const QString dir = QDir(root).absoluteFilePath(id);
    QDir qdir;
    if (!qdir.mkpath(dir)) {
        if (error_message != NULL) {
            *error_message = QString("无法创建运行目录：%1").arg(dir);
        }
        return false;
    }

    if (run_id != NULL) {
        *run_id = id;
    }
    if (report_dir != NULL) {
        *report_dir = dir;
    }
    if (module_config_path != NULL) {
        *module_config_path = QDir(dir).absoluteFilePath("master_module_config.json");
    }
    return true;
}

bool MasterWorker::WriteMasterModuleConfig(
    const DemoSettings& settings,
    const QString& module_config_path,
    QString* error_message) const {
    QJsonObject bus;
    bus.insert("name", "task_flow_master_bus");
    bus.insert("type", "rabbitmq_bus");
    bus.insert("library_path", ResolvePluginPath(settings, "rabbitmq_bus.dll"));

    QJsonObject bus_config;
    bus_config.insert("connection", ConnectionConfig(BuildRabbitMqUri(settings)));

    QJsonObject worker_pool;
    worker_pool.insert("thread_count", settings.masterResultThreads);
    bus_config.insert("worker_pool", worker_pool);

    QJsonObject topology;
    QJsonArray exchanges;
    exchanges.append(ExchangeConfig("mc.task.exchange", "direct", true, true));
    exchanges.append(ExchangeConfig("mc.control.exchange", "fanout", true, true));
    topology.insert("exchanges", exchanges);
    QJsonArray queues;
    queues.append(QueueConfig("mc.result.queue", true, true));
    topology.insert("queues", queues);
    bus_config.insert("topology", topology);

    QJsonArray publishers;
    publishers.append(
        PublisherConfig("task_producer", "mc.task.exchange", "task.ready"));
    publishers.append(
        PublisherConfig("control_producer", "mc.control.exchange", ""));
    bus_config.insert("publishers", publishers);

    QJsonArray consumers;
    consumers.append(ConsumerConfig("result_consumer", "mc.result.queue", 0));
    bus_config.insert("consumers", consumers);
    bus.insert("config", bus_config);

    QJsonObject http;
    http.insert("name", "task_flow_master_http");
    http.insert("type", "http_transport");
    http.insert("library_path", ResolvePluginPath(settings, "http_transport.dll"));

    QJsonObject http_config;
    http_config.insert("role", "server");
    http_config.insert("listen_address", settings.listenAddress);
    http_config.insert("port", settings.httpPort);
    http_config.insert("server_thread_count", settings.httpServerThreadCount);
    http_config.insert("read_timeout_ms", 30000);
    http_config.insert("write_timeout_ms", 30000);
    http_config.insert("max_payload_bytes", 1073741824);
    http_config.insert("chunk_bytes", static_cast<double>(settings.httpChunkBytes));
    http_config.insert(
        "read_buffer_bytes",
        static_cast<double>(settings.httpReadBufferBytes));
    http_config.insert(
        "write_buffer_bytes",
        static_cast<double>(settings.httpWriteBufferBytes));
    http_config.insert(
        "socket_receive_buffer_bytes",
        static_cast<double>(settings.httpSocketReceiveBufferBytes));
    http_config.insert(
        "socket_send_buffer_bytes",
        static_cast<double>(settings.httpSocketSendBufferBytes));
    http.insert("config", http_config);

    QJsonObject root;
    root.insert("schema_version", 2);
    QJsonArray modules;
    modules.append(bus);
    modules.append(http);
    root.insert("modules", modules);

    QFile file(module_config_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error_message != NULL) {
            *error_message =
                QString("无法写入 module config：%1").arg(module_config_path);
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool MasterWorker::WriteLocalWorkerModuleConfig(
    const DemoSettings& settings,
    const QString& worker_id,
    const QString& worker_config_path,
    QString* error_message) const {
    const QString safe_worker_id = SanitizeName(worker_id);
    const QString control_queue =
        QString("mc.control.%1.%2")
            .arg(safe_worker_id)
            .arg(QUuid::createUuid()
                     .toString()
                     .remove(QLatin1Char('{'))
                     .remove(QLatin1Char('}')));

    QJsonObject bus;
    bus.insert("name", "task_flow_local_worker_bus_" + safe_worker_id);
    bus.insert("type", "rabbitmq_bus");
    bus.insert("library_path", ResolvePluginPath(settings, "rabbitmq_bus.dll"));

    QJsonObject bus_config;
    bus_config.insert("connection", ConnectionConfig(BuildWorkerRabbitMqUri(settings)));
    QJsonObject worker_pool;
    worker_pool.insert("thread_count", settings.localWorkerThreads);
    bus_config.insert("worker_pool", worker_pool);

    QJsonObject topology;
    QJsonArray exchanges;
    exchanges.append(ExchangeConfig("mc.result.exchange", "direct", true, false));
    exchanges.append(ExchangeConfig("mc.control.exchange", "fanout", true, false));
    topology.insert("exchanges", exchanges);

    QJsonArray queues;
    queues.append(QueueConfigEx("mc.task.queue", true, false, false, false));
    queues.append(QueueConfigEx(control_queue, false, true, false, false));
    topology.insert("queues", queues);

    QJsonArray bindings;
    bindings.append(BindingConfig("mc.control.exchange", control_queue, ""));
    topology.insert("bindings", bindings);
    bus_config.insert("topology", topology);

    QJsonArray publishers;
    publishers.append(
        PublisherConfig("result_producer", "mc.result.exchange", "result.ready"));
    bus_config.insert("publishers", publishers);

    QJsonArray consumers;
    consumers.append(ConsumerConfig("control_consumer", control_queue, 1));
    consumers.append(
        ConsumerConfig("task_consumer", "mc.task.queue", settings.localWorkerThreads));
    bus_config.insert("consumers", consumers);
    bus.insert("config", bus_config);

    QJsonObject http;
    http.insert("name", "task_flow_local_worker_http_" + safe_worker_id);
    http.insert("type", "http_transport");
    http.insert("library_path", ResolvePluginPath(settings, "http_transport.dll"));

    QJsonObject http_config;
    http_config.insert("role", "client");
    http_config.insert(
        "endpoint",
        QString("http://127.0.0.1:%1").arg(settings.httpPort));
    http_config.insert("read_timeout_ms", 30000);
    http_config.insert("write_timeout_ms", 30000);
    http_config.insert("max_payload_bytes", 1073741824);
    http_config.insert("chunk_bytes", static_cast<double>(settings.httpChunkBytes));
    http.insert("config", http_config);

    QJsonObject root;
    root.insert("schema_version", 2);
    QJsonArray modules;
    modules.append(bus);
    modules.append(http);
    root.insert("modules", modules);

    QFile file(worker_config_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error_message != NULL) {
            *error_message =
                QString("无法写入本机 worker module config：%1")
                    .arg(worker_config_path);
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool MasterWorker::StartLocalWorkers(
    const DemoSettings& settings,
    const QString& report_dir,
    QList<QProcess*>* workers,
    QString* error_message) {
    if (!settings.autoStartLocalWorkers) {
        emit LogMessage(
            "info",
            "RabbitMQ Host 不是 127.0.0.1，跳过桌面端本机 worker 自启动");
        return true;
    }
    if (workers == NULL) {
        if (error_message != NULL) {
            *error_message = "本机 worker 进程列表不可用";
        }
        return false;
    }

    const QString worker_exe = ResolveApplicationFile(
        QStringList() << "mc_task_flow_worker_host.exe"
                      << "mc_task_flow_worker_hostd.exe");
    if (worker_exe.isEmpty()) {
        if (error_message != NULL) {
            *error_message =
                "桌面端无法启动本机 worker：exe 同目录没有 "
                "mc_task_flow_worker_host.exe。请重新编译 Qt demo，"
                "或确认 CMake 已把 worker host 复制到桌面程序目录。";
        }
        return false;
    }

    const QString local_root = QDir(report_dir).absoluteFilePath("local_workers");
    QDir root_dir;
    if (!root_dir.mkpath(local_root)) {
        if (error_message != NULL) {
            *error_message = QString("无法创建本机 worker 运行目录：%1")
                                 .arg(local_root);
        }
        return false;
    }

    emit LogMessage(
        "info",
        QString("单机离线模式：自动启动 %1 个本机 worker，每个 worker %2 个处理线程")
            .arg(settings.expectedWorkerCount)
            .arg(settings.localWorkerThreads));

    for (int index = 0; index < settings.expectedWorkerCount; ++index) {
        const QString worker_id =
            QString("local-gui-%1").arg(index + 1, 2, 10, QLatin1Char('0'));
        const QString worker_dir = QDir(local_root).absoluteFilePath(worker_id);
        if (!root_dir.mkpath(worker_dir)) {
            if (error_message != NULL) {
                *error_message =
                    QString("无法创建本机 worker 目录：%1").arg(worker_dir);
            }
            return false;
        }

        const QString config_path =
            QDir(worker_dir).absoluteFilePath("worker_module_config.json");
        if (!WriteLocalWorkerModuleConfig(
                settings,
                worker_id,
                config_path,
                error_message)) {
            return false;
        }

        QStringList args;
        args << "--module-config" << config_path
             << "--worker-id" << worker_id
             << "--output-dir" << worker_dir
             << "--worker-task-threads" << QString::number(settings.localWorkerThreads)
             << "--algorithm-delay-ms" << "0"
             << "--timeout-ms" << QString::number(settings.timeoutMs)
             << "--http-endpoint" << QString("http://127.0.0.1:%1").arg(settings.httpPort)
             << "--http-route" << settings.httpRoute;

        QProcess* process = new QProcess();
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QString app_dir = QCoreApplication::applicationDirPath();
        const QString path_value = env.value("PATH");
        env.insert("PATH", app_dir + ";" + path_value);
        env.insert("TASK_FLOW_ALGORITHM_DELAY_MS", "0");
        process->setProcessEnvironment(env);
        process->setWorkingDirectory(app_dir);
        process->setStandardOutputFile(
            QDir(worker_dir).absoluteFilePath("worker.log"));
        process->setStandardErrorFile(
            QDir(worker_dir).absoluteFilePath("worker.err.log"));
        const QString worker_log_path =
            QDir(worker_dir).absoluteFilePath("worker.log");
        const QString worker_error_log_path =
            QDir(worker_dir).absoluteFilePath("worker.err.log");
        process->start(worker_exe, args);
        if (!process->waitForStarted(10000)) {
            const QString detail = process->errorString();
            delete process;
            if (error_message != NULL) {
                *error_message =
                    QString("启动本机 worker 失败：%1，%2")
                        .arg(worker_exe)
                        .arg(detail);
            }
            return false;
        }
        if (process->waitForFinished(500)) {
            const int exit_code = process->exitCode();
            delete process;
            if (error_message != NULL) {
                *error_message =
                    QString("本机 worker 启动后立即退出：%1，退出码：%2。"
                            "请查看日志：%3，错误日志：%4")
                        .arg(worker_id)
                        .arg(exit_code)
                        .arg(worker_log_path)
                        .arg(worker_error_log_path);
            }
            return false;
        }
        workers->append(process);
        emit LogMessage(
            "info",
            QString("本机 worker 已启动：%1，日志：%2")
                .arg(worker_id)
                .arg(worker_log_path));
    }

    return true;
}

QString MasterWorker::ResolvePluginPath(
    const DemoSettings& settings,
    const QString& file_name) const {
    const QString directory =
        settings.pluginDirectory.isEmpty()
            ? QCoreApplication::applicationDirPath()
            : settings.pluginDirectory;
    return QDir(directory).absoluteFilePath(file_name);
}

QString MasterWorker::ResolveApplicationFile(const QStringList& names) const {
    return ResolveRuntimeFile(QCoreApplication::applicationDirPath(), names);
}

QString MasterWorker::BuildRabbitMqUri(const DemoSettings& settings) const {
    return QString("amqp://%1:%2@%3:5672/%4")
        .arg(settings.masterUser)
        .arg(settings.masterPassword)
        .arg(settings.rabbitMqHost)
        .arg(settings.vhost);
}

QString MasterWorker::BuildWorkerRabbitMqUri(const DemoSettings& settings) const {
    return QString("amqp://%1:%2@%3:5672/%4")
        .arg(settings.workerUser)
        .arg(settings.workerPassword)
        .arg(settings.rabbitMqHost)
        .arg(settings.vhost);
}

MasterRunConfig MasterWorker::BuildRunConfig(
    const DemoSettings& settings,
    const QString& module_config_path,
    const QString& report_dir,
    const QString& run_id) const {
    MasterRunConfig config;
    config.module_config_path = ToStdString(module_config_path);
    config.run_id = ToStdString(run_id);
    config.report_dir = ToStdString(report_dir);
    config.profile_name = "qt-demo";
    config.http_route = ToStdString(settings.httpRoute);
    config.task_count = settings.taskCount;
    config.publish_rate = settings.publishRate;
    config.image_size_bytes = 20971520;
    config.image_store_capacity_bytes =
        static_cast<std::size_t>(settings.imageStoreCapacityBytes);
    config.image_store_ttl_ms = 600000;
    config.master_write_publish_threads = settings.masterResultThreads;
    config.master_result_threads = settings.masterResultThreads;
    config.worker_count = settings.expectedWorkerCount;
    config.timeout_ms = settings.timeoutMs;
    config.http_chunk_bytes = static_cast<std::size_t>(settings.httpChunkBytes);
    config.http_read_buffer_bytes =
        static_cast<std::size_t>(settings.httpReadBufferBytes);
    config.http_write_buffer_bytes =
        static_cast<std::size_t>(settings.httpWriteBufferBytes);
    config.http_socket_receive_buffer_bytes =
        static_cast<std::size_t>(settings.httpSocketReceiveBufferBytes);
    config.http_socket_send_buffer_bytes =
        static_cast<std::size_t>(settings.httpSocketSendBufferBytes);
    config.send_shutdown = settings.sendShutdown;
    config.source_buffer_mode = "reused_bmp_directory";
    config.source_prepare_ms = 0.0;
    return config;
}
