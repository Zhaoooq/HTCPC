#include "PT100Sensor.h"

#include <QStringList>

#include <fcntl.h>
#include <limits>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace {

constexpr std::uint32_t SPI_SPEED_HZ = 500000;
constexpr std::uint8_t SPI_BITS = 8;
constexpr std::uint8_t MAX31865_CONFIGURATION = 0xD2;
constexpr std::uint8_t MAX31865_FAULT_MASK = 0xFC;

QString systemError(const QString& operation, const QString& device) {
    return QStringLiteral("%1 (%2) failed: errno=%3 (%4)")
        .arg(operation, device)
        .arg(errno)
        .arg(QString::fromLocal8Bit(std::strerror(errno)));
}

} // namespace

PT100Sensor::PT100Sensor(const char* device, float offset, float gain, float bias)
    : spi_fd(-1),
      temp_offset(offset),
      calibration_gain(gain),
      calibration_bias(bias),
      device_name(QString::fromLocal8Bit(device)),
      last_fault_flags(0) {
    spi_fd = open(device, O_RDWR);
    if (spi_fd < 0) {
        last_error = systemError(QStringLiteral("open"), device_name);
        return;
    }

    std::uint8_t mode = SPI_MODE_1;
    std::uint8_t bits = SPI_BITS;
    std::uint32_t speed = SPI_SPEED_HZ;
    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
        last_error = systemError(QStringLiteral("SPI_IOC_WR_MODE"), device_name);
    } else if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        last_error = systemError(QStringLiteral("SPI_IOC_WR_BITS_PER_WORD"), device_name);
    } else if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        last_error = systemError(QStringLiteral("SPI_IOC_WR_MAX_SPEED_HZ"), device_name);
    } else if (!writeConfiguration(MAX31865_CONFIGURATION,
                                   QStringLiteral("MAX31865 configuration"))) {
        // writeConfiguration has already recorded the transfer error.
    } else {
        usleep(100000);
        return;
    }

    close(spi_fd);
    spi_fd = -1;
}

bool PT100Sensor::transfer(std::uint8_t *tx,
                           std::uint8_t *rx,
                           std::uint32_t length,
                           const QString& operation) {
    if (spi_fd < 0) {
        if (last_error.isEmpty()) last_error = QStringLiteral("SPI device is not ready: %1").arg(device_name);
        return false;
    }
    struct spi_ioc_transfer transferDescription = {};
    transferDescription.tx_buf = reinterpret_cast<unsigned long>(tx);
    transferDescription.rx_buf = reinterpret_cast<unsigned long>(rx);
    transferDescription.len = length;
    transferDescription.speed_hz = SPI_SPEED_HZ;
    transferDescription.bits_per_word = SPI_BITS;
    const int result = ioctl(spi_fd, SPI_IOC_MESSAGE(1), &transferDescription);
    if (result != static_cast<int>(length)) {
        last_error = result < 0
            ? systemError(operation, device_name)
            : QStringLiteral("%1 (%2) short transfer: expected %3 bytes, transferred %4")
                  .arg(operation, device_name).arg(length).arg(result);
        return false;
    }
    return true;
}

bool PT100Sensor::writeConfiguration(std::uint8_t configuration,
                                     const QString& operation) {
    std::uint8_t tx[2] = {0x80, configuration};
    std::uint8_t rx[2] = {0, 0};
    return transfer(tx, rx, 2, operation);
}

float PT100Sensor::read_temperature() {
    if (spi_fd < 0) return std::numeric_limits<float>::quiet_NaN();

    std::uint8_t rtdTx[3] = {0x01, 0x00, 0x00};
    std::uint8_t rtdRx[3] = {0, 0, 0};
    if (!transfer(rtdTx, rtdRx, 3, QStringLiteral("MAX31865 RTD read"))) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    std::uint8_t faultTx[2] = {0x07, 0x00};
    std::uint8_t faultRx[2] = {0, 0};
    if (!transfer(faultTx, faultRx, 2, QStringLiteral("MAX31865 fault-status read"))) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    last_fault_flags = faultRx[1] & MAX31865_FAULT_MASK;
    if (last_fault_flags != 0) {
        last_error = QStringLiteral("MAX31865 fault: %1").arg(faultString());
        // Clear the latched register for the next diagnostic read. This sample
        // remains invalid and the controller must require a manual restart.
        writeConfiguration(MAX31865_CONFIGURATION,
                           QStringLiteral("MAX31865 fault clear"));
        return std::numeric_limits<float>::quiet_NaN();
    }

    last_error.clear();
    const std::uint16_t adcCode = static_cast<std::uint16_t>(
        ((static_cast<std::uint16_t>(rtdRx[1]) << 8U) | rtdRx[2]) >> 1U);
    const float resistance = (static_cast<float>(adcCode) * R_REF) / 32768.0f;
    const float rawTemperature = (resistance - PT100_NOMINAL) / PT100_ALPHA;
    const float displayedTemperature = rawTemperature + temp_offset;
    return calibration_gain * displayedTemperature + calibration_bias;
}

bool PT100Sensor::isAvailable() const {
    return isReady();
}

bool PT100Sensor::isReady() const {
    return spi_fd >= 0;
}

QString PT100Sensor::errorString() const {
    return last_error;
}

std::uint8_t PT100Sensor::lastFaultFlags() const {
    return last_fault_flags;
}

bool PT100Sensor::hasFault() const {
    return last_fault_flags != 0;
}

QString PT100Sensor::faultString() const {
    return faultStringForFlags(last_fault_flags);
}

QString PT100Sensor::faultStringForFlags(std::uint8_t flags) {
    QStringList faults;
    if (flags & 0x80) faults << QStringLiteral("RTD High Threshold");
    if (flags & 0x40) faults << QStringLiteral("RTD Low Threshold");
    if (flags & 0x20) faults << QStringLiteral("REFIN- > 0.85 × Bias");
    if (flags & 0x10) faults << QStringLiteral("REFIN- < 0.85 × Bias (FORCE- open)");
    if (flags & 0x08) faults << QStringLiteral("RTDIN- < 0.85 × Bias (FORCE- open)");
    if (flags & 0x04) faults << QStringLiteral("Overvoltage / Undervoltage");
    return faults.isEmpty() ? QStringLiteral("none") : faults.join(QStringLiteral("; "));
}

PT100Sensor::~PT100Sensor() {
    if (spi_fd >= 0) close(spi_fd);
}
