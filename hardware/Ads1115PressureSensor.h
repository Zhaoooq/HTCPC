#ifndef CPC_HARDWARE_ADS1115PRESSURESENSOR_H
#define CPC_HARDWARE_ADS1115PRESSURESENSOR_H

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <array>
#include <cstdint>
#include <vector>

class Ads1115PressureSensor : public QObject {
    Q_OBJECT

public:
    explicit Ads1115PressureSensor(const QString& i2cDevice = "/dev/i2c-1",
                                   std::uint8_t i2cAddress = 0x48,
                                   QObject *parent = nullptr);
    ~Ads1115PressureSensor();

    bool start();
    void stop();
    bool isReady() const;
    bool isZeroing() const;
    QString errorString() const;

public slots:
    void startZeroCalibration();

signals:
    void pressureUpdated(int channel,
                         double pressurePa,
                         double adcVoltage,
                         double sensorVoltage,
                         double fullScalePercent,
                         QString warning);
    void zeroCalibrationProgress(int percent);
    void zeroCalibrationFinished(int channel,
                                 double zeroVoltage,
                                 double uncorrectedPressurePa);
    void statusChanged(QString status);
    void errorOccurred(QString error);

private slots:
    void sampleOnce();

private:
    struct ChannelState {
        double fullScalePa = 0.0;
        double zeroVoltage = 0.0;
        double filteredPressurePa = 0.0;
        bool hasFilteredPressure = false;
        qint64 lastDisplayMs = 0;
        std::vector<double> zeroSamples;
        std::vector<double> recentAdcVoltages;
    };

    bool beginConversion(int channel);
    bool readConversionReady(bool *ready);
    bool readAdcVoltage(double *voltage);
    void processVoltage(int channel, double adcVoltage);
    void handleCommunicationFailure();
    void setError(const QString& operation);
    static double median(std::vector<double> values);
    static double sensorVoltageFromAdc(double adcVoltage);

    QString i2cDevice_;
    std::uint8_t i2cAddress_;
    int i2cFd_;
    bool ready_;
    bool zeroing_;
    bool conversionPending_;
    int currentChannel_;
    int consecutiveReadErrors_;
    int lastZeroProgress_;
    QString lastError_;
    QTimer sampleTimer_;
    QElapsedTimer conversionTimer_;
    QElapsedTimer zeroTimer_;
    QElapsedTimer measurementTimer_;
    std::array<ChannelState, 3> channels_;
};

#endif // CPC_HARDWARE_ADS1115PRESSURESENSOR_H
