#include "PwmOutputs.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <lgpio.h>
#include <unistd.h>

using std::ofstream;
using std::string;
using std::to_string;

bool HardwarePWM::write_sysfs(const string& path, const string& value) {
    errno = 0;
    ofstream fs(path);
    if (!fs.is_open()) {
        setError("无法打开 " + path + ": " + std::strerror(errno));
        return false;
    }

    fs << value;
    fs.flush();
    if (!fs.good()) {
        setError("无法写入 " + path + ": " + std::strerror(errno));
        return false;
    }
    return true;
}

void HardwarePWM::setError(const string& message) {
    error_message = message;
}

HardwarePWM::HardwarePWM(int chip, int channel, int pin, int frequency_hz)
    : pwm_chip(chip),
      pwm_channel(channel),
      gpio_pin(pin),
      pwm_chip_path("/sys/class/pwm/pwmchip" + to_string(pwm_chip) + "/"),
      pwm_path(pwm_chip_path + "pwm" + to_string(pwm_channel) + "/"),
      period_ns(frequency_hz > 0 ? 1000000000 / frequency_hz : 0),
      ready(false),
      exported(false) {
    if (frequency_hz <= 0) {
        setError("PWM 频率必须大于 0");
        return;
    }
    if (access(pwm_chip_path.c_str(), F_OK) != 0) {
        setError("PWM 控制器不存在: " + pwm_chip_path);
        return;
    }
    if (system(("pinctrl set " + to_string(gpio_pin) + " a0").c_str()) != 0) {
        setError("无法把 GPIO" + to_string(gpio_pin) + " 切换到 PWM 功能");
        return;
    }

    if (access(pwm_path.c_str(), F_OK) != 0) {
        if (!write_sysfs(pwm_chip_path + "export", to_string(pwm_channel))) return;
        exported = true;
        for (int attempt = 0; attempt < 20 && access(pwm_path.c_str(), F_OK) != 0; ++attempt) {
            usleep(50000);
        }
    }
    if (access(pwm_path.c_str(), F_OK) != 0) {
        setError("PWM 通道导出失败: " + pwm_path);
        return;
    }

    // udev fixes ownership after the channel appears.  Wait for that rule to
    // finish instead of racing it and reporting a false permission failure.
    const string enable_path = pwm_path + "enable";
    const string period_path = pwm_path + "period";
    const string duty_cycle_path = pwm_path + "duty_cycle";
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (access(enable_path.c_str(), W_OK) == 0 &&
            access(period_path.c_str(), W_OK) == 0 &&
            access(duty_cycle_path.c_str(), W_OK) == 0) {
            break;
        }
        usleep(50000);
    }
    if (access(enable_path.c_str(), W_OK) != 0 ||
        access(period_path.c_str(), W_OK) != 0 ||
        access(duty_cycle_path.c_str(), W_OK) != 0) {
        setError("PWM 通道权限未就绪: " + pwm_path);
        return;
    }

    // A previously interrupted run can leave the channel exported and enabled.
    // Disable it before changing the period, then always start at zero duty.
    if (!write_sysfs(enable_path, "0") ||
        !write_sysfs(period_path, to_string(period_ns)) ||
        !write_sysfs(duty_cycle_path, "0") ||
        !write_sysfs(enable_path, "1")) {
        write_sysfs(duty_cycle_path, "0");
        write_sysfs(enable_path, "0");
        return;
    }
    ready = true;
}

bool HardwarePWM::set_duty_cycle(double percentage) {
    if (!ready) return false;
    if (percentage < 0.0) percentage = 0.0;
    if (percentage > 100.0) percentage = 100.0;
    long duty_ns = static_cast<long>((percentage / 100.0) * period_ns);
    if (!write_sysfs(pwm_path + "duty_cycle", to_string(duty_ns))) {
        ready = false;
        return false;
    }
    return true;
}

bool HardwarePWM::isReady() const {
    return ready;
}

string HardwarePWM::errorString() const {
    return error_message;
}

HardwarePWM::~HardwarePWM() {
    if (access(pwm_path.c_str(), F_OK) == 0) {
        write_sysfs(pwm_path + "duty_cycle", "0");
        write_sysfs(pwm_path + "enable", "0");
    }
    if (exported) write_sysfs(pwm_chip_path + "unexport", to_string(pwm_channel));
    system(("pinctrl set " + to_string(gpio_pin) + " op dl").c_str());
}

LgpioDigitalOutput::LgpioDigitalOutput(int lgpio_handle, int pin)
    : handle(lgpio_handle), gpio(pin), claimed(false) {
    if (handle >= 0) {
        claimed = (lgGpioClaimOutput(handle, 0, gpio, 0) >= 0);
    }
}

bool LgpioDigitalOutput::set(bool on) {
    if (!claimed) return false;
    return lgGpioWrite(handle, gpio, on ? 1 : 0) >= 0;
}

bool LgpioDigitalOutput::isReady() const {
    return claimed;
}

void LgpioDigitalOutput::release() {
    if (claimed) {
        lgGpioWrite(handle, gpio, 0);
        lgGpioFree(handle, gpio);
        claimed = false;
    }
}

LgpioDigitalOutput::~LgpioDigitalOutput() {
    release();
}

LgpioPwmOutput::LgpioPwmOutput(int lgpio_handle, int pin, float frequency_hz)
    : handle(lgpio_handle), gpio(pin), frequency(frequency_hz), claimed(false) {
    if (handle >= 0) {
        claimed = (lgGpioClaimOutput(handle, 0, gpio, 0) >= 0);
    }
}

bool LgpioPwmOutput::set_duty_cycle(double percentage) {
    if (!claimed) return false;
    if (percentage < 0.0) percentage = 0.0;
    if (percentage > 100.0) percentage = 100.0;
    if (percentage == 0.0) {
        lgTxPwm(handle, gpio, 0, 0, 0, 0);
        return lgGpioWrite(handle, gpio, 0) >= 0;
    }
    return lgTxPwm(handle, gpio, frequency, percentage, 0, 0) >= 0;
}

bool LgpioPwmOutput::isReady() const {
    return claimed;
}

void LgpioPwmOutput::release() {
    if (claimed) {
        lgTxPwm(handle, gpio, 0, 0, 0, 0);
        lgGpioWrite(handle, gpio, 0);
        lgGpioFree(handle, gpio);
        claimed = false;
    }
}

LgpioPwmOutput::~LgpioPwmOutput() {
    release();
}
