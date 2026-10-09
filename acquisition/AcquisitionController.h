#ifndef CPC_ACQUISITION_ACQUISITIONCONTROLLER_H
#define CPC_ACQUISITION_ACQUISITIONCONTROLLER_H

#include <QObject>

#include <atomic>

#include "OpcProcessingWorker.h"

class DaqWorker;
class RawDataWriter;

class AcquisitionController : public QObject {
    Q_OBJECT

public:
    explicit AcquisitionController(QObject *parent = nullptr);
    ~AcquisitionController() override;

    bool start(const OpcParams& params);
    void setParticleCalibration(const ParticleCalibrationParams& calibration);
    void stop();
    bool isWorkerRunning() const;
    bool startRecording(const QString& fileName);
    void stopRecording(bool showCompletionMessage);
    void shutdown();

signals:
    void processedChunkReady(OpcProcessedChunk chunk);
    void particleResultReady(double sampleTime, double value, bool valid);
    void acquisitionError(QString message);
    void acquisitionFinished();
    void recordingError(QString message);
    void recordingStopped(QString fileName,
                          qint64 sampleCount,
                          bool success,
                          QString error,
                          bool showCompletionMessage);

private:
    void reportAcquisitionError(const QString& message);

    DaqWorker *daqWorker_;
    OpcProcessingWorker *processingWorker_;
    RawDataWriter *rawDataWriter_;
    std::atomic<bool> errorReported_;
    bool shutdownComplete_;
    ParticleCountRateAccumulator particleAccumulator_;
    ParticleCalibrationParams particleCalibration_;
    double smoothedParticleValue_;
};

#endif // CPC_ACQUISITION_ACQUISITIONCONTROLLER_H
