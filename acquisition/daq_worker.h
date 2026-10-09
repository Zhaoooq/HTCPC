#ifndef CPC_ACQUISITION_DAQ_WORKER_H
#define CPC_ACQUISITION_DAQ_WORKER_H

#include <QThread>
#include <QString>
#include <QVector>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "ftd2xx.h"

constexpr int DEVICE_INDEX = 0;
constexpr std::uint32_t CLOCK_HZ = 15000000U;
constexpr std::uint8_t SK_BIT = 0x01;
constexpr std::uint8_t DO_BIT = 0x02;
constexpr std::uint8_t DI_BIT = 0x04;
constexpr std::uint8_t CS_BIT = 0x08;
constexpr std::uint8_t DIR_MASK = (SK_BIT | DO_BIT | CS_BIT);
constexpr std::uint8_t VAL_IDLE = (CS_BIT | SK_BIT);
constexpr std::uint8_t VAL_CS_LOW = SK_BIT;
constexpr std::uint8_t XFER_OPCODE = 0x34;

constexpr int CHUNK_SAMPLES = 4000;
constexpr int TX_BYTES = 13;
constexpr int RX_BYTES = 4;

class D2xxApi {
public:
    virtual ~D2xxApi() = default;
    virtual FT_STATUS open(int deviceIndex, FT_HANDLE *handle) = 0;
    virtual FT_STATUS resetDevice(FT_HANDLE handle) = 0;
    virtual FT_STATUS setUsbParameters(FT_HANDLE handle, DWORD input, DWORD output) = 0;
    virtual FT_STATUS setTimeouts(FT_HANDLE handle, DWORD readMs, DWORD writeMs) = 0;
    virtual FT_STATUS setLatencyTimer(FT_HANDLE handle, UCHAR latencyMs) = 0;
    virtual FT_STATUS setBitMode(FT_HANDLE handle, UCHAR mask, UCHAR mode) = 0;
    virtual FT_STATUS purge(FT_HANDLE handle, ULONG mask) = 0;
    virtual FT_STATUS write(FT_HANDLE handle, void *buffer, DWORD requested, DWORD *written) = 0;
    virtual FT_STATUS read(FT_HANDLE handle, void *buffer, DWORD requested, DWORD *received) = 0;
    virtual FT_STATUS getQueueStatus(FT_HANDLE handle, DWORD *queued) = 0;
    virtual FT_STATUS close(FT_HANDLE handle) = 0;
};

class DaqWorker : public QThread {
    Q_OBJECT

public:
    explicit DaqWorker(QObject *parent = nullptr,
                       D2xxApi *api = nullptr,
                       std::chrono::milliseconds transactionTimeout =
                           std::chrono::milliseconds(2000),
                       std::chrono::microseconds emptyQueueWait =
                           std::chrono::microseconds(200));
    ~DaqWorker() override;

    void startDaq();
    void stopDaq();

signals:
    void dataReady(quint64 sequence, QVector<double> time, QVector<double> voltage);
    void errorOccurred(QString message);

protected:
    void run() override;

private:
    using Deadline = std::chrono::steady_clock::time_point;

    std::atomic<bool> running_;
    FT_HANDLE ftHandle_;
    D2xxApi *api_;
    bool ownsApi_;
    std::chrono::milliseconds transactionTimeout_;
    std::chrono::microseconds emptyQueueWait_;

    bool checkFtStatus(FT_STATUS status, const QString& operation, QString *error) const;
    bool writeAll(const std::uint8_t *data, std::size_t size, const QString& operation,
                  Deadline deadline, QString *error);
    bool readExactly(std::uint8_t *data, std::size_t expected, const QString& operation,
                     Deadline deadline, QString *error);
    bool sendCommand(std::initializer_list<std::uint8_t> command,
                     std::size_t expectedResponseBytes,
                     const QString& operation, QString *error);
    bool initializeDevice(QString *error);
    bool closeDevice(QString *error);
};

#endif // CPC_ACQUISITION_DAQ_WORKER_H
