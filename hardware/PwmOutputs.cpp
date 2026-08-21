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

bool HardwarePWM::safeDisable() {
    bool success = true;
    if (access(pwm_path.c_str(), F_OK) == 0) {
        if (!write_sysfs(pwm_path + "duty_cycle", "0")) success = false;
        if (!write_sysfs(pwm_path + "enable", "0")) success = false;
    } else {
        setError("PWM 通道不存在: " + pwm_path);
        success = false;
    }

    if (system(("pinctrl set " + to_string(gpio_pin) + " op dl").c_str()) != 0) {
        setError("GPIO" + to_string(gpio_pin) + " 无法切换为低电平安全状态");
        success = false;
    }
    ready = false;
    if (success) error_message.clear();
    return success;
}

bool HardwarePWM::isReady() const {
    return ready;
}

string HardwarePWM::errorString() const {
    return error_message;
}

HardwarePWM::~HardwarePWM() {
    safeDisable();
    if (exported) write_sysfs(pwm_chip_path + "unexport", to_string(pwm_channel));
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
    if (frequency <= 0.0f) {
        error_message = "PWM 频率必须大于 0";
        return;
    }
    if (handle < 0) {
        error_message = "GPIO 控制器不可用";
        return;
    }

    const int result = lgGpioClaimOutput(handle, 0, gpio, 0);
    claimed = (result >= 0);
    if (!claimed) {
        error_message = "无法申请 GPIO" + to_string(gpio) +
            " PWM 输出，lgpio 错误码 " + to_string(result);
    }
}

bool LgpioPwmOutput::set_duty_cycle(double percentage) {
    if (!claimed) {
        if (error_message.empty()) error_message = "GPIO PWM 输出未就绪";
        return false;
    }
    if (percentage < 0.0) percentage = 0.0;
    if (percentage > 100.0) percentage = 100.0;
    if (percentage == 0.0) {
        // Keep a valid PWM period while setting the duty to zero.  Passing an
        // all-zero waveform is rejected as LG_BAD_PWM_MICROS by lgpio 0.2.2.
        const int pwm_result = lgTxPwm(handle, gpio, frequency, 0.0, 0, 0);
        const int write_result = lgGpioWrite(handle, gpio, 0);
        if (pwm_result < 0 || write_result < 0) {
            error_message = "GPIO" + to_string(gpio) +
                " PWM 关闭失败，lgpio 错误码 " +
                to_string(pwm_result < 0 ? pwm_result : write_result);
            return false;
        }
        error_message.clear();
        return true;
    }
    const int result = lgTxPwm(handle, gpio, frequency, percentage, 0, 0);
    if (result < 0) {
        error_message = "GPIO" + to_string(gpio) +
            " PWM 写入失败，lgpio 错误码 " + to_string(result);
        return false;
    }
    error_message.clear();
    return true;
}

bool LgpioPwmOutput::isReady() const {
    return claimed;
}

string LgpioPwmOutput::errorString() const {
    return error_message;
}

void LgpioPwmOutput::release() {
    if (claimed) {
        lgTxPwm(handle, gpio, frequency, 0.0, 0, 0);
        lgGpioWrite(handle, gpio, 0);
        lgGpioFree(handle, gpio);
        claimed = false;
    }
}

LgpioPwmOutput::~LgpioPwmOutput() {
    release();
}
