#include "daq_worker.h"

#include <cassert>
#include <chrono>
#include <cstring>

class FakeD2xxApi : public D2xxApi {
public:
    enum class Failure { None, QueueStatus, Read, Disconnect, ZeroWrite };

    explicit FakeD2xxApi(Failure failure) : failure_(failure) {}

    FT_STATUS open(int, FT_HANDLE *handle) override {
        *handle = reinterpret_cast<FT_HANDLE>(0x1);
        return FT_OK;
    }
    FT_STATUS resetDevice(FT_HANDLE) override { return FT_OK; }
    FT_STATUS setUsbParameters(FT_HANDLE, DWORD, DWORD) override { return FT_OK; }
    FT_STATUS setTimeouts(FT_HANDLE, DWORD, DWORD) override { return FT_OK; }
    FT_STATUS setLatencyTimer(FT_HANDLE, UCHAR) override { return FT_OK; }
    FT_STATUS setBitMode(FT_HANDLE, UCHAR, UCHAR) override { return FT_OK; }
    FT_STATUS purge(FT_HANDLE, ULONG) override {
        ++purgeCalls_;
        // The first purge precedes MPSSE setup. The following four purges
        // precede reset, the 3-byte range write, channel selection and dummy conversion.
        static const DWORD initializationResponseSizes[] = {4, 3, 4, 4};
        if (purgeCalls_ >= 2 && purgeCalls_ <= 5) {
            availableInitializationBytes_ = initializationResponseSizes[purgeCalls_ - 2];
        }
        return FT_OK;
    }

    FT_STATUS write(FT_HANDLE, void *, DWORD requested, DWORD *written) override {
        ++writeCalls;
        if (failure_ == Failure::ZeroWrite) {
            *written = 0;
        } else {
            *written = requested > 1 ? requested / 2 : requested;
        }
        return FT_OK;
    }

    FT_STATUS getQueueStatus(FT_HANDLE, DWORD *queued) override {
        if (initializationTransactions_ >= 4 && failure_ == Failure::QueueStatus) {
            return FT_IO_ERROR;
        }
        if (initializationTransactions_ >= 4 && failure_ == Failure::ZeroWrite) {
            *queued = 0;
        } else if (initializationTransactions_ < 4) {
            *queued = availableInitializationBytes_;
        } else {
            *queued = 65536;
        }
        return FT_OK;
    }

    FT_STATUS read(FT_HANDLE, void *buffer, DWORD requested, DWORD *received) override {
        if (initializationTransactions_ >= 4) {
            if (failure_ == Failure::Read) return FT_IO_ERROR;
            if (failure_ == Failure::Disconnect) return FT_DEVICE_NOT_FOUND;
        }
        const DWORD amount = requested > 1 ? requested / 2 : requested;
        std::memset(buffer, 0, amount);
        *received = amount;
        ++readCalls;
        if (initializationTransactions_ < 4) {
            assert(amount <= availableInitializationBytes_);
            availableInitializationBytes_ -= amount;
            if (availableInitializationBytes_ == 0) {
                ++initializationTransactions_;
            }
        }
        return FT_OK;
    }

    FT_STATUS close(FT_HANDLE) override {
        closed = true;
        return FT_OK;
    }

    int writeCalls = 0;
    int readCalls = 0;
    bool closed = false;

private:
    Failure failure_;
    int purgeCalls_ = 0;
    int initializationTransactions_ = 0;
    DWORD availableInitializationBytes_ = 0;
};

void expectFiniteFailure(FakeD2xxApi::Failure failure, const QString& expectedText) {
    FakeD2xxApi api(failure);
    DaqWorker worker(nullptr, &api,
                     std::chrono::milliseconds(20),
                     std::chrono::microseconds(50));
    QString error;
    QObject::connect(&worker, &DaqWorker::errorOccurred,
                     &worker,
                     [&](const QString& message) { error = message; }, Qt::DirectConnection);
    const auto start = std::chrono::steady_clock::now();
    worker.startDaq();
    assert(worker.wait(1000));
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    assert(elapsed < 1.0);
    assert(error.contains(expectedText));
    assert(api.closed);
}

int main() {
    // Repeated short writes and reads must be completed rather than accepted as full I/O.
    {
        FakeD2xxApi api(FakeD2xxApi::Failure::None);
        DaqWorker worker(nullptr, &api,
                         std::chrono::milliseconds(200),
                         std::chrono::microseconds(10));
        bool receivedChunk = false;
        QObject::connect(&worker, &DaqWorker::dataReady,
                         &worker,
                         [&](quint64 sequence, const QVector<double>& time,
                             const QVector<double>& voltage) {
            assert(sequence == 0);
            assert(time.size() == CHUNK_SAMPLES);
            assert(voltage.size() == CHUNK_SAMPLES);
            receivedChunk = true;
            worker.stopDaq();
        }, Qt::DirectConnection);
        worker.startDaq();
        assert(worker.wait(2000));
        assert(receivedChunk);
        assert(api.writeCalls > 10);
        assert(api.readCalls > 10);
        assert(api.closed);
    }

    expectFiniteFailure(FakeD2xxApi::Failure::QueueStatus, "FT_GetQueueStatus");
    expectFiniteFailure(FakeD2xxApi::Failure::Read, "FT_Read");
    expectFiniteFailure(FakeD2xxApi::Failure::Disconnect, "FT_STATUS");
    expectFiniteFailure(FakeD2xxApi::Failure::ZeroWrite, "timeout");
    return 0;
}
