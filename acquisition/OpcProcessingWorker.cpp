#include "OpcProcessingWorker.h"

#include <QMutexLocker>

#include <algorithm>
#include <cmath>

namespace {

constexpr int QUEUE_CAPACITY = 5;
constexpr int DISPLAY_POINTS_PER_CHUNK = 1000;

bool hasStrictlyIncreasingTime(const QVector<double>& time) {
    if (time.isEmpty() || !std::isfinite(time.first())) return false;
    for (int i = 1; i < time.size(); ++i) {
        if (!std::isfinite(time.at(i)) || time.at(i) <= time.at(i - 1)) return false;
    }
    return true;
}

} // namespace

OpcProcessingWorker::OpcProcessingWorker(QObject *parent)
    : QThread(parent),
      accepting_(false),
      shuttingDown_(false),
      overflowReported_(false),
      sequenceInitialized_(false),
      previousSequence_(0),
      previousLastTime_(0.0) {}

OpcProcessingWorker::~OpcProcessingWorker() {
    shutdown();
    wait();
}

void OpcProcessingWorker::beginSession() {
    QMutexLocker lock(&mutex_);
    queue_.clear();
    accepting_ = true;
    overflowReported_ = false;
    sequenceInitialized_ = false;
    previousSequence_ = 0;
    previousLastTime_ = 0.0;
    queueChanged_.wakeAll();
}

void OpcProcessingWorker::endSession() {
    QMutexLocker lock(&mutex_);
    accepting_ = false;
    queue_.clear();
    queueChanged_.wakeAll();
}

void OpcProcessingWorker::shutdown() {
    QMutexLocker lock(&mutex_);
    accepting_ = false;
    shuttingDown_ = true;
    queue_.clear();
    queueChanged_.wakeAll();
}

void OpcProcessingWorker::setParams(const OpcParams& params) {
    QMutexLocker lock(&mutex_);
    params_ = params;
}

bool OpcProcessingWorker::enqueueChunk(quint64 sequence,
                                       const QVector<double>& time,
                                       const QVector<double>& voltage) {
    QMutexLocker lock(&mutex_);
    if (!accepting_ || shuttingDown_) return false;
    if (static_cast<int>(queue_.size()) >= QUEUE_CAPACITY) {
        accepting_ = false;
        if (!overflowReported_) {
            overflowReported_ = true;
            emit processingError(QStringLiteral(
                "OPC processing queue overflow: capacity %1 chunks; acquisition stopped")
                                     .arg(QUEUE_CAPACITY));
        }
        return false;
    }
    queue_.push_back(PendingChunk{sequence, time, voltage, params_});
    queueChanged_.wakeOne();
    return true;
}

int OpcProcessingWorker::queuedChunkCount() const {
    QMutexLocker lock(&mutex_);
    return static_cast<int>(queue_.size());
}

int OpcProcessingWorker::queueCapacity() {
    return QUEUE_CAPACITY;
}

void OpcProcessingWorker::run() {
    for (;;) {
        PendingChunk pending;
        {
            QMutexLocker lock(&mutex_);
            while (queue_.empty() && !shuttingDown_) queueChanged_.wait(&mutex_);
            if (shuttingDown_) return;
            pending = queue_.front();
            queue_.pop_front();
        }

        if (pending.time.size() != pending.voltage.size() ||
            !hasStrictlyIncreasingTime(pending.time)) {
            emit processingError(QStringLiteral(
                "OPC chunk %1 has invalid sizes or non-monotonic sample time")
                                     .arg(pending.sequence));
            endSession();
            continue;
        }

        bool continuityError = false;
        QString continuityMessage;
        {
            QMutexLocker lock(&mutex_);
            if (sequenceInitialized_) {
                if (pending.sequence != previousSequence_ + 1) {
                    continuityError = true;
                    continuityMessage = QStringLiteral(
                        "OPC chunk sequence discontinuity: expected %1, received %2")
                                            .arg(previousSequence_ + 1)
                                            .arg(pending.sequence);
                } else if (pending.time.first() <= previousLastTime_) {
                    continuityError = true;
                    continuityMessage = QStringLiteral(
                        "OPC chunk time discontinuity at sequence %1: first sample %2, previous last %3")
                                            .arg(pending.sequence)
                                            .arg(pending.time.first(), 0, 'g', 16)
                                            .arg(previousLastTime_, 0, 'g', 16);
                }
            }
            if (!continuityError) {
                sequenceInitialized_ = true;
                previousSequence_ = pending.sequence;
                previousLastTime_ = pending.time.last();
            }
        }
        if (continuityError) {
            emit processingError(continuityMessage);
            endSession();
            continue;
        }

        OpcProcessedChunk result;
        result.sequence = pending.sequence;
        result.countResult = analyzeOpcPulseSignal(
            pending.time, pending.voltage, pending.params);
        result.chunkDurationSeconds = estimateChunkDurationSeconds(pending.time);
        result.lastSampleTime = pending.time.last();
        const int stride = std::max(1, pending.time.size() / DISPLAY_POINTS_PER_CHUNK);
        result.displayTime.reserve((pending.time.size() + stride - 1) / stride);
        result.displayVoltage.reserve((pending.voltage.size() + stride - 1) / stride);
        for (int i = 0; i < pending.time.size(); i += stride) {
            result.displayTime.append(pending.time.at(i));
            result.displayVoltage.append(pending.voltage.at(i));
        }
        emit processedChunkReady(result);
    }
}
