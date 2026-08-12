#ifndef LIQUIDCONTROLSYSTEM_H
#define LIQUIDCONTROLSYSTEM_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <lgpio.h>

class LiquidControlSystem : public QObject {
    Q_OBJECT
public:
    // 默认引脚：液位传感器27，进液电磁阀22，排液电磁阀25
    explicit LiquidControlSystem(int lgpio_handle, 
                                 int sensor_pin = 27, 
                                 int inlet_pin = 22, 
                                 int outlet_pin = 25,
                                 QObject *parent = nullptr);
    ~LiquidControlSystem();

    bool isReady() const;
    QString errorString() const;

    void startMonitoring(); // 开启自动液位监控
    void stopMonitoring();  // 停止监控

public slots:
    void startManualDrain(); // 开始手动排废液 (绑定UI按钮按下)
    void stopManualDrain();  // 停止手动排废液 (绑定UI按钮松开)

signals:
    // 用于向主界面发送状态更新和报警信息的信号
    void statusMessage(QString msg);
    void alertMessage(QString alert);

private slots:
    void checkLiquidLevel(); // 定时检查传感器
    void onRefillTimeout();  // 补液超时处理

private:
    int h; // lgpio 句柄
    int p_sensor, p_inlet, p_outlet;
    
    bool isRefilling;
    bool isDraining;
    bool hasLastLevelState;
    bool lastHasLiquid;
    bool refillFaultLatched;
    bool sensorClaimed;
    bool inletClaimed;
    bool outletClaimed;
    bool hardwareReady;
    int consecutiveLowSamples;
    QString hardwareError;

    QTimer *monitorTimer;
    QTimer *refillTimeoutTimer;

    static const int LOW_LEVEL_CONFIRM_SAMPLES = 2;

    void startRefill();
    void stopRefill(const QString& reason);
};

#endif // LIQUIDCONTROLSYSTEM_H
