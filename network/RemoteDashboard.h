#ifndef CPC_NETWORK_REMOTEDASHBOARD_H
#define CPC_NETWORK_REMOTEDASHBOARD_H

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QVector>

class QTcpServer;
class QTcpSocket;

class RemoteDashboard final : public QObject {
    Q_OBJECT

public:
    explicit RemoteDashboard(QObject *parent = nullptr);

    bool start(quint16 port = 8080, QString *errorMessage = nullptr);
    void stop();
    void setAcquiring(bool acquiring);
    void resetMeasurements();
    void publishParticleConcentration(double acquisitionTimeSeconds,
                                      double concentration,
                                      bool valid);

    bool isListening() const;
    quint16 serverPort() const;

signals:
    void listeningChanged(bool listening);
    void errorOccurred(const QString &message);

private slots:
    void acceptPendingConnections();

private:
    struct HistoryPoint {
        double acquisitionTimeSeconds = 0.0;
        double concentration = 0.0;
        QDateTime capturedAt;
        quint64 sequence = 0;
    };

    void readRequest(QTcpSocket *socket);
    void forgetSocket(QTcpSocket *socket);
    void sendResponse(QTcpSocket *socket,
                      int statusCode,
                      const QByteArray &reason,
                      const QByteArray &contentType,
                      const QByteArray &body,
                      const QByteArray &contentDisposition = QByteArray());
    QByteArray snapshotJson() const;
    QByteArray historyCsv(quint64 fromSequence = 0, quint64 toSequence = 0) const;
    QByteArray dashboardHtml() const;

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_requestBuffers;
    QVector<HistoryPoint> m_history;
    bool m_acquiring = false;
    bool m_latestValid = false;
    double m_latestConcentration = 0.0;
    double m_latestAcquisitionTime = 0.0;
    QDateTime m_latestCapturedAt;
    quint64 m_stateSequence = 0;
    quint64 m_sampleSequence = 0;
};

#endif // CPC_NETWORK_REMOTEDASHBOARD_H
