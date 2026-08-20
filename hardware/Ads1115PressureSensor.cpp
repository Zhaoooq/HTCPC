#include "Ads1115PressureSensor.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <QStringList>

namespace {

constexpr double ADS_LSB_VOLTS = 0.000125; // PGA = +/-4.096 V
constexpr double DIVIDER_RESTORE = (10000.0 + 18000.0) / 18000.0;
constexpr double EMA_ALPHA = 0.20;
constexpr int CHANNEL_COUNT = 3;
constexpr int MEDIAN_SAMPLES = 9;
constexpr int CONVERSION_INTERVAL_MS = 9; // 写入配置后首次检查；128 SPS 单次转换约需 7.8 ms
constexpr int CONVERSION_POLL_INTERVAL_MS = 1;
constexpr int CONVERSION_TIMEOUT_MS = 50;
constexpr int DISPLAY_INTERVAL_MS = 100;
constexpr int ZERO_TIME_MS = 3000;

// OS=1, PGA=+/-4.096 V, single-shot, 128 SPS, comparator off.
constexpr std::uint16_t ADS_CONFIG_BASE = 0x8383;

} // namespace

Ads1115PressureSensor::Ads1115PressureSensor(const QString& i2cDevice,
                                             std::uint8_t i2cAddress,
                                             QObject *parent)
    : QObject(parent),
      i2cDevice_(i2cDevice),
      i2cAddress_(i2cAddress),
      i2cFd_(-1),
      ready_(false),
      zeroing_(false),
      conversionPending_(false),
      currentChannel_(0),
      consecutiveReadErrors_(0),
      lastZeroProgress_(-1) {
    channels_[0].fullScalePa = 40000.0;
    channels_[1].fullScalePa = 500.0;
    channels_[2].fullScalePa = 300.0;

    sampleTimer_.setInterval(CONVERSION_INTERVAL_MS);
    sampleTimer_.setTimerType(Qt::PreciseTimer);
    connect(&sampleTimer_, &QTimer::timeout, this, &Ads1115PressureSensor::sampleOnce);
}

Ads1115PressureSensor::~Ads1115PressureSensor() {
    stop();
}

bool Ads1115PressureSensor::start() {
    stop();

    const QByteArray devicePath = i2cDevice_.toLocal8Bit();
    i2cFd_ = ::open(devicePath.constData(), O_RDWR);
    if (i2cFd_ < 0) {
        setError(QString("打开 %1").arg(i2cDevice_));
        emit errorOccurred(lastError_);
        return false;
    }

    if (::ioctl(i2cFd_, I2C_SLAVE, static_cast<int>(i2cAddress_)) < 0) {
        setError(QString("选择 I2C 地址 0x%1")
                     .arg(i2cAddress_, 2, 16, QLatin1Char('0'))
                     .toUpper());
        ::close(i2cFd_);
        i2cFd_ = -1;
        emit errorOccurred(lastError_);
        return false;
    }

    ready_ = true;
    currentChannel_ = 0;
    consecutiveReadErrors_ = 0;
    if (!beginConversion(currentChannel_)) {
        ready_ = false;
        ::close(i2cFd_);
        i2cFd_ = -1;
        emit errorOccurred(lastError_);
        return false;
    }
    conversionPending_ = true;
    startZeroCalibration();
    sampleTimer_.start();
    return true;
}

void Ads1115PressureSensor::stop() {
    sampleTimer_.stop();
    ready_ = false;
    zeroing_ = false;
    conversionPending_ = false;
    if (i2cFd_ >= 0) {
        ::close(i2cFd_);
        i2cFd_ = -1;
    }
}

bool Ads1115PressureSensor::isReady() const {
    return ready_;
}

bool Ads1115PressureSensor::isZeroing() const {
    return zeroing_;
}

QString Ads1115PressureSensor::errorString() const {
    return lastError_;
}

void Ads1115PressureSensor::startZeroCalibration() {
    if (!ready_) {
        emit errorOccurred("ADS1115 尚未就绪，无法进行零点校准。");
        return;
    }

    zeroing_ = true;
    lastZeroProgress_ = -1;
    for (ChannelState& state : channels_) {
        state.zeroSamples.clear();
        state.recentAdcVoltages.clear();
        state.hasFilteredPressure = false;
        state.lastDisplayMs = 0;
    }
    zeroTimer_.start();
    emit zeroCalibrationProgress(0);
    emit statusChanged("三路压差传感器正在进行 3 秒零点校准，请保持气泵关闭且所有 H/L 两侧压力相同...");
}

void Ads1115PressureSensor::sampleOnce() {
    if (!conversionPending_) {
        if (beginConversion(currentChannel_)) {
            conversionPending_ = true;
            sampleTimer_.start(CONVERSION_INTERVAL_MS);
        } else {
            handleCommunicationFailure();
        }
        return;
    }

    bool conversionReady = false;
    if (!readConversionReady(&conversionReady)) {
        handleCommunicationFailure();
        return;
    }
    if (!conversionReady) {
        if (conversionTimer_.elapsed() >= CONVERSION_TIMEOUT_MS) {
            lastError_ = QString("等待 ADS1115 A%1 转换完成超时。").arg(currentChannel_);
            conversionPending_ = false;
            handleCommunicationFailure();
        } else {
            sampleTimer_.start(CONVERSION_POLL_INTERVAL_MS);
        }
        return;
    }

    double adcVoltage = 0.0;
    conversionPending_ = false;
    if (!readAdcVoltage(&adcVoltage)) {
        handleCommunicationFailure();
        return;
    }

    consecutiveReadErrors_ = 0;
    processVoltage(currentChannel_, adcVoltage);
    currentChannel_ = (currentChannel_ + 1) % CHANNEL_COUNT;

    if (beginConversion(currentChannel_)) {
        conversionPending_ = true;
        // QTimer 原本从上一次 timeout 计时，I2C 读写会吃掉部分转换窗口。
        // 此处重新启动，保证 9 ms 从本次配置写完后开始计算。
        sampleTimer_.start(CONVERSION_INTERVAL_MS);
    } else {
        handleCommunicationFailure();
    }
}

bool Ads1115PressureSensor::beginConversion(int channel) {
    if (channel < 0 || channel >= CHANNEL_COUNT) {
        lastError_ = "ADS1115 通道编号无效。";
        return false;
    }

    const std::uint16_t mux = static_cast<std::uint16_t>(4 + channel) << 12;
    const std::uint16_t configValue = ADS_CONFIG_BASE | mux;
    const std::uint8_t config[] = {
        0x01,
        static_cast<std::uint8_t>((configValue >> 8) & 0xFF),
        static_cast<std::uint8_t>(configValue & 0xFF)
    };
    if (::write(i2cFd_, config, sizeof(config)) != static_cast<ssize_t>(sizeof(config))) {
        setError(QString("启动 ADS1115 A%1 转换").arg(channel));
        return false;
    }
    conversionTimer_.restart();
    return true;
}

bool Ads1115PressureSensor::readConversionReady(bool *ready) {
    const std::uint8_t configRegister = 0x01;
    if (::write(i2cFd_, &configRegister, 1) != 1) {
        setError(QString("选择 ADS1115 A%1 配置寄存器").arg(currentChannel_));
        return false;
    }

    std::uint8_t bytes[2] = {0, 0};
    if (::read(i2cFd_, bytes, sizeof(bytes)) != static_cast<ssize_t>(sizeof(bytes))) {
        setError(QString("读取 ADS1115 A%1 转换状态").arg(currentChannel_));
        return false;
    }

    *ready = (bytes[0] & 0x80U) != 0;
    return true;
}

bool Ads1115PressureSensor::readAdcVoltage(double *voltage) {
    const std::uint8_t conversionRegister = 0x00;
    if (::write(i2cFd_, &conversionRegister, 1) != 1) {
        setError(QString("选择 ADS1115 A%1 转换寄存器").arg(currentChannel_));
        return false;
    }

    std::uint8_t bytes[2] = {0, 0};
    if (::read(i2cFd_, bytes, sizeof(bytes)) != static_cast<ssize_t>(sizeof(bytes))) {
        setError(QString("读取 ADS1115 A%1").arg(currentChannel_));
        return false;
    }

    const std::uint16_t rawUnsigned =
        (static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1];
    const std::int16_t raw = static_cast<std::int16_t>(rawUnsigned);
    *voltage = static_cast<double>(raw) * ADS_LSB_VOLTS;
    return std::isfinite(*voltage);
}

void Ads1115PressureSensor::processVoltage(int channel, double adcVoltage) {
    ChannelState& state = channels_[channel];
    const double sensorVoltage = sensorVoltageFromAdc(adcVoltage);

    if (zeroing_) {
        state.zeroSamples.push_back(sensorVoltage);
        const int elapsedMs = static_cast<int>(zeroTimer_.elapsed());
        const int progress = std::min(100, elapsedMs * 100 / ZERO_TIME_MS);
        if (progress != lastZeroProgress_) {
            lastZeroProgress_ = progress;
            emit zeroCalibrationProgress(progress);
        }

        bool allChannelsHaveSamples = true;
        for (const ChannelState& item : channels_) {
            if (item.zeroSamples.empty()) {
                allChannelsHaveSamples = false;
                break;
            }
        }
        if (elapsedMs < ZERO_TIME_MS || !allChannelsHaveSamples) return;

        zeroing_ = false;
        measurementTimer_.start();
        for (int index = 0; index < CHANNEL_COUNT; ++index) {
            ChannelState& calibrated = channels_[index];
            calibrated.zeroVoltage = median(calibrated.zeroSamples);
            calibrated.zeroSamples.clear();
            calibrated.recentAdcVoltages.clear();
            calibrated.hasFilteredPressure = false;
            calibrated.lastDisplayMs = 0;
            emit zeroCalibrationFinished(
                index,
                calibrated.zeroVoltage,
                calibrated.zeroVoltage * calibrated.fullScalePa / 5.0);
        }
        emit statusChanged("三路压差传感器零点校准完成，正在测量。");
        return;
    }

    state.recentAdcVoltages.push_back(adcVoltage);
    if (state.recentAdcVoltages.size() > MEDIAN_SAMPLES) {
        state.recentAdcVoltages.erase(state.recentAdcVoltages.begin());
    }
    const qint64 nowMs = measurementTimer_.elapsed();
    if (state.recentAdcVoltages.size() < MEDIAN_SAMPLES ||
        nowMs - state.lastDisplayMs < DISPLAY_INTERVAL_MS) {
        return;
    }
    state.lastDisplayMs = nowMs;

    const double medianAdcVoltage = median(state.recentAdcVoltages);
    const double medianSensorVoltage = sensorVoltageFromAdc(medianAdcVoltage);
    const double pressurePerVolt = state.fullScalePa / 5.0;
    const double pressurePa = (medianSensorVoltage - state.zeroVoltage) * pressurePerVolt;
    if (!state.hasFilteredPressure) {
        state.filteredPressurePa = pressurePa;
        state.hasFilteredPressure = true;
    } else {
        state.filteredPressurePa = EMA_ALPHA * pressurePa +
                                   (1.0 - EMA_ALPHA) * state.filteredPressurePa;
    }

    QStringList warnings;
    if (medianAdcVoltage > 3.25) warnings << "ADC 输入接近 3.3 V 上限";
    if (medianSensorVoltage > 5.10) warnings << "传感器输出超过 5 V";
    if (state.filteredPressurePa > state.fullScalePa) {
        warnings << QString("超过 %1 Pa 量程").arg(state.fullScalePa, 0, 'f', 0);
    }
    if (state.filteredPressurePa < -0.0125 * state.fullScalePa) {
        warnings << "请检查 H/L 是否接反";
    }

    emit pressureUpdated(channel,
                         state.filteredPressurePa,
                         medianAdcVoltage,
                         medianSensorVoltage,
                         state.filteredPressurePa / state.fullScalePa * 100.0,
                         warnings.join("；"));
}

void Ads1115PressureSensor::handleCommunicationFailure() {
    ++consecutiveReadErrors_;
    if (consecutiveReadErrors_ >= 3) {
        sampleTimer_.stop();
        ready_ = false;
        conversionPending_ = false;
    }
    if (consecutiveReadErrors_ == 1 || consecutiveReadErrors_ >= 3) {
        emit errorOccurred(lastError_);
    }
}

void Ads1115PressureSensor::setError(const QString& operation) {
    lastError_ = QString("%1失败：%2")
                     .arg(operation, QString::fromLocal8Bit(std::strerror(errno)));
}

double Ads1115PressureSensor::median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    if (values.size() % 2 != 0) return values[middle];
    const double upper = values[middle];
    std::nth_element(values.begin(), values.begin() + middle - 1, values.begin() + middle);
    return (values[middle - 1] + upper) * 0.5;
}

double Ads1115PressureSensor::sensorVoltageFromAdc(double adcVoltage) {
    return adcVoltage * DIVIDER_RESTORE;
}
