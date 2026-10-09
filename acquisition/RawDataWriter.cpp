#include "RawDataWriter.h"

#include <QElapsedTimer>
#include <QFile>
#include <QMutexLocker>

#include <algorithm>

namespace {

constexpr int WRITER_QUEUE_CAPACITY = 10;

} // namespace

RawDataWriter::RawDataWriter(QObject *parent)
    : QThread(parent), acceptingChunks_(false), shuttingDown_(false), queuedChunks_(0) {}

RawDataWriter::~RawDataWriter() {
    shutdown();
    wait();
}

bool RawDataWriter::startRecording(const QString& fileName) {
    if (fileName.isEmpty()) return false;
    QMutexLocker lock(&mutex_);
    if (acceptingChunks_ || shuttingDown_) return false;
    Task task;
    task.type = TaskType::Start;
    task.fileName = fileName;
    tasks_.push_back(task);
    acceptingChunks_ = true;
    queuedChunks_ = 0;
    tasksChanged_.wakeOne();
    return true;
}

void RawDataWriter::stopRecording(bool showCompletionMessage) {
    QMutexLocker lock(&mutex_);
    if (!acceptingChunks_) return;
    acceptingChunks_ = false;
    Task task;
    task.type = TaskType::Stop;
    task.showCompletionMessage = showCompletionMessage;
    tasks_.push_back(task);
    tasksChanged_.wakeOne();
}

void RawDataWriter::enqueueChunk(const QVector<double>& time,
                                 const QVector<double>& voltage) {
    QMutexLocker lock(&mutex_);
    if (!acceptingChunks_ || shuttingDown_) return;
    if (queuedChunks_ >= WRITER_QUEUE_CAPACITY) {
        acceptingChunks_ = false;
        Task stop;
        stop.type = TaskType::Stop;
        tasks_.push_back(stop);
        tasksChanged_.wakeOne();
        emit recordingError(QStringLiteral(
            "CSV writer queue overflow: capacity %1 chunks; recording stopped")
                                .arg(WRITER_QUEUE_CAPACITY));
        return;
    }
    Task task;
    task.type = TaskType::Chunk;
    task.time = time;
    task.voltage = voltage;
    tasks_.push_back(task);
    ++queuedChunks_;
    tasksChanged_.wakeOne();
}

void RawDataWriter::shutdown() {
    QMutexLocker lock(&mutex_);
    if (shuttingDown_) return;
    acceptingChunks_ = false;
    shuttingDown_ = true;
    Task task;
    task.type = TaskType::Shutdown;
    tasks_.push_back(task);
    tasksChanged_.wakeAll();
}

void RawDataWriter::run() {
    QFile file;
    qint64 sampleCount = 0;
    QElapsedTimer flushTimer;

    auto closeRecording = [&](bool showCompletionMessage) {
        if (!file.isOpen()) return;
        const QString fileName = file.fileName();
        const bool success = file.flush();
        const QString error = success ? QString() : file.errorString();
        file.close();
        emit recordingStopped(fileName, sampleCount, success, error, showCompletionMessage);
        sampleCount = 0;
    };

    for (;;) {
        Task task;
        {
            QMutexLocker lock(&mutex_);
            while (tasks_.empty()) tasksChanged_.wait(&mutex_);
            task = tasks_.front();
            tasks_.pop_front();
            if (task.type == TaskType::Chunk) --queuedChunks_;
        }

        if (task.type == TaskType::Shutdown) {
            closeRecording(false);
            return;
        }
        if (task.type == TaskType::Start) {
            closeRecording(false);
            file.setFileName(task.fileName);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                {
                    QMutexLocker lock(&mutex_);
                    acceptingChunks_ = false;
                }
                emit recordingError(QStringLiteral("无法打开 CSV 文件：%1").arg(file.errorString()));
                continue;
            }
            const QByteArray header("Time(s),Voltage(V)\n");
            if (file.write(header) != header.size()) {
                const QString error = file.errorString();
                file.close();
                {
                    QMutexLocker lock(&mutex_);
                    acceptingChunks_ = false;
                }
                emit recordingError(QStringLiteral("无法写入 CSV 文件头：%1").arg(error));
                continue;
            }
            sampleCount = 0;
            flushTimer.start();
            continue;
        }
        if (task.type == TaskType::Stop) {
            closeRecording(task.showCompletionMessage);
            continue;
        }
        if (!file.isOpen()) continue;

        const int count = std::min(task.time.size(), task.voltage.size());
        QByteArray csvChunk;
        csvChunk.reserve(count * 24);
        for (int i = 0; i < count; ++i) {
            csvChunk.append(QByteArray::number(task.time.at(i), 'f', 6));
            csvChunk.append(',');
            csvChunk.append(QByteArray::number(task.voltage.at(i), 'f', 5));
            csvChunk.append('\n');
        }
        if (file.write(csvChunk) != csvChunk.size()) {
            const QString error = file.errorString();
            file.close();
            {
                QMutexLocker lock(&mutex_);
                acceptingChunks_ = false;
            }
            emit recordingError(QStringLiteral("写入 CSV 文件失败，已停止保存：%1").arg(error));
            continue;
        }
        sampleCount += count;
        if (flushTimer.elapsed() >= 1000) {
            if (!file.flush()) {
                const QString error = file.errorString();
                file.close();
                {
                    QMutexLocker lock(&mutex_);
                    acceptingChunks_ = false;
                }
                emit recordingError(QStringLiteral("刷新 CSV 文件失败，已停止保存：%1").arg(error));
            } else {
                flushTimer.restart();
            }
        }
    }
}
