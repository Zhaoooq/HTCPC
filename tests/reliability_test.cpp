#include "LiquidControlSystem.h"
#include "acquisition/OpcProcessingWorker.h"
#include "acquisition/RawDataWriter.h"
#include "control/TemperaturePid.h"
#include "hardware/PT100Sensor.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

namespace {

QVector<double> increasingTime(double start) {
    QVector<double> result;
    for (int i = 0; i < 100; ++i) result.append(start + i * 0.001);
    return result;
}

bool waitForSignal(QEventLoop& loop, int timeoutMs = 2000) {
    bool timedOut = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });
    timer.start(timeoutMs);
    loop.exec();
    return !timedOut;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qRegisterMetaType<OpcProcessedChunk>("OpcProcessedChunk");

    // Bounded queue: a non-running consumer makes overflow deterministic.
    {
        OpcProcessingWorker worker;
        worker.beginSession();
        bool overflowReported = false;
        QObject::connect(&worker, &OpcProcessingWorker::processingError,
                         [&](const QString& message) {
            overflowReported = message.contains("queue overflow");
        });
        const QVector<double> time = increasingTime(0.0);
        const QVector<double> voltage(time.size(), 0.0);
        for (int i = 0; i < OpcProcessingWorker::queueCapacity(); ++i) {
            assert(worker.enqueueChunk(static_cast<quint64>(i), time, voltage));
        }
        assert(!worker.enqueueChunk(99, time, voltage));
        assert(overflowReported);
        assert(worker.queuedChunkCount() == OpcProcessingWorker::queueCapacity());
    }

    // Sequence and time continuity errors must be explicit.
    {
        OpcProcessingWorker worker;
        worker.start();
        worker.beginSession();
        QVector<double> time = increasingTime(0.0);
        QVector<double> voltage(time.size(), 0.0);
        QEventLoop firstProcessed;
        QObject::connect(&worker, &OpcProcessingWorker::processedChunkReady,
                         &firstProcessed, [&](const OpcProcessedChunk&) { firstProcessed.quit(); });
        assert(worker.enqueueChunk(0, time, voltage));
        assert(waitForSignal(firstProcessed));

        QString sequenceError;
        QEventLoop sequenceLoop;
        QObject::connect(&worker, &OpcProcessingWorker::processingError,
                         &sequenceLoop, [&](const QString& message) {
            sequenceError = message;
            sequenceLoop.quit();
        });
        assert(worker.enqueueChunk(2, increasingTime(1.0), voltage));
        assert(waitForSignal(sequenceLoop));
        assert(sequenceError.contains("sequence discontinuity"));
        worker.shutdown();
        worker.wait();
    }
    {
        OpcProcessingWorker worker;
        worker.start();
        worker.beginSession();
        QVector<double> badTime = increasingTime(0.0);
        badTime[20] = badTime[19];
        const QVector<double> voltage(badTime.size(), 0.0);
        QString timeError;
        QEventLoop loop;
        QObject::connect(&worker, &OpcProcessingWorker::processingError,
                         &loop, [&](const QString& message) {
            timeError = message;
            loop.quit();
        });
        assert(worker.enqueueChunk(0, badTime, voltage));
        assert(waitForSignal(loop));
        assert(timeError.contains("non-monotonic"));
        worker.shutdown();
        worker.wait();
    }

    // Realistic variable dt values stay bounded; invalid/long values fail safe.
    for (double dt : {0.3, 0.5, 0.8}) {
        HybridCoolingPID cooling;
        PredictiveHeatingPID heating;
        const double coolingOutput = cooling.compute(12.0, dt);
        const double heatingOutput = heating.compute(35.0, dt);
        assert(std::isfinite(coolingOutput) && coolingOutput >= 0.0 && coolingOutput <= 100.0);
        assert(std::isfinite(heatingOutput) && heatingOutput >= 0.0 && heatingOutput <= 100.0);
    }
    for (double dt : {0.0, -0.5, std::numeric_limits<double>::quiet_NaN(), 10.0}) {
        HybridCoolingPID cooling;
        PredictiveHeatingPID heating;
        assert(cooling.compute(12.0, dt) == 0.0);
        assert(heating.compute(35.0, dt) == 0.0);
        assert(std::isfinite(cooling.prev_temp));
        assert(std::isfinite(heating.prev_temp));
    }

    // MAX31865 fault-status register D7..D2, per the manufacturer table.
    const std::vector<std::pair<std::uint8_t, QString>> faultCases = {
        {0x80, "RTD High Threshold"},
        {0x40, "RTD Low Threshold"},
        {0x20, "REFIN- > 0.85"},
        {0x10, "REFIN- < 0.85"},
        {0x08, "RTDIN- < 0.85"},
        {0x04, "Overvoltage / Undervoltage"}
    };
    for (const auto& faultCase : faultCases) {
        assert(PT100Sensor::faultStringForFlags(faultCase.first).contains(faultCase.second));
    }
    assert(PT100Sensor::faultStringForFlags(0).contains("none"));

    // A failed inlet-close operation must never attempt to open the outlet.
    {
        bool outletOpenAttempted = false;
        QString error;
        const bool opened = LiquidControlSystem::closeInletThenOpenOutlet(
            [&](int pin, int level) {
                if (pin == 22 && level == 0) return -7;
                if (pin == 6 && level == 1) outletOpenAttempted = true;
                return 0;
            },
            22,
            6,
            &error);
        assert(!opened);
        assert(!outletOpenAttempted);
        assert(error.contains("GPIO22") && error.contains("-7"));
    }
    {
        std::vector<int> writes;
        QString error;
        assert(LiquidControlSystem::closeInletThenOpenOutlet(
            [&](int pin, int level) {
                writes.push_back(pin * 10 + level);
                return 0;
            },
            22,
            6,
            &error));
        assert((writes == std::vector<int>{220, 61}));
        assert(error.isEmpty());
    }

    // CSV work is asynchronous and retains the established file format.
    {
        QTemporaryDir directory;
        assert(directory.isValid());
        const QString path = directory.filePath("raw.csv");
        RawDataWriter writer;
        writer.start();
        QString stoppedFile;
        qint64 stoppedSamples = -1;
        bool stoppedSuccessfully = false;
        QEventLoop loop;
        QObject::connect(&writer, &RawDataWriter::recordingStopped,
                         &loop, [&](const QString& fileName, qint64 sampleCount,
                                    bool success, const QString&, bool) {
            stoppedFile = fileName;
            stoppedSamples = sampleCount;
            stoppedSuccessfully = success;
            loop.quit();
        });
        assert(writer.startRecording(path));
        writer.enqueueChunk(QVector<double>{0.0, 0.5}, QVector<double>{1.25, 2.5});
        writer.stopRecording(true);
        assert(waitForSignal(loop));
        assert(stoppedSuccessfully);
        assert(stoppedFile == path);
        assert(stoppedSamples == 2);
        writer.shutdown();
        writer.wait();

        QFile csv(path);
        assert(csv.open(QIODevice::ReadOnly | QIODevice::Text));
        assert(csv.readAll() == QByteArray("Time(s),Voltage(V)\n"
                                           "0.000000,1.25000\n"
                                           "0.500000,2.50000\n"));
    }

    return 0;
}
