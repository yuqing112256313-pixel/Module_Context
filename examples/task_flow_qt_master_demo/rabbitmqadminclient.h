#pragma once

#include "taskflowdemotypes.h"

#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtNetwork/QNetworkAccessManager>

#include <atomic>

class RabbitMqAdminClient : public QObject {
    Q_OBJECT

public:
    explicit RabbitMqAdminClient(QObject* parent = NULL);

    void Configure(
        const QString& api_url,
        const QString& user,
        const QString& password);

    bool BootstrapTopology(
        const QString& vhost,
        const QString& master_user,
        const QString& master_password,
        const QString& worker_user,
        const QString& worker_password,
        QString* error_message);
    bool WaitForTaskConsumers(
        const QString& vhost,
        int expected_count,
        int timeout_ms,
        const std::atomic_bool* cancel_requested,
        QString* error_message);
    bool GetQueues(
        const QString& vhost,
        QueueStatus* status,
        QString* error_message);

signals:
    void LogMessage(const QString& level, const QString& message);
    void QueueStatusChanged(const QueueStatus& status);

private:
    struct Response {
        Response()
            : ok(false),
              status_code(0) {
        }

        bool ok;
        int status_code;
        QByteArray body;
        QString error;
    };

    Response Request(
        const QByteArray& method,
        const QString& path,
        const QJsonObject& body,
        bool has_body);
    bool PutJson(
        const QString& path,
        const QJsonObject& body,
        QString* error_message);
    bool PostJson(
        const QString& path,
        const QJsonObject& body,
        QString* error_message);
    bool DeleteNoBody(const QString& path, QString* error_message);
    bool GetJson(
        const QString& path,
        QJsonObject* object,
        QString* error_message);
    bool GetQueue(
        const QString& vhost,
        const QString& queue_name,
        int* ready,
        int* unacked,
        int* consumers,
        QString* error_message);

    QString PathSegment(const QString& value) const;
    QJsonObject ExchangeBody(const QString& type) const;
    QJsonObject QueueBody() const;
    QJsonObject BindingBody(const QString& routing_key) const;
    QJsonObject UserBody(const QString& password) const;
    QJsonObject PermissionBody(
        const QString& configure,
        const QString& write,
        const QString& read) const;

    QString api_url_;
    QByteArray authorization_header_;
    QNetworkAccessManager network_;
};
