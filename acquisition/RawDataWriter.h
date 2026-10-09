#ifndef CPC_ACQUISITION_RAWDATAWRITER_H
#define CPC_ACQUISITION_RAWDATAWRITER_H

#include <QMutex>
#include <QString>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

#include <deque>

class RawDataWriter : public QThread {
    Q_OBJECT

public:
    explicit RawDataWriter(QObject *parent = nullptr);
    ~RawDataWriter() override;

    bool startRecording(const QString& fileName);
    void stopRecording(bool showCompletionMessage);
    void enqueueChunk(const QVector<double>& time, const QVector<double>& voltage);
    void shutdown();

signals:
    void recordingError(QString message);
    void recordingStopped(QString fileName,
                          qint64 sampleCount,
                          bool success,
                          QString error,
                          bool showCompletionMessage);

protected:
    void run() override;

private:
    enum class TaskType { Start, Chunk, Stop, Shutdown };
    struct Task {
        TaskType type = TaskType::Chunk;
        QString fileName;
        QVector<double> time;
        QVector<double> voltage;
        bool showCompletionMessage = false;
    };

    mutable QMutex mutex_;
    QWaitCondition tasksChanged_;
    std::deque<Task> tasks_;
    bool acceptingChunks_;
    bool shuttingDown_;
    int queuedChunks_;
};

#endif // CPC_ACQUISITION_RAWDATAWRITER_H
