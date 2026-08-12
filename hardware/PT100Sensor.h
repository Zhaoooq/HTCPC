#ifndef CPC_HARDWARE_PT100SENSOR_H
#define CPC_HARDWARE_PT100SENSOR_H

class PT100Sensor {
public:
    PT100Sensor(const char* device,
                float offset = 0.0f,
                float gain = 1.0f,
                float bias = 0.0f);
    ~PT100Sensor();

    float read_temperature();
    bool isAvailable() const;

private:
    int spi_fd;
    float temp_offset;
    float calibration_gain;
    float calibration_bias;
    const float R_REF = 430.0f;
    const float PT100_NOMINAL = 100.0f;
    const float PT100_ALPHA = 0.385f;
};

#endif // CPC_HARDWARE_PT100SENSOR_H
