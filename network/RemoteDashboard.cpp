#include "RemoteDashboard.h"

#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>

namespace {

constexpr int MAX_REQUEST_BYTES = 16 * 1024;
constexpr int MAX_HISTORY_POINTS = 3600;
constexpr int SNAPSHOT_HISTORY_POINTS = 600;

} // namespace

RemoteDashboard::RemoteDashboard(QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this)) {
    connect(m_server, &QTcpServer::newConnection,
            this, &RemoteDashboard::acceptPendingConnections);
}

bool RemoteDashboard::start(quint16 port, QString *errorMessage) {
    if (m_server->isListening()) {
        if (m_server->serverPort() == port) return true;
        const QString message = QStringLiteral("远程看板已在端口 %1 监听")
                                    .arg(m_server->serverPort());
        if (errorMessage) *errorMessage = message;
        emit errorOccurred(message);
        return false;
    }

    if (!m_server->listen(QHostAddress::AnyIPv4, port)) {
        const QString message = m_server->errorString();
        if (errorMessage) *errorMessage = message;
        emit errorOccurred(message);
        return false;
    }
    emit listeningChanged(true);
    return true;
}

void RemoteDashboard::stop() {
    const bool wasListening = m_server->isListening();

    const QList<QTcpSocket *> sockets = m_requestBuffers.keys();
    m_requestBuffers.clear();
    for (QTcpSocket *socket : sockets) {
        if (!socket) continue;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }

    if (wasListening) m_server->close();
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket) continue;
        socket->abort();
        socket->deleteLater();
    }
    if (wasListening) emit listeningChanged(false);
}

void RemoteDashboard::setAcquiring(bool acquiring) {
    if (m_acquiring == acquiring) return;
    m_acquiring = acquiring;
    ++m_stateSequence;
}

void RemoteDashboard::resetMeasurements() {
    m_history.clear();
    m_latestValid = false;
    m_latestConcentration = 0.0;
    m_latestAcquisitionTime = 0.0;
    m_latestCapturedAt = QDateTime();
    ++m_stateSequence;
}

void RemoteDashboard::publishParticleConcentration(double acquisitionTimeSeconds,
                                                    double concentration,
                                                    bool valid) {
    ++m_sampleSequence;
    ++m_stateSequence;
    m_latestAcquisitionTime = acquisitionTimeSeconds;
    m_latestCapturedAt = QDateTime::currentDateTime();
    m_latestValid = valid && std::isfinite(concentration);

    if (!m_latestValid) return;

    m_latestConcentration = concentration;
    HistoryPoint point;
    point.acquisitionTimeSeconds = acquisitionTimeSeconds;
    point.concentration = concentration;
    point.capturedAt = m_latestCapturedAt;
    point.sequence = m_sampleSequence;
    m_history.append(point);
    if (m_history.size() > MAX_HISTORY_POINTS) {
        m_history.remove(0, m_history.size() - MAX_HISTORY_POINTS);
    }
}

bool RemoteDashboard::isListening() const {
    return m_server->isListening();
}

quint16 RemoteDashboard::serverPort() const {
    return m_server->serverPort();
}

void RemoteDashboard::acceptPendingConnections() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket) continue;

        m_requestBuffers.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            readRequest(socket);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            forgetSocket(socket);
            socket->deleteLater();
        });
        QTimer::singleShot(5000, socket, [socket]() {
            if (socket->state() != QAbstractSocket::UnconnectedState) {
                socket->disconnectFromHost();
            }
        });
    }
}

void RemoteDashboard::readRequest(QTcpSocket *socket) {
    if (!m_requestBuffers.contains(socket)) return;

    QByteArray &request = m_requestBuffers[socket];
    request.append(socket->readAll());
    if (request.size() > MAX_REQUEST_BYTES) {
        sendResponse(socket, 413, "Payload Too Large", "text/plain; charset=utf-8",
                     QByteArrayLiteral("Request is too large.\n"));
        return;
    }
    if (!request.contains("\r\n\r\n")) return;

    const int firstLineEnd = request.indexOf("\r\n");
    const QList<QByteArray> requestLine = request.left(firstLineEnd).split(' ');
    if (requestLine.size() != 3 || requestLine.at(0) != "GET") {
        sendResponse(socket, 405, "Method Not Allowed", "text/plain; charset=utf-8",
                     QByteArrayLiteral("Only GET is supported.\n"));
        return;
    }

    const QUrl requestUrl = QUrl::fromEncoded(requestLine.at(1));
    const QString path = requestUrl.path();
    if (path == QStringLiteral("/") || path == QStringLiteral("/index.html")) {
        sendResponse(socket, 200, "OK", "text/html; charset=utf-8", dashboardHtml());
    } else if (path == QStringLiteral("/api/snapshot")) {
        sendResponse(socket, 200, "OK", "application/json; charset=utf-8", snapshotJson());
    } else if (path == QStringLiteral("/api/history.csv")) {
        const QUrlQuery query(requestUrl);
        bool fromOk = false;
        bool toOk = false;
        const quint64 fromSequence =
            query.queryItemValue(QStringLiteral("from_sequence")).toULongLong(&fromOk);
        const quint64 toSequence =
            query.queryItemValue(QStringLiteral("to_sequence")).toULongLong(&toOk);
        const QByteArray filename =
            QByteArrayLiteral("htcpc-history-") +
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")).toLatin1() +
            QByteArrayLiteral(".csv");
        sendResponse(socket, 200, "OK", "text/csv; charset=utf-8",
                     historyCsv(fromOk ? fromSequence : 0, toOk ? toSequence : 0),
                     QByteArrayLiteral("attachment; filename=\"") + filename + QByteArrayLiteral("\""));
    } else if (path == QStringLiteral("/health")) {
        sendResponse(socket, 200, "OK", "text/plain; charset=utf-8",
                     QByteArrayLiteral("ok\n"));
    } else if (path == QStringLiteral("/favicon.ico")) {
        sendResponse(socket, 204, "No Content", "image/x-icon", QByteArray());
    } else {
        sendResponse(socket, 404, "Not Found", "text/plain; charset=utf-8",
                     QByteArrayLiteral("Not found.\n"));
    }
}

void RemoteDashboard::forgetSocket(QTcpSocket *socket) {
    m_requestBuffers.remove(socket);
}

void RemoteDashboard::sendResponse(QTcpSocket *socket,
                                   int statusCode,
                                   const QByteArray &reason,
                                   const QByteArray &contentType,
                                   const QByteArray &body,
                                   const QByteArray &contentDisposition) {
    QByteArray response;
    response.reserve(body.size() + 256);
    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(statusCode));
    response.append(' ');
    response.append(reason);
    response.append("\r\nContent-Type: ");
    response.append(contentType);
    response.append("\r\nContent-Length: ");
    response.append(QByteArray::number(body.size()));
    response.append("\r\nCache-Control: no-store\r\nConnection: close\r\n");
    if (!contentDisposition.isEmpty()) {
        response.append("Content-Disposition: ");
        response.append(contentDisposition);
        response.append("\r\n");
    }
    response.append("X-Content-Type-Options: nosniff\r\n\r\n");
    response.append(body);
    socket->write(response);
    socket->disconnectFromHost();
}

QByteArray RemoteDashboard::snapshotJson() const {
    QJsonObject root;
    root.insert(QStringLiteral("service"), QStringLiteral("HTCPC"));
    root.insert(QStringLiteral("state_sequence"), static_cast<double>(m_stateSequence));
    root.insert(QStringLiteral("sample_sequence"), static_cast<double>(m_sampleSequence));
    root.insert(QStringLiteral("acquiring"), m_acquiring);
    root.insert(QStringLiteral("valid"), m_latestValid);
    root.insert(QStringLiteral("unit"), QStringLiteral("个/ml"));
    root.insert(QStringLiteral("history_count"), m_history.size());

    if (m_latestValid) {
        root.insert(QStringLiteral("concentration"), m_latestConcentration);
        root.insert(QStringLiteral("acquisition_time_seconds"), m_latestAcquisitionTime);
    } else {
        root.insert(QStringLiteral("concentration"), QJsonValue::Null);
        root.insert(QStringLiteral("acquisition_time_seconds"), QJsonValue::Null);
    }
    root.insert(QStringLiteral("captured_at"),
                m_latestCapturedAt.isValid()
                    ? m_latestCapturedAt.toString(Qt::ISODateWithMs)
                    : QString());
    root.insert(QStringLiteral("server_time"),
                QDateTime::currentDateTime().toString(Qt::ISODateWithMs));

    QJsonArray history;
    const int first = qMax(0, m_history.size() - SNAPSHOT_HISTORY_POINTS);
    for (int i = first; i < m_history.size(); ++i) {
        const HistoryPoint &point = m_history.at(i);
        QJsonObject item;
        item.insert(QStringLiteral("sequence"), static_cast<double>(point.sequence));
        item.insert(QStringLiteral("time_seconds"), point.acquisitionTimeSeconds);
        item.insert(QStringLiteral("value"), point.concentration);
        item.insert(QStringLiteral("captured_at"), point.capturedAt.toString(Qt::ISODateWithMs));
        history.append(item);
    }
    root.insert(QStringLiteral("history"), history);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray RemoteDashboard::historyCsv(quint64 fromSequence, quint64 toSequence) const {
    QByteArray csv = QByteArray::fromHex("efbbbf");
    csv.append("Sequence,Time(s),Concentration(count/ml),CapturedAt\n");
    for (int i = 0; i < m_history.size(); ++i) {
        const HistoryPoint &point = m_history.at(i);
        if (fromSequence > 0 && point.sequence < fromSequence) continue;
        if (toSequence > 0 && point.sequence > toSequence) continue;
        csv.append(QByteArray::number(point.sequence));
        csv.append(',');
        csv.append(QByteArray::number(point.acquisitionTimeSeconds, 'g', 15));
        csv.append(',');
        csv.append(QByteArray::number(point.concentration, 'g', 15));
        csv.append(',');
        csv.append(point.capturedAt.toString(Qt::ISODateWithMs).toUtf8());
        csv.append('\n');
    }
    return csv;
}

QByteArray RemoteDashboard::dashboardHtml() const {
    QFile page(QStringLiteral(":/web/index.html"));
    if (!page.open(QIODevice::ReadOnly)) {
        return QByteArrayLiteral(
            "<!doctype html><meta charset=utf-8><title>HTCPC</title>"
            "<h1>HTCPC dashboard resource is unavailable.</h1>");
    }
    return page.readAll();
}
