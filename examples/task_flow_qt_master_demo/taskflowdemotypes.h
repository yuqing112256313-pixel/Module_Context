#pragma once

#include <QtCore/QMetaType>
#include <QtCore/QString>

struct DemoSettings {
    QString imageDirectory;
    QString outputRoot;
    QString managementApiUrl;
    QString rabbitMqHost;
    QString adminUser;
    QString adminPassword;
    QString vhost;
    QString masterUser;
    QString masterPassword;
    QString workerUser;
    QString workerPassword;
    QString pluginDirectory;
    QString listenAddress;
    QString httpRoute;
    int httpPort;
    int httpServerThreadCount;
    int expectedWorkerCount;
    bool autoStartLocalWorkers;
    int localWorkerThreads;
    int taskCount;
    int publishRate;
    int masterResultThreads;
    int timeoutMs;
    qint64 imageStoreCapacityBytes;
    qint64 httpChunkBytes;
    qint64 httpReadBufferBytes;
    qint64 httpWriteBufferBytes;
    qint64 httpSocketReceiveBufferBytes;
    qint64 httpSocketSendBufferBytes;
    bool sendShutdown;
};

struct QueueStatus {
    QueueStatus()
        : taskReady(0),
          taskUnacked(0),
          taskConsumers(0),
          resultReady(0),
          resultUnacked(0),
          resultConsumers(0) {
    }

    int taskReady;
    int taskUnacked;
    int taskConsumers;
    int resultReady;
    int resultUnacked;
    int resultConsumers;
};

struct TaskUiState {
    TaskUiState()
        : sourceIndex(-1),
          imageBytes(0),
          processedBytes(0),
          imageFetchMs(0.0),
          httpFirstByteMs(0.0),
          httpBodyMs(0.0),
          httpCopyMs(0.0),
          httpTotalMs(0.0),
          algorithmMs(0.0),
          masterEndToEndMs(0.0),
          finished(false) {
    }

    int sourceIndex;
    QString taskId;
    QString imageId;
    QString workerId;
    QString status;
    QString detail;
    QString sourcePath;
    QString sourceName;
    qint64 imageBytes;
    qint64 processedBytes;
    double imageFetchMs;
    double httpFirstByteMs;
    double httpBodyMs;
    double httpCopyMs;
    double httpTotalMs;
    double algorithmMs;
    double masterEndToEndMs;
    bool finished;
};

struct ProgressState {
    ProgressState()
        : sentCount(0),
          finishedCount(0),
          successCount(0),
          failureCount(0),
          taskCount(0),
          elapsedMs(0.0) {
    }

    int sentCount;
    int finishedCount;
    int successCount;
    int failureCount;
    int taskCount;
    double elapsedMs;
};

struct RunResultState {
    RunResultState()
        : success(false),
          timedOut(false),
          cancelled(false),
          exitCode(1),
          sentCount(0),
          finishedCount(0),
          successCount(0),
          failureCount(0),
          imageStoreRemainingCount(0) {
    }

    bool success;
    bool timedOut;
    bool cancelled;
    int exitCode;
    int sentCount;
    int finishedCount;
    int successCount;
    int failureCount;
    qint64 imageStoreRemainingCount;
};

Q_DECLARE_METATYPE(QueueStatus)
Q_DECLARE_METATYPE(DemoSettings)
Q_DECLARE_METATYPE(TaskUiState)
Q_DECLARE_METATYPE(ProgressState)
Q_DECLARE_METATYPE(RunResultState)
