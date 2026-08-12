#include "PT100Sensor.h"

#include <fcntl.h>
#include <limits>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

PT100Sensor::PT100Sensor(const char* device, float offset, float gain, float bias)
    : spi_fd(-1),
      temp_offset(offset),
      calibration_gain(gain),
      calibration_bias(bias) {
    spi_fd = open(device, O_RDWR);
    if (spi_fd < 0) return;

    uint8_t mode = SPI_MODE_1;
    ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    uint8_t bits = 8;
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    uint32_t speed = 500000;
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    uint8_t tx[2] = {0x80, 0xD2};
    uint8_t rx[2] = {0};
    struct spi_ioc_transfer tr = {};
    tr.tx_buf = reinterpret_cast<unsigned long>(tx);
    tr.rx_buf = reinterpret_cast<unsigned long>(rx);
    tr.len = 2;
    tr.speed_hz = speed;
    tr.bits_per_word = bits;
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
    usleep(100000);
}

float PT100Sensor::read_temperature() {
    if (spi_fd < 0) return std::numeric_limits<float>::quiet_NaN();

    uint8_t tx[3] = {0x01, 0x00, 0x00};
    uint8_t rx[3] = {0};
    struct spi_ioc_transfer tr = {};
    tr.tx_buf = reinterpret_cast<unsigned long>(tx);
    tr.rx_buf = reinterpret_cast<unsigned long>(rx);
    tr.len = 3;
    tr.speed_hz = 500000;
    tr.bits_per_word = 8;
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        return std::numeric_limits<float>::quiet_NaN();
    }

    uint16_t adc_code = ((rx[1] << 8) | rx[2]) >> 1;
    float resistance = (static_cast<float>(adc_code) * R_REF) / 32768.0f;
    float raw_temp = (resistance - PT100_NOMINAL) / PT100_ALPHA;
    float displayed_temp = raw_temp + temp_offset;
    return calibration_gain * displayed_temp + calibration_bias;
}

bool PT100Sensor::isAvailable() const {
    return spi_fd >= 0;
}

PT100Sensor::~PT100Sensor() {
    if (spi_fd >= 0) close(spi_fd);
}
