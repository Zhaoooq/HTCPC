#include "N4IOA01Valve.h"

#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace {

constexpr double kValveMinMilliamp = 4.0;
constexpr double kValveMaxMilliamp = 20.0;
constexpr double kModuleMinMilliamp = 0.5;
constexpr double kModuleMaxMilliamp = 20.0;

struct CurrentCalibrationPoint {
    double moduleMilliamp;
    double measuredMilliamp;
};

// Piecewise-linear calibration measured from the installed N4IOA01 channel.
// moduleMilliamp is the value actually stored in register 0x0000, while
// measuredMilliamp is the corresponding multimeter reading.
const std::array<CurrentCalibrationPoint, 23> kCurrentCalibration = {{
    { 3.96,  4.010}, { 4.76,  4.810}, { 5.55,  5.610},
    { 6.35,  6.410}, { 7.14,  7.210}, { 7.94,  8.010},
    { 8.73,  8.805}, { 9.53,  9.610}, {10.32, 10.405},
    {11.28, 11.370}, {11.91, 12.000}, {12.71, 12.800},
    {13.51, 13.605}, {14.30, 14.395}, {15.10, 15.200},
    {15.89, 15.990}, {16.69, 16.790}, {17.48, 17.575},
    {18.28, 18.370}, {18.91, 18.995}, {19.39, 19.465},
    {19.71, 19.780}, {19.87, 19.945}
}};

constexpr std::uint16_t kCurrentRegister = 0x0000;
constexpr int kResponseTimeoutMs = 1000;

double targetToModuleCurrent(double targetMilliamp) {
    std::size_t upper = 1;
    while (upper + 1 < kCurrentCalibration.size() &&
           targetMilliamp > kCurrentCalibration[upper].measuredMilliamp) {
        ++upper;
    }
    const CurrentCalibrationPoint& low = kCurrentCalibration[upper - 1];
    const CurrentCalibrationPoint& high = kCurrentCalibration[upper];
    const double fraction = (targetMilliamp - low.measuredMilliamp) /
                            (high.measuredMilliamp - low.measuredMilliamp);
    const double command = low.moduleMilliamp +
                           fraction * (high.moduleMilliamp - low.moduleMilliamp);
    return std::max(kModuleMinMilliamp,
                    std::min(kModuleMaxMilliamp, command));
}

double moduleToCalibratedCurrent(double moduleMilliamp) {
    std::size_t upper = 1;
    while (upper + 1 < kCurrentCalibration.size() &&
           moduleMilliamp > kCurrentCalibration[upper].moduleMilliamp) {
        ++upper;
    }
    const CurrentCalibrationPoint& low = kCurrentCalibration[upper - 1];
    const CurrentCalibrationPoint& high = kCurrentCalibration[upper];
    const double fraction = (moduleMilliamp - low.moduleMilliamp) /
                            (high.moduleMilliamp - low.moduleMilliamp);
    return low.measuredMilliamp +
           fraction * (high.measuredMilliamp - low.measuredMilliamp);
}

speed_t baudToTermios(int baudRate) {
    switch (baudRate) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    default: return 0;
    }
}

QString systemError(const QString& operation) {
    return QString("%1: %2").arg(operation, QString::fromLocal8Bit(std::strerror(errno)));
}

} // namespace

N4IOA01Valve::N4IOA01Valve(const QString& port, int baudRate, std::uint8_t deviceAddress)
    : port_(port),
      baudRate_(baudRate),
      address_(deviceAddress),
      serialFd_(-1) {
}

N4IOA01Valve::~N4IOA01Valve() {
    close();
}

bool N4IOA01Valve::setOpeningPercent(double openingPercent,
                                     double *actualCurrentMilliamp,
                                     QString *error) {
    if (!std::isfinite(openingPercent)) {
        setError(error, "阀门开度必须是有限数值");
        return false;
    }
    const double current = openingToCurrent(openingPercent);
    if (!setCurrentMilliamp(current, error)) return false;
    if (actualCurrentMilliamp) *actualCurrentMilliamp = current;
    return true;
}

bool N4IOA01Valve::setCurrentMilliamp(double currentMilliamp, QString *error) {
    if (!std::isfinite(currentMilliamp)) {
        setError(error, "输出电流必须是有限数值");
        return false;
    }
    const double limited = std::max(kValveMinMilliamp,
                                    std::min(kValveMaxMilliamp, currentMilliamp));
    const double moduleCurrent = targetToModuleCurrent(limited);
    const std::uint16_t raw =
        static_cast<std::uint16_t>(std::lround(moduleCurrent * 100.0));
    return writeRegister(kCurrentRegister, raw, error);
}

bool N4IOA01Valve::readCurrentMilliamp(double *currentMilliamp, QString *error) {
    if (!currentMilliamp) {
        setError(error, "读取输出电流时未提供结果存储地址");
        return false;
    }

    std::uint16_t raw = 0;
    if (!readRegister(kCurrentRegister, &raw, error)) return false;
    const double moduleCurrent = static_cast<double>(raw) * 0.01;
    *currentMilliamp = moduleToCalibratedCurrent(moduleCurrent);
    return true;
}

bool N4IOA01Valve::safeClose(QString *error) {
    return setCurrentMilliamp(kValveMinMilliamp, error);
}

void N4IOA01Valve::close() {
    if (serialFd_ >= 0) {
        ::close(serialFd_);
        serialFd_ = -1;
    }
}

QString N4IOA01Valve::portName() const {
    return port_;
}

int N4IOA01Valve::baudRate() const {
    return baudRate_;
}

std::uint8_t N4IOA01Valve::deviceAddress() const {
    return address_;
}

double N4IOA01Valve::openingToCurrent(double openingPercent) {
    const double limited = std::max(0.0, std::min(100.0, openingPercent));
    return kValveMinMilliamp +
           (kValveMaxMilliamp - kValveMinMilliamp) * limited / 100.0;
}

double N4IOA01Valve::currentToOpening(double currentMilliamp) {
    const double limited = std::max(kValveMinMilliamp,
                                    std::min(kValveMaxMilliamp, currentMilliamp));
    return (limited - kValveMinMilliamp) * 100.0 /
           (kValveMaxMilliamp - kValveMinMilliamp);
}

bool N4IOA01Valve::ensureOpen(QString *error) {
    if (serialFd_ >= 0) return true;

    const speed_t speed = baudToTermios(baudRate_);
    if (speed == 0) {
        setError(error, QString("不支持的串口波特率: %1").arg(baudRate_));
        return false;
    }

    const QByteArray nativePort = port_.toLocal8Bit();
    serialFd_ = ::open(nativePort.constData(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (serialFd_ < 0) {
        setError(error, systemError(QString("无法打开串口 %1").arg(port_)));
        return false;
    }

    termios options;
    if (tcgetattr(serialFd_, &options) != 0) {
        setError(error, systemError(QString("无法读取串口 %1 配置").arg(port_)));
        close();
        return false;
    }

    cfmakeraw(&options);
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    options.c_cflag = (options.c_cflag & ~CSIZE) | CS8;
    options.c_cflag |= CLOCAL | CREAD;
    options.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 0;

    if (tcsetattr(serialFd_, TCSANOW, &options) != 0) {
        setError(error, systemError(QString("无法配置串口 %1").arg(port_)));
        close();
        return false;
    }

    tcflush(serialFd_, TCIOFLUSH);
    usleep(200000);
    return true;
}

bool N4IOA01Valve::transact(const QByteArray& request,
                            int expectedResponseLength,
                            QByteArray *response,
                            QString *error) {
    if (!response) {
        setError(error, "通信响应存储地址无效");
        return false;
    }
    if (!ensureOpen(error)) return false;

    tcflush(serialFd_, TCIOFLUSH);
    usleep(50000);

    int written = 0;
    while (written < request.size()) {
        const ssize_t result = ::write(serialFd_, request.constData() + written,
                                       static_cast<std::size_t>(request.size() - written));
        if (result < 0) {
            if (errno == EINTR) continue;
            setError(error, systemError("串口写入失败"));
            close();
            return false;
        }
        if (result == 0) {
            setError(error, "串口写入未发送任何数据");
            close();
            return false;
        }
        written += static_cast<int>(result);
    }
    if (tcdrain(serialFd_) != 0) {
        setError(error, systemError("等待串口发送完成失败"));
        close();
        return false;
    }

    response->clear();
    int requiredLength = expectedResponseLength;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kResponseTimeoutMs);

    while (response->size() < requiredLength) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        const int remainingMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());

        pollfd descriptor;
        descriptor.fd = serialFd_;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        const int pollResult = ::poll(&descriptor, 1, std::max(1, remainingMs));
        if (pollResult < 0) {
            if (errno == EINTR) continue;
            setError(error, systemError("等待串口响应失败"));
            close();
            return false;
        }
        if (pollResult == 0) break;
        if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            setError(error, "串口在等待响应时发生错误或断开");
            close();
            return false;
        }

        char buffer[32];
        const ssize_t count = ::read(serialFd_, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) continue;
            setError(error, systemError("读取串口响应失败"));
            close();
            return false;
        }
        if (count > 0) response->append(buffer, static_cast<int>(count));
        if (response->size() >= 2 &&
            (static_cast<unsigned char>(response->at(1)) & 0x80U)) {
            requiredLength = 5;
        }
    }

    if (response->isEmpty()) {
        setError(error, QString("模块无响应，TX=%1").arg(frameToHex(request)));
        return false;
    }
    if (response->size() < requiredLength) {
        setError(error, QString("响应长度不足，期望 %1 字节，收到 %2 字节，RX=%3")
                            .arg(requiredLength)
                            .arg(response->size())
                            .arg(frameToHex(*response)));
        return false;
    }
    if (response->size() > requiredLength) response->truncate(requiredLength);

    const QByteArray payload = response->left(response->size() - 2);
    const std::uint16_t receivedCrc =
        static_cast<std::uint8_t>(response->at(response->size() - 2)) |
        (static_cast<std::uint16_t>(static_cast<std::uint8_t>(response->at(response->size() - 1))) << 8);
    if (crc16(payload) != receivedCrc) {
        setError(error, QString("响应 CRC 错误，RX=%1").arg(frameToHex(*response)));
        return false;
    }
    if (static_cast<std::uint8_t>(response->at(0)) != address_) {
        setError(error, QString("响应模块地址不匹配，期望 0x%1，收到 0x%2")
                            .arg(address_, 2, 16, QLatin1Char('0'))
                            .arg(static_cast<std::uint8_t>(response->at(0)), 2, 16, QLatin1Char('0'))
                            .toUpper());
        return false;
    }
    if (static_cast<std::uint8_t>(response->at(1)) & 0x80U) {
        setError(error, QString("Modbus 异常响应，功能码 0x%1，异常码 0x%2")
                            .arg(static_cast<std::uint8_t>(response->at(1)), 2, 16, QLatin1Char('0'))
                            .arg(static_cast<std::uint8_t>(response->at(2)), 2, 16, QLatin1Char('0'))
                            .toUpper());
        return false;
    }
    return true;
}

bool N4IOA01Valve::readRegister(std::uint16_t registerAddress,
                                std::uint16_t *value,
                                QString *error) {
    QByteArray body;
    body.append(static_cast<char>(address_));
    body.append(static_cast<char>(0x03));
    body.append(static_cast<char>((registerAddress >> 8) & 0xFF));
    body.append(static_cast<char>(registerAddress & 0xFF));
    body.append(static_cast<char>(0x00));
    body.append(static_cast<char>(0x01));

    QByteArray response;
    if (!transact(withCrc(body), 7, &response, error)) return false;
    if (static_cast<std::uint8_t>(response.at(1)) != 0x03 ||
        static_cast<std::uint8_t>(response.at(2)) != 0x02) {
        setError(error, QString("读取寄存器响应格式错误，RX=%1").arg(frameToHex(response)));
        return false;
    }

    *value = (static_cast<std::uint16_t>(static_cast<std::uint8_t>(response.at(3))) << 8) |
             static_cast<std::uint8_t>(response.at(4));
    return true;
}

bool N4IOA01Valve::writeRegister(std::uint16_t registerAddress,
                                 std::uint16_t value,
                                 QString *error) {
    QByteArray body;
    body.append(static_cast<char>(address_));
    body.append(static_cast<char>(0x06));
    body.append(static_cast<char>((registerAddress >> 8) & 0xFF));
    body.append(static_cast<char>(registerAddress & 0xFF));
    body.append(static_cast<char>((value >> 8) & 0xFF));
    body.append(static_cast<char>(value & 0xFF));
    const QByteArray request = withCrc(body);

    QByteArray response;
    if (!transact(request, 8, &response, error)) return false;
    if (response != request) {
        setError(error, QString("写寄存器回显不一致，TX=%1，RX=%2")
                            .arg(frameToHex(request), frameToHex(response)));
        return false;
    }
    return true;
}

std::uint16_t N4IOA01Valve::crc16(const QByteArray& data) {
    std::uint16_t crc = 0xFFFF;
    for (char byte : data) {
        crc ^= static_cast<std::uint8_t>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x0001U) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001U)
                                  : static_cast<std::uint16_t>(crc >> 1);
        }
    }
    return crc;
}

QByteArray N4IOA01Valve::withCrc(const QByteArray& data) {
    QByteArray frame = data;
    const std::uint16_t crc = crc16(data);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

QString N4IOA01Valve::frameToHex(const QByteArray& frame) {
    return QString::fromLatin1(frame.toHex(' ').toUpper());
}

void N4IOA01Valve::setError(QString *target, const QString& message) {
    if (target) *target = message;
}
