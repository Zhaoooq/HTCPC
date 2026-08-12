#ifndef CPC_HARDWARE_PWMOUTPUTS_H
#define CPC_HARDWARE_PWMOUTPUTS_H

#include <string>

class HardwarePWM {
public:
    HardwarePWM(int chip, int channel, int pin, int frequency_hz = 5);
    ~HardwarePWM();

    bool set_duty_cycle(double percentage);
    bool isReady() const;
    std::string errorString() const;

private:
    int pwm_chip;
    int pwm_channel;
    int gpio_pin;
    std::string pwm_chip_path;
    std::string pwm_path;
    long period_ns;
    bool ready;
    bool exported;
    std::string error_message;

    bool write_sysfs(const std::string& path, const std::string& value);
    void setError(const std::string& message);
};

class LgpioDigitalOutput {
public:
    LgpioDigitalOutput(int lgpio_handle, int pin);
    ~LgpioDigitalOutput();

    bool set(bool on);
    bool isReady() const;
    void release();

private:
    int handle;
    int gpio;
    bool claimed;
};

class LgpioPwmOutput {
public:
    LgpioPwmOutput(int lgpio_handle, int pin, float frequency_hz);
    ~LgpioPwmOutput();

    bool set_duty_cycle(double percentage);
    bool isReady() const;
    void release();

private:
    int handle;
    int gpio;
    float frequency;
    bool claimed;
};

#endif // CPC_HARDWARE_PWMOUTPUTS_H
