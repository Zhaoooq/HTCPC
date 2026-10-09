#ifndef CPC_NETWORK_CPCTCPSERVER_H
#define CPC_NETWORK_CPCTCPSERVER_H

#include <QObject>
#include <QDateTime>
#include <QString>
#include <QtGlobal>

class QTcpServer;
class QTcpSocket;

class CpcTcpServer final : public QObject {
    Q_OBJECT

public:
    explicit CpcTcpServer(QObject *parent = nullptr);
    ~CpcTcpServer() override;

    bool start(quint16 port = 5000, QString *errorMessage = nullptr);
    void stop();

    bool isListening() const;
    bool hasClient() const;
    quint16 serverPort() const;
    QString clientAddress() const;

    void publishParticleResult(double concentration, bool valid);

signals:
    void listeningChanged(bool listening);
    void clientConnected(const QString &address);
    void clientDisconnected();
    void errorOccurred(const QString &message);
    void frameSent(quint32 sequence, const QDateTime &sentAt);

private:
    void acceptPendingConnections();
    void rejectExtraClient(QTcpSocket *socket);
    void attachClient(QTcpSocket *socket);
    void clearClient(QTcpSocket *socket);
    static QString formatPeerAddress(const QTcpSocket *socket);

    QTcpServer *m_server = nullptr;
    QTcpSocket *m_clientSocket = nullptr;
    quint32 m_sequence = 0;
};

#endif // CPC_NETWORK_CPCTCPSERVER_H
