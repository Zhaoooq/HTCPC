#ifndef CPC_HARDWARE_N4IOA01VALVE_H
#define CPC_HARDWARE_N4IOA01VALVE_H

#include <QByteArray>
#include <QString>

#include <cstdint>

class N4IOA01Valve {
public:
    explicit N4IOA01Valve(const QString& port = "/dev/ttyAMA0",
                          int baudRate = 9600,
                          std::uint8_t deviceAddress = 0x01);
    ~N4IOA01Valve();

    N4IOA01Valve(const N4IOA01Valve&) = delete;
    N4IOA01Valve& operator=(const N4IOA01Valve&) = delete;

    bool setOpeningPercent(double openingPercent,
                           double *actualCurrentMilliamp = nullptr,
                           QString *error = nullptr);
    bool setCurrentMilliamp(double currentMilliamp, QString *error = nullptr);
    bool readCurrentMilliamp(double *currentMilliamp, QString *error = nullptr);
    bool safeClose(QString *error = nullptr);
    void close();

    QString portName() const;
    int baudRate() const;
    std::uint8_t deviceAddress() const;

    static double openingToCurrent(double openingPercent);
    static double currentToOpening(double currentMilliamp);

private:
    bool ensureOpen(QString *error);
    bool transact(const QByteArray& request,
                  int expectedResponseLength,
                  QByteArray *response,
                  QString *error);
    bool readRegister(std::uint16_t registerAddress,
                      std::uint16_t *value,
                      QString *error);
    bool writeRegister(std::uint16_t registerAddress,
                       std::uint16_t value,
                       QString *error);

    static std::uint16_t crc16(const QByteArray& data);
    static QByteArray withCrc(const QByteArray& data);
    static QString frameToHex(const QByteArray& frame);
    static void setError(QString *target, const QString& message);

    QString port_;
    int baudRate_;
    std::uint8_t address_;
    int serialFd_;
};

#endif // CPC_HARDWARE_N4IOA01VALVE_H
