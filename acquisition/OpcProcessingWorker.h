#ifndef CPC_ACQUISITION_OPCPROCESSINGWORKER_H
#define CPC_ACQUISITION_OPCPROCESSINGWORKER_H

#include <QMutex>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

#include <deque>

#include "algorithms/OpcCounter.h"

struct OpcProcessedChunk {
    quint64 sequence = 0;
    QVector<double> displayTime;
    QVector<double> displayVoltage;
    OpcCountResult countResult;
    double chunkDurationSeconds = 0.0;
    double lastSampleTime = 0.0;
};

Q_DECLARE_METATYPE(OpcProcessedChunk)

class OpcProcessingWorker : public QThread {
    Q_OBJECT

public:
    explicit OpcProcessingWorker(QObject *parent = nullptr);
    ~OpcProcessingWorker() override;

    void beginSession();
    void endSession();
    void shutdown();
    void setParams(const OpcParams& params);
    bool enqueueChunk(quint64 sequence,
                      const QVector<double>& time,
                      const QVector<double>& voltage);
    int queuedChunkCount() const;
    static int queueCapacity();

signals:
    void processedChunkReady(OpcProcessedChunk chunk);
    void processingError(QString message);

protected:
    void run() override;

private:
    struct PendingChunk {
        quint64 sequence;
        QVector<double> time;
        QVector<double> voltage;
        OpcParams params;
    };

    mutable QMutex mutex_;
    QWaitCondition queueChanged_;
    std::deque<PendingChunk> queue_;
    OpcParams params_;
    bool accepting_;
    bool shuttingDown_;
    bool overflowReported_;
    bool sequenceInitialized_;
    quint64 previousSequence_;
    double previousLastTime_;
};

#endif // CPC_ACQUISITION_OPCPROCESSINGWORKER_H
