#include "mainwindow.h"

#include "masterworker.h"
#include "ui_mainwindow.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFileInfo>
#include <QtCore/QCoreApplication>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkInterface>
#include <QtNetwork/QHostInfo>
#include <QtNetwork/QAbstractSocket>
#include <QtGui/QPixmap>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStyle>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QVBoxLayout>

namespace {

QString FormatBytes(qint64 bytes) {
    const double mb = static_cast<double>(bytes) / 1024.0 / 1024.0;
    return QString("%1 MiB").arg(mb, 0, 'f', 2);
}

QString FormatMs(double value) {
    return QString("%1 ms").arg(value, 0, 'f', 1);
}

QString DefaultOutputRoot() {
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        "runtime/master");
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      ui_(new Ui::MainWindow),
      settings_("ModuleContext", "TaskFlowQtMasterDemo"),
      preview_scene_(this),
      result_scene_(this),
      clock_timer_(this),
      worker_thread_(NULL),
      worker_(NULL),
      task_rows_(),
      image_directory_(),
      current_preview_path_() {
    ui_->setupUi(this);
    setWindowTitle("Task Flow E2E 主机演示台");

    SetupDynamicControls();
    SetupTables();
    LoadSettings();

    ui_->graphicsView->setScene(&preview_scene_);
    ui_->graphicsView_2->setScene(&result_scene_);
    result_scene_.addText("算法结果待接入");

    connect(
        ui_->SelectImageDirButton,
        SIGNAL(clicked()),
        this,
        SLOT(SelectImageDirectory()));
    connect(
        ui_->SelectSharedDirButton,
        SIGNAL(clicked()),
        this,
        SLOT(SelectOutputDirectory()));
    connect(
        ui_->SelectPluginDirButton,
        SIGNAL(clicked()),
        this,
        SLOT(SelectPluginDirectory()));
    connect(
        ui_->StartCaptureButton,
        SIGNAL(clicked()),
        this,
        SLOT(StartRun()));
    connect(ui_->StopButton, SIGNAL(clicked()), this, SLOT(StopRun()));
    connect(
        ui_->ClearSharedDirButton,
        SIGNAL(clicked()),
        this,
        SLOT(ClearRuntime()));
    connect(ui_->ClearLogButton, SIGNAL(clicked()), this, SLOT(ClearLog()));
    connect(
        &clock_timer_,
        SIGNAL(timeout()),
        this,
        SLOT(UpdateClock()));

    clock_timer_.start(1000);
    UpdateClock();
    SetRunning(false);
    SetRunState("stopped", "待启动");
    SetRabbitState(false, "未连接");
    SetRuntimeState(true, "就绪");
    ui_->HostLabel->setText(QString("主机：%1").arg(QHostInfo::localHostName()));
    ui_->IpLabel->setText(QString("IP：%1").arg(HostIpText()));
    ui_->VersionLabel->setText("Task Flow Qt Demo / Qt 5.9.7");
}

MainWindow::~MainWindow() {
    SaveSettings();
    if (worker_ != NULL) {
        worker_->RequestStop();
    }
    if (worker_thread_ != NULL) {
        worker_thread_->quit();
        worker_thread_->wait(3000);
    }
    delete ui_;
}

void MainWindow::SetupDynamicControls() {
    ui_->ExpectedWorkerCountLabel->setText("从机数量");
    ui_->PublishRateLabel->setText("发布速率");
    ui_->SharedImageRootLabel->setText("运行输出目录");
    ui_->StartCaptureButton->setText("启动 E2E");
    ui_->ClearSharedDirButton->setText("清理运行目录");
    ui_->TaskFlowTitleLabel->setText("采图与调度链路");
    ui_->RabbitMqTitleMainLabel->setText("RabbitMQ 调度状态");
    ui_->ResultTitleMainLabel->setText("算法结果占位");
}

void MainWindow::SetupTables() {
    ui_->TaskStateTable->setColumnCount(8);
    ui_->TaskStateTable->setHorizontalHeaderLabels(
        QStringList() << "序号"
                      << "图片"
                      << "任务"
                      << "状态"
                      << "从机"
                      << "拉图"
                      << "收正文"
                      << "全链路");
    ui_->TaskStateTable->horizontalHeader()->setStretchLastSection(true);
    ui_->TaskStateTable->verticalHeader()->setVisible(false);
    ui_->TaskStateTable->setAlternatingRowColors(true);
    ui_->TaskStateTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    ui_->TraceLogTable->setColumnCount(3);
    ui_->TraceLogTable->setHorizontalHeaderLabels(
        QStringList() << "时间"
                      << "级别"
                      << "日志");
    ui_->TraceLogTable->horizontalHeader()->setStretchLastSection(true);
    ui_->TraceLogTable->verticalHeader()->setVisible(false);
    ui_->TraceLogTable->setAlternatingRowColors(true);
}

void MainWindow::LoadSettings() {
    image_directory_ = settings_.value("imageDirectory", "").toString();
    ui_->LocalImageDirValueLabel->setText(
        image_directory_.isEmpty() ? "未选择" : image_directory_);
    ui_->ExpectedWorkerCountEdit->setText(
        settings_.value("expectedWorkerCount", 5).toString());
    ui_->PublishRateEdit->setText(settings_.value("publishRate", 50).toString());
    ui_->SharedImageRootEdit->setText(
        settings_.value("outputRoot", DefaultOutputRoot()).toString());
    ui_->TaskCountEdit->setText(settings_.value("taskCount", 100).toString());
    ui_->RabbitMqApiEdit->setText(
        settings_.value("managementApiUrl", "http://127.0.0.1:15672/api")
            .toString());
    ui_->RabbitMqHostEdit->setText(
        settings_.value("rabbitMqHost", "127.0.0.1").toString());
    ui_->HttpPortEdit->setText(settings_.value("httpPort", 50080).toString());
    ui_->PluginDirectoryEdit->setText(
        settings_.value("pluginDirectory", QCoreApplication::applicationDirPath())
            .toString());
}

void MainWindow::SaveSettings() {
    settings_.setValue("imageDirectory", image_directory_);
    settings_.setValue("expectedWorkerCount", ui_->ExpectedWorkerCountEdit->text());
    settings_.setValue("publishRate", ui_->PublishRateEdit->text());
    settings_.setValue("outputRoot", ui_->SharedImageRootEdit->text());
    settings_.setValue("taskCount", ui_->TaskCountEdit->text());
    settings_.setValue("managementApiUrl", ui_->RabbitMqApiEdit->text());
    settings_.setValue("rabbitMqHost", ui_->RabbitMqHostEdit->text());
    settings_.setValue("httpPort", ui_->HttpPortEdit->text());
    settings_.setValue("pluginDirectory", ui_->PluginDirectoryEdit->text());
}

DemoSettings MainWindow::ReadSettings() const {
    DemoSettings settings;
    settings.imageDirectory = image_directory_;
    settings.outputRoot = ui_->SharedImageRootEdit->text().trimmed();
    settings.managementApiUrl = ui_->RabbitMqApiEdit->text().trimmed();
    settings.rabbitMqHost = ui_->RabbitMqHostEdit->text().trimmed();
    settings.adminUser = "guest";
    settings.adminPassword = "guest";
    settings.vhost = "mc_integration";
    settings.masterUser = "mc_master";
    settings.masterPassword = "master_secret";
    settings.workerUser = "mc_worker";
    settings.workerPassword = "worker_secret";
    settings.pluginDirectory = ui_->PluginDirectoryEdit->text().trimmed();
    settings.listenAddress = "0.0.0.0";
    settings.httpRoute = "/task-flow/images";
    settings.httpPort = ui_->HttpPortEdit->text().toInt();
    settings.httpServerThreadCount = 64;
    settings.expectedWorkerCount = ui_->ExpectedWorkerCountEdit->text().toInt();
    settings.autoStartLocalWorkers =
        settings.rabbitMqHost == "127.0.0.1" ||
        settings.rabbitMqHost.compare("localhost", Qt::CaseInsensitive) == 0;
    settings.localWorkerThreads = 8;
    settings.taskCount = ui_->TaskCountEdit->text().toInt();
    settings.publishRate = ui_->PublishRateEdit->text().toInt();
    settings.masterResultThreads = 64;
    settings.timeoutMs = 0;
    settings.imageStoreCapacityBytes = 4294967296LL;
    settings.httpChunkBytes = 8388608;
    settings.httpReadBufferBytes = 262144;
    settings.httpWriteBufferBytes = 262144;
    settings.httpSocketReceiveBufferBytes = 0;
    settings.httpSocketSendBufferBytes = 0;
    settings.sendShutdown = true;
    return settings;
}

void MainWindow::SelectImageDirectory() {
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        "选择 BMP 图像目录",
        image_directory_.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
            : image_directory_);
    if (dir.isEmpty()) {
        return;
    }
    image_directory_ = dir;
    ui_->LocalImageDirValueLabel->setText(dir);
    SaveSettings();
}

void MainWindow::SelectOutputDirectory() {
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        "选择运行输出目录",
        ui_->SharedImageRootEdit->text());
    if (!dir.isEmpty()) {
        ui_->SharedImageRootEdit->setText(dir);
    }
}

void MainWindow::SelectPluginDirectory() {
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        "选择插件 DLL 目录",
        ui_->PluginDirectoryEdit->text());
    if (!dir.isEmpty()) {
        ui_->PluginDirectoryEdit->setText(dir);
    }
}

void MainWindow::StartRun() {
    SaveSettings();
    const DemoSettings settings = ReadSettings();
    if (settings.imageDirectory.isEmpty()) {
        QMessageBox::warning(this, "缺少图像", "请先选择包含 BMP 的图像目录。");
        return;
    }
    QDir image_dir(settings.imageDirectory);
    if (image_dir.entryList(QStringList() << "*.bmp" << "*.BMP", QDir::Files)
            .isEmpty()) {
        QMessageBox::warning(this, "缺少图像", "目录中没有 BMP 文件。");
        return;
    }
    if (settings.expectedWorkerCount <= 0 || settings.taskCount <= 0 ||
        settings.publishRate <= 0 || settings.httpPort <= 0) {
        QMessageBox::warning(this, "参数错误", "从机数量、任务数量、速率和端口必须大于 0。");
        return;
    }

    task_rows_.clear();
    ui_->TaskStateTable->setRowCount(0);
    SetRunning(true);
    SetRunState("running", "运行中");
    SetRabbitState(false, "连接中");
    SetRuntimeState(true, "运行中");
    AddLogRow("info", "开始准备桌面演示版 E2E");

    worker_thread_ = new QThread(this);
    worker_ = new MasterWorker();
    worker_->moveToThread(worker_thread_);
    connect(
        worker_thread_,
        &QThread::started,
        worker_,
        [this, settings]() { worker_->Run(settings); });
    connect(
        worker_,
        SIGNAL(LogMessage(QString, QString)),
        this,
        SLOT(OnLogMessage(QString, QString)));
    connect(
        worker_,
        SIGNAL(QueueStatusChanged(QueueStatus)),
        this,
        SLOT(OnQueueStatus(QueueStatus)));
    connect(
        worker_,
        SIGNAL(TaskPublished(TaskUiState)),
        this,
        SLOT(OnTaskPublished(TaskUiState)));
    connect(
        worker_,
        SIGNAL(TaskUpdated(TaskUiState)),
        this,
        SLOT(OnTaskUpdated(TaskUiState)));
    connect(
        worker_,
        SIGNAL(ProgressChanged(ProgressState)),
        this,
        SLOT(OnProgress(ProgressState)));
    connect(
        worker_,
        SIGNAL(PreviewChanged(QString, QString, qint64, int)),
        this,
        SLOT(OnPreviewChanged(QString, QString, qint64, int)));
    connect(
        worker_,
        SIGNAL(Finished(RunResultState)),
        this,
        SLOT(OnRunFinished(RunResultState)));
    connect(worker_, SIGNAL(Finished(RunResultState)), worker_thread_, SLOT(quit()));
    connect(worker_thread_, SIGNAL(finished()), worker_, SLOT(deleteLater()));
    connect(worker_thread_, SIGNAL(finished()), worker_thread_, SLOT(deleteLater()));
    worker_thread_->start();
}

void MainWindow::StopRun() {
    if (worker_ != NULL) {
        QMetaObject::invokeMethod(worker_, "RequestStop", Qt::QueuedConnection);
        AddLogRow("warn", "已请求停止，本轮会走主机清理和控制广播");
    }
}

void MainWindow::ClearRuntime() {
    if (worker_thread_ != NULL) {
        QMessageBox::information(this, "正在运行", "请先停止当前运行。");
        return;
    }
    const QString root = ui_->SharedImageRootEdit->text().trimmed();
    if (root.isEmpty()) {
        return;
    }
    QDir dir(root);
    if (!dir.exists()) {
        AddLogRow("info", "运行目录不存在，无需清理");
        return;
    }
    const qint64 bytes = DirectorySize(root);
    if (QMessageBox::question(
            this,
            "确认清理",
            QString("将清理运行输出目录：\n%1\n约 %2")
                .arg(root)
                .arg(FormatBytes(bytes))) != QMessageBox::Yes) {
        return;
    }
    if (dir.removeRecursively()) {
        AddLogRow("info", "运行输出目录已清理");
    } else {
        AddLogRow("error", "运行输出目录清理失败，可能有文件仍被占用");
    }
}

void MainWindow::ClearLog() {
    ui_->TraceLogTable->setRowCount(0);
}

void MainWindow::OnLogMessage(
    const QString& level,
    const QString& message) {
    AddLogRow(level, message);
    if (level == "error") {
        SetRunState("error", "异常");
    }
}

void MainWindow::OnQueueStatus(const QueueStatus& status) {
    ui_->TaskReadyValueLabel->setText(QString::number(status.taskReady));
    ui_->TaskUnackedValueLabel->setText(QString::number(status.taskUnacked));
    ui_->TaskConsumersValueLabel->setText(QString::number(status.taskConsumers));
    ui_->ResultReadyValueLabel->setText(
        QString("待消费 %1").arg(status.resultReady));
    ui_->ResultUnackedValueLabel->setText(
        QString("处理中 %1").arg(status.resultUnacked));
    ui_->ResultConsumersValueLabel->setText(
        QString("消费者 %1").arg(status.resultConsumers));
    ui_->TelemetryReadyValueLabel->setText("广播停止");
    ui_->TelemetryUnackedValueLabel->setText("不承载任务");
    ui_->TelemetryConsumersValueLabel->setText("fanout");
    SetRabbitState(true, "已连接");
}

void MainWindow::OnTaskPublished(const TaskUiState& task) {
    const int row = EnsureTaskRow(task);
    UpdateTaskRow(row, task);
}

void MainWindow::OnTaskUpdated(const TaskUiState& task) {
    const int row = EnsureTaskRow(task);
    UpdateTaskRow(row, task);
    ui_->WorkerIdValueLabel->setText(task.workerId.isEmpty() ? "-" : task.workerId);
    ui_->AlgorithmMsValueLabel->setText(FormatMs(task.algorithmMs));
    ui_->InspectionResultValueLabel->setText("待接入");
    ui_->DefectCountValueLabel->setText("-");
    ui_->ResultJsonPathValueLabel->setText("-");
}

void MainWindow::OnProgress(const ProgressState& progress) {
    const double seconds = progress.elapsedMs / 1000.0;
    const double throughput =
        seconds > 0.001 ? static_cast<double>(progress.finishedCount) / seconds
                         : 0.0;
    ui_->RunIdValueLabel->setText(
        QString("%1/%2 完成，%3 张/秒")
            .arg(progress.finishedCount)
            .arg(progress.taskCount)
            .arg(throughput, 0, 'f', 2));
}

void MainWindow::OnPreviewChanged(
    const QString& image_path,
    const QString& image_name,
    qint64 image_bytes,
    int source_index) {
    current_preview_path_ = image_path;
    ui_->TraceIdValueLabel->setText(QString("source-%1").arg(source_index + 1));
    ui_->ImagePathValueLabel->setText(image_path);
    ui_->ImageFormatValueLabel->setText(image_name);
    ui_->ImageBytesValueLabel->setText(FormatBytes(image_bytes));
    UpdatePreview(image_path);
}

void MainWindow::OnRunFinished(const RunResultState& result) {
    AddLogRow(
        result.success ? "info" : "warn",
        QString("本轮结束：完成 %1/%2，成功 %3，失败 %4，内存仓库剩余 %5")
            .arg(result.finishedCount)
            .arg(result.sentCount)
            .arg(result.successCount)
            .arg(result.failureCount)
            .arg(result.imageStoreRemainingCount));
    SetRunning(false);
    SetRunState(result.success ? "stopped" : "error", result.success ? "完成" : "结束");
    worker_ = NULL;
    worker_thread_ = NULL;
}

void MainWindow::UpdateClock() {
    ui_->SystemTimeLabel->setText(
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
}

void MainWindow::SetRunning(bool running) {
    ui_->StartCaptureButton->setEnabled(!running);
    ui_->StopButton->setEnabled(running);
    ui_->SelectImageDirButton->setEnabled(!running);
    ui_->SelectSharedDirButton->setEnabled(!running);
    ui_->ClearSharedDirButton->setEnabled(!running);
}

void MainWindow::SetRunState(const QString& state, const QString& text) {
    ui_->RunStateLabel->setProperty("runState", state);
    ui_->RunStateLabel->setText(text);
    ui_->RunStateLabel->style()->unpolish(ui_->RunStateLabel);
    ui_->RunStateLabel->style()->polish(ui_->RunStateLabel);
}

void MainWindow::SetRabbitState(bool connected, const QString& text) {
    ui_->RabbitMqDotLabel->setProperty(
        "connState",
        connected ? "connected" : "disconnected");
    ui_->RabbitMqStateLabel->setText(text);
    ui_->RabbitMqDotLabel->style()->unpolish(ui_->RabbitMqDotLabel);
    ui_->RabbitMqDotLabel->style()->polish(ui_->RabbitMqDotLabel);
}

void MainWindow::SetRuntimeState(bool normal, const QString& text) {
    ui_->SharedDirDotLabel->setProperty("dirState", normal ? "normal" : "error");
    ui_->SharedDirStateLabel->setText(text);
    ui_->SharedDirDotLabel->style()->unpolish(ui_->SharedDirDotLabel);
    ui_->SharedDirDotLabel->style()->polish(ui_->SharedDirDotLabel);
}

void MainWindow::AddLogRow(const QString& level, const QString& message) {
    const int row = ui_->TraceLogTable->rowCount();
    ui_->TraceLogTable->insertRow(row);
    ui_->TraceLogTable->setItem(
        row,
        0,
        new QTableWidgetItem(QDateTime::currentDateTime().toString("HH:mm:ss.zzz")));
    ui_->TraceLogTable->setItem(row, 1, new QTableWidgetItem(level));
    ui_->TraceLogTable->setItem(row, 2, new QTableWidgetItem(message));
    ui_->TraceLogTable->scrollToBottom();
}

int MainWindow::EnsureTaskRow(const TaskUiState& task) {
    const QString key = task.taskId.isEmpty()
                            ? QString::number(task.sourceIndex)
                            : task.taskId;
    if (task_rows_.contains(key)) {
        return task_rows_.value(key);
    }
    const int row = ui_->TaskStateTable->rowCount();
    ui_->TaskStateTable->insertRow(row);
    task_rows_.insert(key, row);
    return row;
}

void MainWindow::UpdateTaskRow(int row, const TaskUiState& task) {
    ui_->TaskStateTable->setItem(
        row,
        0,
        new QTableWidgetItem(QString::number(task.sourceIndex + 1)));
    ui_->TaskStateTable->setItem(
        row,
        1,
        new QTableWidgetItem(task.sourceName.isEmpty() ? "-" : task.sourceName));
    ui_->TaskStateTable->setItem(row, 2, new QTableWidgetItem(task.taskId));
    ui_->TaskStateTable->setItem(row, 3, new QTableWidgetItem(task.status));
    ui_->TaskStateTable->setItem(
        row,
        4,
        new QTableWidgetItem(task.workerId.isEmpty() ? "-" : task.workerId));
    ui_->TaskStateTable->setItem(row, 5, new QTableWidgetItem(FormatMs(task.imageFetchMs)));
    ui_->TaskStateTable->setItem(row, 6, new QTableWidgetItem(FormatMs(task.httpBodyMs)));
    ui_->TaskStateTable->setItem(
        row,
        7,
        new QTableWidgetItem(FormatMs(task.masterEndToEndMs)));
}

void MainWindow::UpdatePreview(const QString& image_path) {
    QPixmap pixmap(image_path);
    preview_scene_.clear();
    if (pixmap.isNull()) {
        preview_scene_.addText("BMP 预览加载失败");
        return;
    }
    const QSize viewport = ui_->graphicsView->viewport()->size();
    const QPixmap scaled = pixmap.scaled(
        viewport,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    preview_scene_.addPixmap(scaled);
    preview_scene_.setSceneRect(scaled.rect());
}

QString MainWindow::HostIpText() const {
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (int index = 0; index < addresses.size(); ++index) {
        const QHostAddress address = addresses.at(index);
        if (address.protocol() == QAbstractSocket::IPv4Protocol &&
            !address.isLoopback()) {
            return address.toString();
        }
    }
    return "127.0.0.1";
}

qint64 MainWindow::DirectorySize(const QString& path) const {
    qint64 total = 0;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}
