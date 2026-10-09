#include "network/CpcTcpServer.h"
#include "network/RemoteDashboard.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QThread>

#include <cassert>
#include <functional>

namespace {

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 2000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

QByteArray httpGet(quint16 port, const QByteArray& path) {
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    assert(waitUntil([&]() {
        return socket.state() == QAbstractSocket::ConnectedState;
    }));
    const QByteArray request = "GET " + path +
        " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    assert(socket.write(request) == request.size());
    assert(waitUntil([&]() {
        return socket.state() == QAbstractSocket::UnconnectedState &&
               socket.bytesAvailable() > 0;
    }));
    return socket.readAll();
}

QByteArray responseBody(const QByteArray& response) {
    const int separator = response.indexOf("\r\n\r\n");
    assert(separator >= 0);
    return response.mid(separator + 4);
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    CpcTcpServer tcpServer;
    QString error;
    assert(tcpServer.start(0, &error));
    assert(tcpServer.serverPort() != 0);

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, tcpServer.serverPort());
    assert(waitUntil([&]() { return tcpServer.hasClient(); }));

    tcpServer.publishParticleResult(12.3454, true);
    assert(waitUntil([&]() { return client.canReadLine(); }));
    assert(client.readLine() == QByteArray("$CPC,1,1,12.345,0\r\n"));

    tcpServer.publishParticleResult(123.0, false);
    assert(waitUntil([&]() { return client.canReadLine(); }));
    assert(client.readLine() == QByteArray("$CPC,1,2,0.000,2\r\n"));
    tcpServer.stop();

    RemoteDashboard dashboard;
    assert(dashboard.start(0, &error));
    dashboard.setAcquiring(true);
    dashboard.publishParticleConcentration(1.25, 42.5, true);

    const QByteArray snapshotResponse = httpGet(
        dashboard.serverPort(), QByteArrayLiteral("/api/snapshot"));
    assert(snapshotResponse.startsWith("HTTP/1.1 200 OK\r\n"));
    const QJsonDocument snapshot = QJsonDocument::fromJson(
        responseBody(snapshotResponse));
    assert(snapshot.isObject());
    const QJsonObject root = snapshot.object();
    assert(root.value(QStringLiteral("service")).toString() == QStringLiteral("HTCPC"));
    assert(root.value(QStringLiteral("acquiring")).toBool());
    assert(root.value(QStringLiteral("valid")).toBool());
    assert(root.value(QStringLiteral("unit")).toString() == QStringLiteral("个/ml"));
    assert(root.value(QStringLiteral("concentration")).toDouble() == 42.5);
    assert(root.value(QStringLiteral("history_count")).toInt() == 1);

    const QByteArray csvResponse = httpGet(
        dashboard.serverPort(), QByteArrayLiteral("/api/history.csv"));
    assert(csvResponse.startsWith("HTTP/1.1 200 OK\r\n"));
    const QByteArray csv = responseBody(csvResponse);
    assert(csv.contains("Sequence,Time(s),Concentration(count/ml),CapturedAt"));
    assert(csv.contains("1,1.25,42.5,"));
    dashboard.stop();
    return 0;
}
