#include "acquisition/daq_worker.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace {

class RealD2xxApi : public D2xxApi {
public:
    FT_STATUS open(int deviceIndex, FT_HANDLE *handle) override {
        return FT_Open(deviceIndex, handle);
    }
    FT_STATUS resetDevice(FT_HANDLE handle) override { return FT_ResetDevice(handle); }
    FT_STATUS setUsbParameters(FT_HANDLE handle, DWORD input, DWORD output) override {
        return FT_SetUSBParameters(handle, input, output);
    }
    FT_STATUS setTimeouts(FT_HANDLE handle, DWORD readMs, DWORD writeMs) override {
        return FT_SetTimeouts(handle, readMs, writeMs);
    }
    FT_STATUS setLatencyTimer(FT_HANDLE handle, UCHAR latencyMs) override {
        return FT_SetLatencyTimer(handle, latencyMs);
    }
    FT_STATUS setBitMode(FT_HANDLE handle, UCHAR mask, UCHAR mode) override {
        return FT_SetBitMode(handle, mask, mode);
    }
    FT_STATUS purge(FT_HANDLE handle, ULONG mask) override { return FT_Purge(handle, mask); }
    FT_STATUS write(FT_HANDLE handle, void *buffer, DWORD requested, DWORD *written) override {
        return FT_Write(handle, buffer, requested, written);
    }
    FT_STATUS read(FT_HANDLE handle, void *buffer, DWORD requested, DWORD *received) override {
        return FT_Read(handle, buffer, requested, received);
    }
    FT_STATUS getQueueStatus(FT_HANDLE handle, DWORD *queued) override {
        return FT_GetQueueStatus(handle, queued);
    }
    FT_STATUS close(FT_HANDLE handle) override { return FT_Close(handle); }
};

QString ftStatusText(FT_STATUS status) {
    return QStringLiteral("FT_STATUS=%1").arg(static_cast<unsigned long>(status));
}

} // namespace

DaqWorker::DaqWorker(QObject *parent,
                     D2xxApi *api,
                     std::chrono::milliseconds transactionTimeout,
                     std::chrono::microseconds emptyQueueWait)
    : QThread(parent),
      running_(false),
      ftHandle_(nullptr),
      api_(api ? api : new RealD2xxApi()),
      ownsApi_(api == nullptr),
      transactionTimeout_(transactionTimeout),
      emptyQueueWait_(emptyQueueWait) {}

DaqWorker::~DaqWorker() {
    stopDaq();
    if (isRunning()) wait();
    if (ownsApi_) delete api_;
}

void DaqWorker::startDaq() {
    if (isRunning()) return;
    running_.store(true);
    start();
}

void DaqWorker::stopDaq() {
    running_.store(false);
}

bool DaqWorker::checkFtStatus(FT_STATUS status,
                              const QString& operation,
                              QString *error) const {
    if (status == FT_OK) return true;
    if (error) *error = QStringLiteral("%1 failed: %2").arg(operation, ftStatusText(status));
    return false;
}

bool DaqWorker::writeAll(const std::uint8_t *data,
                         std::size_t size,
                         const QString& operation,
                         Deadline deadline,
                         QString *error) {
    std::size_t totalWritten = 0;
    FT_STATUS lastStatus = FT_OK;
    while (totalWritten < size && running_.load()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            if (error) {
                *error = QStringLiteral("%1 timeout: expected %2 bytes, written %3 bytes, %4")
                             .arg(operation)
                             .arg(static_cast<qulonglong>(size))
                             .arg(static_cast<qulonglong>(totalWritten))
                             .arg(ftStatusText(lastStatus));
            }
            return false;
        }
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            size - totalWritten, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        lastStatus = api_->write(ftHandle_,
                                 const_cast<std::uint8_t *>(data + totalWritten),
                                 request,
                                 &written);
        if (!checkFtStatus(lastStatus, operation, error)) return false;
        if (written > request) {
            if (error) {
                *error = QStringLiteral("%1 returned invalid byte count: requested %2, written %3")
                             .arg(operation).arg(request).arg(written);
            }
            return false;
        }
        totalWritten += written;
        if (written == 0) std::this_thread::sleep_for(emptyQueueWait_);
    }
    return totalWritten == size;
}

bool DaqWorker::readExactly(std::uint8_t *data,
                            std::size_t expected,
                            const QString& operation,
                            Deadline deadline,
                            QString *error) {
    std::size_t totalReceived = 0;
    FT_STATUS lastStatus = FT_OK;
    while (totalReceived < expected && running_.load()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            if (error) {
                *error = QStringLiteral("%1 timeout: expected %2 bytes, received %3 bytes, %4")
                             .arg(operation)
                             .arg(static_cast<qulonglong>(expected))
                             .arg(static_cast<qulonglong>(totalReceived))
                             .arg(ftStatusText(lastStatus));
            }
            return false;
        }
        DWORD queued = 0;
        lastStatus = api_->getQueueStatus(ftHandle_, &queued);
        if (!checkFtStatus(lastStatus, operation + QStringLiteral(" FT_GetQueueStatus"), error)) {
            return false;
        }
        if (queued == 0) {
            std::this_thread::sleep_for(emptyQueueWait_);
            continue;
        }
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            std::min<std::size_t>(queued, expected - totalReceived),
            std::numeric_limits<DWORD>::max()));
        DWORD received = 0;
        lastStatus = api_->read(ftHandle_, data + totalReceived, request, &received);
        if (!checkFtStatus(lastStatus, operation + QStringLiteral(" FT_Read"), error)) return false;
        if (received > request) {
            if (error) {
                *error = QStringLiteral("%1 returned invalid byte count: requested %2, received %3")
                             .arg(operation).arg(request).arg(received);
            }
            return false;
        }
        totalReceived += received;
        if (received == 0) std::this_thread::sleep_for(emptyQueueWait_);
    }
    return totalReceived == expected;
}

bool DaqWorker::sendCommand(std::initializer_list<std::uint8_t> command,
                            std::size_t expectedResponseBytes,
                            const QString& operation,
                            QString *error) {
    std::vector<std::uint8_t> tx(command);
    std::vector<std::uint8_t> rx(expectedResponseBytes, 0);
    if (!checkFtStatus(api_->purge(ftHandle_, FT_PURGE_RX | FT_PURGE_TX),
                       operation + QStringLiteral(" FT_Purge"), error)) return false;
    if (!writeAll(tx.data(), tx.size(), operation + QStringLiteral(" FT_Write"),
                  std::chrono::steady_clock::now() + transactionTimeout_, error)) return false;
    return readExactly(rx.data(), rx.size(), operation + QStringLiteral(" FT_Read"),
                       std::chrono::steady_clock::now() + transactionTimeout_, error);
}

bool DaqWorker::initializeDevice(QString *error) {
    if (!checkFtStatus(api_->resetDevice(ftHandle_), QStringLiteral("FT_ResetDevice"), error) ||
        !checkFtStatus(api_->setUsbParameters(ftHandle_, 262144, 262144),
                       QStringLiteral("FT_SetUSBParameters"), error) ||
        !checkFtStatus(api_->setTimeouts(ftHandle_, 500, 500),
                       QStringLiteral("FT_SetTimeouts"), error) ||
        !checkFtStatus(api_->setLatencyTimer(ftHandle_, 2),
                       QStringLiteral("FT_SetLatencyTimer"), error) ||
        !checkFtStatus(api_->setBitMode(ftHandle_, 0x00, 0x00),
                       QStringLiteral("FT_SetBitMode reset"), error)) return false;
    msleep(50);
    if (!running_.load()) return false;
    if (!checkFtStatus(api_->setBitMode(ftHandle_, 0x00, 0x02),
                       QStringLiteral("FT_SetBitMode MPSSE"), error)) return false;
    msleep(50);
    if (!running_.load()) return false;
    if (!checkFtStatus(api_->purge(ftHandle_, FT_PURGE_RX | FT_PURGE_TX),
                       QStringLiteral("FT_Purge before MPSSE initialization"), error)) return false;

    const std::uint32_t divisor = (60000000U / (2U * CLOCK_HZ)) - 1U;
    const std::uint8_t initCommands[] = {
        0x8A, 0x97, 0x8D, 0x85, 0x86,
        static_cast<std::uint8_t>(divisor & 0xFFU),
        static_cast<std::uint8_t>((divisor >> 8U) & 0xFFU),
        0x80, VAL_IDLE, DIR_MASK
    };
    if (!writeAll(initCommands, sizeof(initCommands), QStringLiteral("MPSSE initialization"),
                  std::chrono::steady_clock::now() + transactionTimeout_, error)) return false;

    return sendCommand({0x80, VAL_CS_LOW, DIR_MASK, XFER_OPCODE, 0x03, 0x00, 0x85,
                        0x00, 0x00, 0x00, 0x80, VAL_IDLE, DIR_MASK, 0x87},
                       4,
                       QStringLiteral("ADS8688 reset"), error) &&
           sendCommand({0x80, VAL_CS_LOW, DIR_MASK, XFER_OPCODE, 0x02, 0x00, 0x0D,
                        0x05, 0x00, 0x80, VAL_IDLE, DIR_MASK, 0x87},
                       3,
                       QStringLiteral("ADS8688 CH1 range configuration"), error) &&
           sendCommand({0x80, VAL_CS_LOW, DIR_MASK, XFER_OPCODE, 0x03, 0x00, 0xC4,
                        0x00, 0x00, 0x00, 0x80, VAL_IDLE, DIR_MASK, 0x87},
                       4,
                       QStringLiteral("ADS8688 CH1 selection"), error) &&
           sendCommand({0x80, VAL_CS_LOW, DIR_MASK, XFER_OPCODE, 0x03, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x80, VAL_IDLE, DIR_MASK, 0x87},
                       4,
                       QStringLiteral("ADS8688 dummy conversion"), error);
}

bool DaqWorker::closeDevice(QString *error) {
    if (!ftHandle_) return true;
    const FT_STATUS status = api_->close(ftHandle_);
    ftHandle_ = nullptr;
    return checkFtStatus(status, QStringLiteral("FT_Close"), error);
}

void DaqWorker::run() {
    if (!running_.load()) return;
    QString error;
    const FT_STATUS openStatus = api_->open(DEVICE_INDEX, &ftHandle_);
    if (!checkFtStatus(openStatus,
                       QStringLiteral("FT_Open device index %1").arg(DEVICE_INDEX), &error)) {
        running_.store(false);
        emit errorOccurred(QStringLiteral("DAQ initialization failed: %1").arg(error));
        return;
    }
    if (!initializeDevice(&error)) {
        const bool stopped = !running_.load();
        QString closeError;
        closeDevice(&closeError);
        running_.store(false);
        if (!stopped) {
            if (!closeError.isEmpty()) error += QStringLiteral("; ") + closeError;
            emit errorOccurred(QStringLiteral("DAQ initialization failed: %1").arg(error));
        }
        return;
    }

    std::vector<std::uint8_t> txChunk(CHUNK_SAMPLES * TX_BYTES);
    for (int sample = 0; sample < CHUNK_SAMPLES; ++sample) {
        const std::uint8_t command[TX_BYTES] = {
            0x80, VAL_CS_LOW, DIR_MASK, XFER_OPCODE, 0x03, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x80, VAL_IDLE, DIR_MASK
        };
        std::memcpy(txChunk.data() + (sample * TX_BYTES), command, TX_BYTES);
    }
    std::vector<std::uint8_t> rxChunk(CHUNK_SAMPLES * RX_BYTES);
    QVector<double> time(CHUNK_SAMPLES);
    QVector<double> voltage(CHUNK_SAMPLES);
    double currentTime = 0.0;
    quint64 sequence = 0;

    while (running_.load()) {
        const auto chunkStart = std::chrono::steady_clock::now();
        if (!writeAll(txChunk.data(), txChunk.size(), QStringLiteral("ADS8688 acquisition FT_Write"),
                      chunkStart + transactionTimeout_, &error) ||
            !readExactly(rxChunk.data(), rxChunk.size(),
                         QStringLiteral("ADS8688 acquisition FT_Read"),
                         chunkStart + transactionTimeout_, &error)) {
            if (running_.load()) emit errorOccurred(error);
            break;
        }
        if (!running_.load()) break;

        const auto chunkEnd = std::chrono::steady_clock::now();
        const double sampleInterval =
            std::chrono::duration<double>(chunkEnd - chunkStart).count() / CHUNK_SAMPLES;
        for (int sample = 0; sample < CHUNK_SAMPLES; ++sample) {
            const std::uint16_t raw =
                (static_cast<std::uint16_t>(rxChunk[sample * RX_BYTES + 2]) << 8U) |
                rxChunk[sample * RX_BYTES + 3];
            time[sample] = currentTime;
            voltage[sample] = (static_cast<double>(raw) / 65535.0) * 10.24;
            currentTime += sampleInterval;
        }
        emit dataReady(sequence++, time, voltage);
    }

    QString closeError;
    if (!closeDevice(&closeError)) emit errorOccurred(closeError);
    running_.store(false);
}
