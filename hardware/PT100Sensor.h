#ifndef CPC_HARDWARE_PT100SENSOR_H
#define CPC_HARDWARE_PT100SENSOR_H

#include <QString>

#include <cstdint>

class PT100Sensor {
public:
    PT100Sensor(const char* device,
                float offset = 0.0f,
                float gain = 1.0f,
                float bias = 0.0f);
    ~PT100Sensor();

    float read_temperature();
    bool isAvailable() const;
    bool isReady() const;
    QString errorString() const;
    std::uint8_t lastFaultFlags() const;
    bool hasFault() const;
    QString faultString() const;
    static QString faultStringForFlags(std::uint8_t flags);

private:
    bool transfer(std::uint8_t *tx, std::uint8_t *rx, std::uint32_t length,
                  const QString& operation);
    bool writeConfiguration(std::uint8_t configuration, const QString& operation);

    int spi_fd;
    float temp_offset;
    float calibration_gain;
    float calibration_bias;
    QString device_name;
    QString last_error;
    std::uint8_t last_fault_flags;
    const float R_REF = 430.0f;
    const float PT100_NOMINAL = 100.0f;
    const float PT100_ALPHA = 0.385f;
};

#endif // CPC_HARDWARE_PT100SENSOR_H
