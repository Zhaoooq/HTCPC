#ifndef DAQ_WORKER_H
#define DAQ_WORKER_H

#include <QThread>
#include <QVector>
#include <QString>
#include <QDebug>
#include <chrono>
#include <atomic>
#include "ftd2xx.h"

// 复用之前的常量配置
constexpr int DEVICE_INDEX = 0;
constexpr uint32_t CLOCK_HZ = 15000000U;
constexpr uint8_t SK_BIT = 0x01, DO_BIT = 0x02, DI_BIT = 0x04, CS_BIT = 0x08;
constexpr uint8_t DIR_MASK = (SK_BIT | DO_BIT | CS_BIT);
constexpr uint8_t VAL_IDLE = (CS_BIT | SK_BIT);
constexpr uint8_t VAL_CS_LOW = (SK_BIT);
constexpr uint8_t XFER_OPCODE = 0x34;

constexpr int CHUNK_SAMPLES = 4000;
constexpr int TX_BYTES = 13;
constexpr int RX_BYTES = 4;

class DaqWorker : public QThread {
    Q_OBJECT
public:
    explicit DaqWorker(QObject *parent = nullptr) : QThread(parent), m_running(false), ftHandle(nullptr) {}

    void startDaq() {
        if (isRunning()) return;
        m_running.store(true);
        start();
    }

    void stopDaq() {
        m_running.store(false);
    }

signals:
    // 将采集到的时间数组和电压数组发送给 UI
    void dataReady(QVector<double> time, QVector<double> voltage);
    void errorOccurred(QString msg);

protected:
    void run() override {
        // startDaq() 在启动线程前设置运行标志。若用户立即点击停止，
        // 这里必须保留已经发出的停止请求，不能重新将标志改回 true。
        if (!m_running.load()) return;

        if (FT_Open(DEVICE_INDEX, &ftHandle) != FT_OK) {
            m_running.store(false);
            emit errorOccurred("无法打开 FTDI 采集设备，请检查设备连接和访问权限。");
            return;
        }

        auto closeDevice = [&]() {
            if (ftHandle) {
                FT_Close(ftHandle);
                ftHandle = nullptr;
            }
            m_running.store(false);
        };

        if (!m_running.load()) {
            closeDevice();
            return;
        }

        // 初始化 FTDI 和 ADS8688 (与之前代码相同)
        FT_ResetDevice(ftHandle);
        FT_SetUSBParameters(ftHandle, 262144, 262144);
        FT_SetTimeouts(ftHandle, 5000, 5000);
        FT_SetLatencyTimer(ftHandle, 2);
        FT_SetBitMode(ftHandle, 0x00, 0x00); msleep(50);
        if (!m_running.load()) {
            closeDevice();
            return;
        }
        FT_SetBitMode(ftHandle, 0x00, 0x02); msleep(50);
        if (!m_running.load()) {
            closeDevice();
            return;
        }

        FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
        uint32_t div = (60000000U / (2U * CLOCK_HZ)) - 1U;
        uint8_t init_cmds[] = { 0x8A, 0x97, 0x8D, 0x85, 0x86, (uint8_t)(div & 0xFF), (uint8_t)((div >> 8) & 0xFF), 0x80, VAL_IDLE, DIR_MASK };
        DWORD written = 0;
        FT_Write(ftHandle, init_cmds, sizeof(init_cmds), &written);

        // ADS8688 配置：复位 -> CH1 10.24V -> 选 CH1 -> Dummy
        sendCmd({0x80, VAL_CS_LOW, DIR_MASK, XFER_OPCODE, 0x03, 0x00, 0x85, 0x00, 0x00, 0x00, 0x80, VAL_IDLE, DIR_MASK, 0x87});
        if (!m_running.load()) {
            closeDevice();
            return;
        }
        sendCmd({0x80, VAL_CS_LOW, DIR_MASK, XFER_OPCODE, 0x02, 0x00, 0x0D, 0x05, 0x00, 0x80, VAL_IDLE, DIR_MASK, 0x87});
        if (!m_running.load()) {
            closeDevice();
            return;
        }
        sendCmd({0x80, VAL_CS_LOW, DIR_MASK, XFER_OPCODE, 0x03, 0x00, 0xC4, 0x00, 0x00, 0x00, 0x80, VAL_IDLE, DIR_MASK, 0x87});
        if (!m_running.load()) {
            closeDevice();
            return;
        }
        sendCmd({0x80, VAL_CS_LOW, DIR_MASK, XFER_OPCODE, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, VAL_IDLE, DIR_MASK, 0x87});
        if (!m_running.load()) {
            closeDevice();
            return;
        }

        // 准备批量传输数据块
        std::vector<uint8_t> tx_chunk(CHUNK_SAMPLES * TX_BYTES);
        int idx = 0;
        for (int i = 0; i < CHUNK_SAMPLES; i++) {
            uint8_t cmd[] = {0x80, VAL_CS_LOW, DIR_MASK, XFER_OPCODE, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, VAL_IDLE, DIR_MASK};
            memcpy(&tx_chunk[idx], cmd, TX_BYTES);
            idx += TX_BYTES;
        }
        std::vector<uint8_t> rx_chunk(CHUNK_SAMPLES * RX_BYTES);

        double currentTime = 0.0;
        QVector<double> tData(CHUNK_SAMPLES), vData(CHUNK_SAMPLES);

        // 核心采集循环
        while (m_running.load()) {
            auto chunk_start = std::chrono::high_resolution_clock::now();
            
            FT_Write(ftHandle, tx_chunk.data(), tx_chunk.size(), &written);
            
            DWORD total_rx = 0;
            while (total_rx < rx_chunk.size() && m_running.load()) {
                DWORD rxq = 0; FT_GetQueueStatus(ftHandle, &rxq);
                if (rxq > 0) {
                    DWORD want = rx_chunk.size() - total_rx;
                    if (rxq < want) want = rxq;
                    DWORD got = 0;
                    FT_Read(ftHandle, rx_chunk.data() + total_rx, want, &got);
                    total_rx += got;
                }
            }

            if (!m_running.load()) break;

            auto chunk_end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> chunk_elapsed = chunk_end - chunk_start;
            double dt = chunk_elapsed.count() / CHUNK_SAMPLES;

            // 解析数据
            for (int i = 0; i < CHUNK_SAMPLES; i++) {
                uint16_t raw = (rx_chunk[i * 4 + 2] << 8) | rx_chunk[i * 4 + 3];
                tData[i] = currentTime;
                vData[i] = (static_cast<float>(raw) / 65535.0f) * 10.24f;
                currentTime += dt;
            }

            // 通过信号将这 4000 个点发送给 UI 线程
            emit dataReady(tData, vData);
        }

        closeDevice();
    }

private:
    std::atomic<bool> m_running;
    FT_HANDLE ftHandle;

    void sendCmd(const std::initializer_list<uint8_t>& cmd) {
        std::vector<uint8_t> tx(cmd);
        std::vector<uint8_t> rx(4, 0);
        DWORD w, r;
        FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
        FT_Write(ftHandle, tx.data(), tx.size(), &w);
        FT_Read(ftHandle, rx.data(), rx.size(), &r);
    }
};

#endif // DAQ_WORKER_H
