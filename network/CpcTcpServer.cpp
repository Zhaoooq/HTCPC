#include "CpcTcpServer.h"

#include <QAbstractSocket>
#include <QDebug>
#include <QHostAddress>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <cmath>

namespace {

constexpr int PROTOCOL_VERSION = 1;
constexpr qint64 MAX_PENDING_BYTES = 64 * 1024;

} // namespace

CpcTcpServer::CpcTcpServer(QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this)) {
    connect(m_server, &QTcpServer::newConnection,
            this, &CpcTcpServer::acceptPendingConnections);
    connect(m_server, &QTcpServer::acceptError, this,
            [this](QAbstractSocket::SocketError) {
                const QString message = m_server->errorString();
                qWarning() << "CPC TCP Server accept error:" << message;
                emit errorOccurred(message);
            });
}

CpcTcpServer::~CpcTcpServer() {
    stop();
}

bool CpcTcpServer::start(quint16 port, QString *errorMessage) {
    if (m_server->isListening()) {
        if (m_server->serverPort() == port) return true;
        const QString message = QStringLiteral("CPC TCP 已在端口 %1 监听")
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

void CpcTcpServer::stop() {
    const bool wasListening = m_server->isListening();
    const bool hadClient = m_clientSocket != nullptr;
    if (m_clientSocket) {
        QTcpSocket *socket = m_clientSocket;
        m_clientSocket = nullptr;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    if (hadClient) emit clientDisconnected();

    if (wasListening) m_server->close();
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket) continue;
        socket->abort();
        socket->deleteLater();
    }
    if (wasListening) emit listeningChanged(false);
}

bool CpcTcpServer::isListening() const {
    return m_server->isListening();
}

bool CpcTcpServer::hasClient() const {
    return m_clientSocket &&
           m_clientSocket->state() == QAbstractSocket::ConnectedState;
}

quint16 CpcTcpServer::serverPort() const {
    return m_server->serverPort();
}

QString CpcTcpServer::clientAddress() const {
    return hasClient() ? formatPeerAddress(m_clientSocket) : QString();
}

void CpcTcpServer::publishParticleResult(double concentration, bool valid) {
    ++m_sequence;

    if (!hasClient()) return;

    if (m_clientSocket->bytesToWrite() > MAX_PENDING_BYTES) {
        qWarning() << "CPC TCP client send buffer exceeds"
                   << MAX_PENDING_BYTES
                   << "bytes; dropping current particle result frame";
        return;
    }

    const bool valueValid = valid && std::isfinite(concentration);
    const double outputValue = valueValid ? concentration : 0.0;
    const int status = valueValid ? 0 : 2;
    const QString frame = QStringLiteral("$CPC,%1,%2,%3,%4\r\n")
        .arg(PROTOCOL_VERSION)
        .arg(m_sequence)
        .arg(outputValue, 0, 'f', 3)
        .arg(status);

    if (m_clientSocket->write(frame.toLatin1()) < 0) {
        const QString message = m_clientSocket->errorString();
        qWarning().noquote() << QStringLiteral("CPC TCP write error: %1").arg(message);
        emit errorOccurred(message);
        return;
    }
    emit frameSent(m_sequence, QDateTime::currentDateTime());
    qDebug().noquote() << "TX:" << frame.trimmed();
}

void CpcTcpServer::acceptPendingConnections() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket) continue;

        if (hasClient()) {
            rejectExtraClient(socket);
            continue;
        }

        attachClient(socket);
    }
}

void CpcTcpServer::rejectExtraClient(QTcpSocket *socket) {
    qWarning().noquote()
        << QStringLiteral("CPC TCP extra client rejected: %1")
               .arg(formatPeerAddress(socket));
    socket->disconnectFromHost();
    socket->deleteLater();
}

void CpcTcpServer::attachClient(QTcpSocket *socket) {
    m_clientSocket = socket;
    m_clientSocket->setParent(this);

    qInfo().noquote()
        << QStringLiteral("CPC TCP client connected: %1")
               .arg(formatPeerAddress(m_clientSocket));
    emit clientConnected(formatPeerAddress(m_clientSocket));

    connect(m_clientSocket, &QTcpSocket::readyRead, this, [this]() {
        if (m_clientSocket) m_clientSocket->readAll();
    });
    connect(m_clientSocket, &QTcpSocket::disconnected, this, [this, socket]() {
        clearClient(socket);
        socket->deleteLater();
    });
    connect(m_clientSocket,
            &QAbstractSocket::errorOccurred,
            this,
            [this, socket](QAbstractSocket::SocketError) {
                const QString message = socket->errorString();
                qWarning().noquote()
                    << QStringLiteral("CPC TCP client socket error from %1: %2")
                           .arg(formatPeerAddress(socket), message);
                emit errorOccurred(message);
            });
}

void CpcTcpServer::clearClient(QTcpSocket *socket) {
    if (socket != m_clientSocket) return;

    qInfo() << "CPC TCP client disconnected";
    m_clientSocket = nullptr;
    emit clientDisconnected();
}

QString CpcTcpServer::formatPeerAddress(const QTcpSocket *socket) {
    if (!socket) return QStringLiteral("<unknown>");

    const QHostAddress peer = socket->peerAddress();
    bool ipv4Ok = false;
    const quint32 ipv4 = peer.toIPv4Address(&ipv4Ok);
    const QString address = ipv4Ok ? QHostAddress(ipv4).toString() : peer.toString();
    return address.isEmpty() ? QStringLiteral("<unknown>") : address;
}
