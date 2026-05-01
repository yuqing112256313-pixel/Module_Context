#include "rabbitmqadminclient.h"

#include <QtCore/QEventLoop>
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonValue>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

RabbitMqAdminClient::RabbitMqAdminClient(QObject* parent)
    : QObject(parent),
      api_url_(),
      authorization_header_(),
      network_() {
}

void RabbitMqAdminClient::Configure(
    const QString& api_url,
    const QString& user,
    const QString& password) {
    api_url_ = api_url;
    while (api_url_.endsWith('/')) {
        api_url_.chop(1);
    }
    const QByteArray plain =
        QString("%1:%2").arg(user, password).toUtf8();
    authorization_header_ = "Basic " + plain.toBase64();
}

bool RabbitMqAdminClient::BootstrapTopology(
    const QString& vhost,
    const QString& master_user,
    const QString& master_password,
    const QString& worker_user,
    const QString& worker_password,
    QString* error_message) {
    const QString encoded_vhost = PathSegment(vhost);
    const QString task_exchange = PathSegment("mc.task.exchange");
    const QString result_exchange = PathSegment("mc.result.exchange");
    const QString control_exchange = PathSegment("mc.control.exchange");
    const QString task_queue = PathSegment("mc.task.queue");
    const QString result_queue = PathSegment("mc.result.queue");
    const QString encoded_master_user = PathSegment(master_user);
    const QString encoded_worker_user = PathSegment(worker_user);

    emit LogMessage("info", "准备 RabbitMQ vhost、用户、交换机和队列");
    if (!PutJson("/vhosts/" + encoded_vhost, QJsonObject(), error_message)) {
        return false;
    }
    if (!PutJson(
            "/users/" + encoded_master_user,
            UserBody(master_password),
            error_message)) {
        return false;
    }
    if (!PutJson(
            "/users/" + encoded_worker_user,
            UserBody(worker_password),
            error_message)) {
        return false;
    }
    if (!PutJson(
            "/exchanges/" + encoded_vhost + "/" + task_exchange,
            ExchangeBody("direct"),
            error_message)) {
        return false;
    }
    if (!PutJson(
            "/exchanges/" + encoded_vhost + "/" + result_exchange,
            ExchangeBody("direct"),
            error_message)) {
        return false;
    }
    if (!PutJson(
            "/exchanges/" + encoded_vhost + "/" + control_exchange,
            ExchangeBody("fanout"),
            error_message)) {
        return false;
    }
    if (!PutJson(
            "/queues/" + encoded_vhost + "/" + task_queue,
            QueueBody(),
            error_message)) {
        return false;
    }
    if (!PutJson(
            "/queues/" + encoded_vhost + "/" + result_queue,
            QueueBody(),
            error_message)) {
        return false;
    }
    if (!PostJson(
            "/bindings/" + encoded_vhost + "/e/" + task_exchange + "/q/" +
                task_queue,
            BindingBody("task.ready"),
            error_message)) {
        return false;
    }
    if (!PostJson(
            "/bindings/" + encoded_vhost + "/e/" + result_exchange + "/q/" +
                result_queue,
            BindingBody("result.ready"),
            error_message)) {
        return false;
    }

    (void)DeleteNoBody(
        "/queues/" + encoded_vhost + "/" + task_queue + "/contents",
        error_message);
    (void)DeleteNoBody(
        "/queues/" + encoded_vhost + "/" + result_queue + "/contents",
        error_message);

    if (!PutJson(
            "/permissions/" + encoded_vhost + "/" + encoded_master_user,
            PermissionBody(
                "^(mc\\.task\\.exchange|mc\\.task\\.queue|mc\\.result\\.exchange|mc\\.result\\.queue|mc\\.control\\.exchange)$",
                "^(mc\\.task\\.exchange|mc\\.control\\.exchange)$",
                "^(mc\\.result\\.queue)$"),
            error_message)) {
        return false;
    }
    if (!PutJson(
            "/permissions/" + encoded_vhost + "/" + encoded_worker_user,
            PermissionBody(
                "^(mc\\.task\\.queue|mc\\.result\\.exchange|mc\\.control\\.exchange|mc\\.control\\..*)$",
                "^(mc\\.result\\.exchange|mc\\.control\\.exchange|mc\\.control\\..*)$",
                "^(mc\\.task\\.queue|mc\\.control\\..*)$"),
            error_message)) {
        return false;
    }

    emit LogMessage("info", "RabbitMQ 调度拓扑已准备好");
    return true;
}

bool RabbitMqAdminClient::WaitForTaskConsumers(
    const QString& vhost,
    int expected_count,
    int timeout_ms,
    const std::atomic_bool* cancel_requested,
    QString* error_message) {
    QElapsedTimer timer;
    timer.start();
    QueueStatus status;
    while (timeout_ms <= 0 || timer.elapsed() < timeout_ms) {
        if (cancel_requested != NULL && cancel_requested->load()) {
            if (error_message != NULL) {
                *error_message = "等待 worker 时收到停止请求";
            }
            return false;
        }
        if (GetQueues(vhost, &status, error_message)) {
            emit QueueStatusChanged(status);
            if (status.taskConsumers >= expected_count) {
                emit LogMessage(
                    "info",
                    QString("任务消费者已就绪：%1/%2")
                        .arg(status.taskConsumers)
                        .arg(expected_count));
                return true;
            }
        }
        QThread::msleep(500);
    }

    if (error_message != NULL) {
        *error_message = QString("等待任务消费者超时：%1/%2")
                             .arg(status.taskConsumers)
                             .arg(expected_count);
    }
    return false;
}

bool RabbitMqAdminClient::GetQueues(
    const QString& vhost,
    QueueStatus* status,
    QString* error_message) {
    if (status == NULL) {
        return false;
    }

    QueueStatus next;
    if (!GetQueue(
            vhost,
            "mc.task.queue",
            &next.taskReady,
            &next.taskUnacked,
            &next.taskConsumers,
            error_message)) {
        return false;
    }
    if (!GetQueue(
            vhost,
            "mc.result.queue",
            &next.resultReady,
            &next.resultUnacked,
            &next.resultConsumers,
            error_message)) {
        return false;
    }

    *status = next;
    emit QueueStatusChanged(next);
    return true;
}

RabbitMqAdminClient::Response RabbitMqAdminClient::Request(
    const QByteArray& method,
    const QString& path,
    const QJsonObject& body,
    bool has_body) {
    Response response;
    if (api_url_.isEmpty()) {
        response.error = "RabbitMQ Management API URL is empty";
        return response;
    }

    QNetworkRequest request(QUrl(api_url_ + path));
    request.setRawHeader("Authorization", authorization_header_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = NULL;
    if (has_body) {
        const QByteArray json =
            QJsonDocument(body).toJson(QJsonDocument::Compact);
        if (method == "PUT") {
            reply = network_.put(request, json);
        } else if (method == "POST") {
            reply = network_.post(request, json);
        } else {
            response.error = "HTTP method with body is not supported";
            return response;
        }
    } else {
        if (method == "GET") {
            reply = network_.get(request);
        } else {
            reply = network_.sendCustomRequest(request, method);
        }
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    QObject::connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
    timer.start(10000);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        response.error = "RabbitMQ Management API request timed out";
        reply->deleteLater();
        return response;
    }

    response.status_code =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        response.error = reply->errorString();
        if (!response.body.isEmpty()) {
            response.error += ": " + QString::fromUtf8(response.body);
        }
    }
    response.ok = response.status_code >= 200 && response.status_code < 300;
    if (!response.ok && response.error.isEmpty()) {
        response.error =
            QString("HTTP %1: %2")
                .arg(response.status_code)
                .arg(QString::fromUtf8(response.body));
    }
    reply->deleteLater();
    return response;
}

bool RabbitMqAdminClient::PutJson(
    const QString& path,
    const QJsonObject& body,
    QString* error_message) {
    const Response response = Request("PUT", path, body, true);
    if (response.ok) {
        return true;
    }
    if (error_message != NULL) {
        *error_message = response.error;
    }
    return false;
}

bool RabbitMqAdminClient::PostJson(
    const QString& path,
    const QJsonObject& body,
    QString* error_message) {
    const Response response = Request("POST", path, body, true);
    if (response.ok) {
        return true;
    }
    if (error_message != NULL) {
        *error_message = response.error;
    }
    return false;
}

bool RabbitMqAdminClient::DeleteNoBody(
    const QString& path,
    QString* error_message) {
    const Response response = Request("DELETE", path, QJsonObject(), false);
    if (response.ok || response.status_code == 404) {
        return true;
    }
    if (error_message != NULL) {
        *error_message = response.error;
    }
    return false;
}

bool RabbitMqAdminClient::GetJson(
    const QString& path,
    QJsonObject* object,
    QString* error_message) {
    const Response response = Request("GET", path, QJsonObject(), false);
    if (!response.ok) {
        if (error_message != NULL) {
            *error_message = response.error;
        }
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument document =
        QJsonDocument::fromJson(response.body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError ||
        !document.isObject()) {
        if (error_message != NULL) {
            *error_message = "RabbitMQ API returned invalid JSON";
        }
        return false;
    }
    if (object != NULL) {
        *object = document.object();
    }
    return true;
}

bool RabbitMqAdminClient::GetQueue(
    const QString& vhost,
    const QString& queue_name,
    int* ready,
    int* unacked,
    int* consumers,
    QString* error_message) {
    QJsonObject object;
    if (!GetJson(
            "/queues/" + PathSegment(vhost) + "/" + PathSegment(queue_name),
            &object,
            error_message)) {
        return false;
    }
    if (ready != NULL) {
        *ready = object.value("messages_ready").toInt();
    }
    if (unacked != NULL) {
        *unacked = object.value("messages_unacknowledged").toInt();
    }
    if (consumers != NULL) {
        *consumers = object.value("consumers").toInt();
    }
    return true;
}

QString RabbitMqAdminClient::PathSegment(const QString& value) const {
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

QJsonObject RabbitMqAdminClient::ExchangeBody(const QString& type) const {
    QJsonObject body;
    body.insert("type", type);
    body.insert("durable", true);
    body.insert("auto_delete", false);
    body.insert("internal", false);
    body.insert("arguments", QJsonObject());
    return body;
}

QJsonObject RabbitMqAdminClient::QueueBody() const {
    QJsonObject body;
    body.insert("durable", true);
    body.insert("auto_delete", false);
    body.insert("arguments", QJsonObject());
    return body;
}

QJsonObject RabbitMqAdminClient::BindingBody(const QString& routing_key) const {
    QJsonObject body;
    body.insert("routing_key", routing_key);
    body.insert("arguments", QJsonObject());
    return body;
}

QJsonObject RabbitMqAdminClient::UserBody(const QString& password) const {
    QJsonObject body;
    body.insert("password", password);
    body.insert("tags", QString());
    return body;
}

QJsonObject RabbitMqAdminClient::PermissionBody(
    const QString& configure,
    const QString& write,
    const QString& read) const {
    QJsonObject body;
    body.insert("configure", configure);
    body.insert("write", write);
    body.insert("read", read);
    return body;
}
