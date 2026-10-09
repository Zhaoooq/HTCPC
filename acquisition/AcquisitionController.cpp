#include "AcquisitionController.h"

#include "RawDataWriter.h"
#include "daq_worker.h"

#include <cmath>
#include <limits>

namespace {

constexpr double PARTICLE_DISPLAY_SMOOTHING_ALPHA = 0.65;

} // namespace

AcquisitionController::AcquisitionController(QObject *parent)
    : QObject(parent),
      daqWorker_(new DaqWorker()),
      processingWorker_(new OpcProcessingWorker()),
      rawDataWriter_(new RawDataWriter()),
      errorReported_(false),
      shutdownComplete_(false),
      particleAccumulator_(1.0),
      smoothedParticleValue_(std::numeric_limits<double>::quiet_NaN()) {
    processingWorker_->start();
    rawDataWriter_->start();

    connect(daqWorker_, &DaqWorker::dataReady, processingWorker_,
            [this](quint64 sequence, const QVector<double>& time,
                   const QVector<double>& voltage) {
        if (!processingWorker_->enqueueChunk(sequence, time, voltage)) {
            daqWorker_->stopDaq();
        }
        rawDataWriter_->enqueueChunk(time, voltage);
    }, Qt::DirectConnection);
    connect(processingWorker_, &OpcProcessingWorker::processedChunkReady,
            this, [this](const OpcProcessedChunk& chunk) {
        emit processedChunkReady(chunk);
        double rawCountRate = 0.0;
        if (!particleAccumulator_.addChunk(
                chunk.countResult.totalCount, chunk.chunkDurationSeconds, rawCountRate)) {
            return;
        }
        const double calibrated = applyParticleCountCalibration(rawCountRate, particleCalibration_);
        const bool valid = std::isfinite(calibrated);
        if (valid) {
            smoothedParticleValue_ = std::isfinite(smoothedParticleValue_)
                ? PARTICLE_DISPLAY_SMOOTHING_ALPHA * calibrated +
                      (1.0 - PARTICLE_DISPLAY_SMOOTHING_ALPHA) * smoothedParticleValue_
                : calibrated;
        } else {
            smoothedParticleValue_ = std::numeric_limits<double>::quiet_NaN();
        }
        emit particleResultReady(chunk.lastSampleTime, smoothedParticleValue_, valid);
    });
    connect(processingWorker_, &OpcProcessingWorker::processingError,
            this, [this](const QString& message) {
        daqWorker_->stopDaq();
        reportAcquisitionError(message);
    }, Qt::DirectConnection);
    connect(daqWorker_, &DaqWorker::errorOccurred, this,
            [this](const QString& message) {
        processingWorker_->endSession();
        reportAcquisitionError(message);
    }, Qt::DirectConnection);
    connect(daqWorker_, &QThread::finished, this, [this]() {
        processingWorker_->endSession();
        emit acquisitionFinished();
    });
    connect(rawDataWriter_, &RawDataWriter::recordingError,
            this, &AcquisitionController::recordingError);
    connect(rawDataWriter_, &RawDataWriter::recordingStopped,
            this, &AcquisitionController::recordingStopped);
}

AcquisitionController::~AcquisitionController() {
    shutdown();
    delete daqWorker_;
    delete processingWorker_;
    delete rawDataWriter_;
}

bool AcquisitionController::start(const OpcParams& params) {
    if (shutdownComplete_ || daqWorker_->isRunning()) return false;
    errorReported_.store(false);
    particleAccumulator_.reset();
    smoothedParticleValue_ = std::numeric_limits<double>::quiet_NaN();
    processingWorker_->setParams(params);
    processingWorker_->beginSession();
    daqWorker_->startDaq();
    return true;
}

void AcquisitionController::setParticleCalibration(
    const ParticleCalibrationParams& calibration) {
    particleCalibration_ = calibration;
    particleAccumulator_.reset();
    smoothedParticleValue_ = std::numeric_limits<double>::quiet_NaN();
}

void AcquisitionController::stop() {
    daqWorker_->stopDaq();
    processingWorker_->endSession();
}

bool AcquisitionController::isWorkerRunning() const {
    return daqWorker_->isRunning();
}

bool AcquisitionController::startRecording(const QString& fileName) {
    return rawDataWriter_->startRecording(fileName);
}

void AcquisitionController::stopRecording(bool showCompletionMessage) {
    rawDataWriter_->stopRecording(showCompletionMessage);
}

void AcquisitionController::shutdown() {
    if (shutdownComplete_) return;
    shutdownComplete_ = true;
    stop();
    rawDataWriter_->stopRecording(false);
    daqWorker_->wait();
    processingWorker_->shutdown();
    processingWorker_->wait();
    rawDataWriter_->shutdown();
    rawDataWriter_->wait();
}

void AcquisitionController::reportAcquisitionError(const QString& message) {
    bool expected = false;
    if (errorReported_.compare_exchange_strong(expected, true)) {
        emit acquisitionError(message);
    }
}
