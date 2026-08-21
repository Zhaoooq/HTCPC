#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QMetaType>
#include <QMouseEvent>
#include <QPushButton>
#include <QProcess>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QStringList>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVector>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <csignal>
#include <cmath>
#include <functional>
#include <limits>

#include <lgpio.h>

#include "LiquidControlSystem.h"
#include "algorithms/OpcCounter.h"
#include "control/PressureValveController.h"
#include "control/TemperaturePid.h"
#include "daq_worker.h"
#include "hardware/Ads1115PressureSensor.h"
#include "hardware/N4IOA01Valve.h"
#include "hardware/PT100Sensor.h"
#include "hardware/PinMap.h"
#include "hardware/PwmOutputs.h"
#include "qcustomplot.h"
#include "state/AppRuntimeState.h"
#include "ui/Formatters.h"
#include "ui/MainWindowUi.h"

namespace {

enum class StartupPhase {
    SelfCheck,
    WarmingUp,
    Ready
};

constexpr qint64 WARMUP_DURATION_MS = 10 * 60 * 1000;

struct PidTunings {
    double kp;
    double ki;
    double kd;
};

constexpr PidTunings COND_PID_RECOMMENDED = {20.0, 1.5, 40.0};
constexpr PidTunings SAT_PID_RECOMMENDED = {8.0, 0.10, 90.0};
constexpr PidTunings OPC_PID_RECOMMENDED = {10.0, 0.20, 60.0};

volatile std::sig_atomic_t pendingTerminationSignal = 0;

void handleTerminationSignal(int signalNumber) {
    pendingTerminationSignal = signalNumber;
}

void appendSafetyShutdownLog(const QString& trigger, const QStringList& results) {
    const QString logDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (logDirectory.isEmpty() || !QDir().mkpath(logDirectory)) {
        qWarning() << "无法创建安全停机日志目录" << logDirectory;
        return;
    }

    QFile logFile(logDirectory + "/safety_shutdown.log");
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "无法写入安全停机日志" << logFile.fileName() << logFile.errorString();
        return;
    }

    QTextStream stream(&logFile);
    stream << QDateTime::currentDateTime().toString(Qt::ISODate)
           << "  触发原因: " << trigger << "\n";
    for (const QString& result : results) stream << "  " << result << "\n";
    stream << "\n";
    stream.flush();
}

QString formatCountdown(int remainingSeconds) {
    const int minutes = remainingSeconds / 60;
    const int seconds = remainingSeconds % 60;
    return QString("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    qRegisterMetaType<QVector<double>>("QVector<double>");
    std::signal(SIGINT, handleTerminationSignal);
    std::signal(SIGTERM, handleTerminationSignal);
    std::signal(SIGHUP, handleTerminationSignal);
    std::signal(SIGQUIT, handleTerminationSignal);

    int gpio_handle = lgGpiochipOpen(PinMap::GPIO_CHIP);

    // 水浴锅标定：校正后温度 = gain * 当前显示温度 + bias。
    PT100Sensor cond_sensor("/dev/spidev1.0", -5.0f, 0.94866653f, -2.88653625f);
    PT100Sensor sat_sensor("/dev/spidev1.1", -8.0f, 0.95384681f, 0.17535515f);
    PT100Sensor opc_sensor("/dev/spidev1.2", 0.0f, 0.99875547f, -9.19239152f);
    HardwarePWM peltier_cond(PinMap::HARDWARE_PWM_CHIP, 0, PinMap::PIN_PELTIER_COND, 5);
    HardwarePWM heater_sat(PinMap::HARDWARE_PWM_CHIP, 1, PinMap::PIN_HEATER_SAT, 5);
    LgpioPwmOutput vacuum_pump(gpio_handle, PinMap::PIN_VACUUM_PUMP, 200.0f);
    LgpioPwmOutput opc_heater(gpio_handle, PinMap::PIN_OPC_HEATER_PWM, 5.0f);
    LgpioDigitalOutput bypass_valve(gpio_handle, PinMap::PIN_BYPASS_VALVE);
    N4IOA01Valve proportional_valve("/dev/ttyAMA0", 9600, 0x01);

    bool gpioReady = (gpio_handle >= 0);
    bool bypassValveReady = gpioReady && bypass_valve.isReady();
    bool pumpReady = gpioReady && vacuum_pump.isReady();
    bool opcHeaterReady = gpioReady && opc_heater.isReady();
    bool condPwmReady = peltier_cond.isReady();
    bool satPwmReady = heater_sat.isReady();

    HybridCoolingPID cond_pid;
    PredictiveHeatingPID sat_pid;
    PredictiveHeatingPID opc_pid;
    QSettings pidSettings(QSettings::IniFormat,
                          QSettings::UserScope,
                          QStringLiteral("CPC"),
                          QStringLiteral("CPC_1"));
    auto loadPidTunings = [&](const QString& keyPrefix, const PidTunings& defaults) {
        bool kpOk = false;
        bool kiOk = false;
        bool kdOk = false;
        const double kp = pidSettings.value(keyPrefix + "/kp", defaults.kp).toDouble(&kpOk);
        const double ki = pidSettings.value(keyPrefix + "/ki", defaults.ki).toDouble(&kiOk);
        const double kd = pidSettings.value(keyPrefix + "/kd", defaults.kd).toDouble(&kdOk);
        if (!kpOk || !kiOk || !kdOk || !std::isfinite(kp) || !std::isfinite(ki) ||
            !std::isfinite(kd) || kp < 0.0 || kp > 100.0 ||
            ki < 0.0 || ki > 20.0 || kd < 0.0 || kd > 300.0) {
            return defaults;
        }
        return PidTunings{kp, ki, kd};
    };
    const PidTunings condStored = loadPidTunings("temperature/cond", COND_PID_RECOMMENDED);
    const PidTunings satStored = loadPidTunings("temperature/sat", SAT_PID_RECOMMENDED);
    const PidTunings opcStored = loadPidTunings("temperature/opc", OPC_PID_RECOMMENDED);
    cond_pid.setTunings(condStored.kp, condStored.ki, condStored.kd);
    sat_pid.setTunings(satStored.kp, satStored.ki, satStored.kd);
    // Saturation heaters have significant residual heat. Start braking earlier
    // and keep the near-target output below the former 50% ceiling.
    sat_pid.prediction_seconds = 20.0;
    sat_pid.full_power_error = 6.0;
    sat_pid.approach_max_output = 40.0;
    opc_pid.setTunings(opcStored.kp, opcStored.ki, opcStored.kd);
    ActuatorState actuatorState;
    bool &is_cond_running = actuatorState.condRunning;
    bool &is_sat_running = actuatorState.satRunning;
    bool &is_opc_heater_running = actuatorState.opcHeaterRunning;
    bool &is_pump_running = actuatorState.pumpRunning;
    bool &is_bypass_valve_open = actuatorState.bypassValveOpen;
    double &pump_current_power = actuatorState.pumpCurrentPower;

    OpcParams opcParams;
    constexpr double BYPASS_LOW_FLOW_ML_MIN = 300.0;
    constexpr double BYPASS_HIGH_FLOW_ML_MIN = 1500.0;
    double currentSampleFlowMlMin = BYPASS_LOW_FLOW_ML_MIN;

    QMainWindow window;
    window.setWindowFlag(Qt::FramelessWindowHint);
    MainWindowUi ui = buildMainWindow(app, window, opcParams);
    DaqWorker *daqWorker = new DaqWorker();
    bool shutdownRequested = false;
    QString shutdownTrigger = QStringLiteral("正常退出");
    StartupPhase startupPhase = StartupPhase::SelfCheck;
    QElapsedTimer warmupElapsed;
    int warmupRemainingSeconds = static_cast<int>(WARMUP_DURATION_MS / 1000);
    bool liquidLevelNormal = false;
    bool pressureZeroing = true;
    QString startupBlockReason;
    std::function<void()> updateAcqUi;
    std::function<void()> refreshPumpControls = []() {};
    std::function<void()> refreshTemperatureControls = []() {};
    std::function<void()> evaluateStartup = []() {};
    std::function<bool(QString *)> startPumpAtFullPower;
    std::function<void()> stopPumpSafely = []() {};

    QObject::connect(ui.btnShutdown, &QToolButton::clicked, [&]() {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            &window,
            "关闭设备",
            "确定要关闭树莓派吗？\n程序会先停止采集并安全关闭全部执行器。",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (answer != QMessageBox::Yes) return;

        shutdownRequested = true;
        shutdownTrigger = QStringLiteral("界面关机按钮");
        ui.btnShutdown->setEnabled(false);
        ui.lblStatus->setText("状态: 正在安全关闭设备...");
        daqWorker->stopDaq();
        app.quit();
    });

    AcquisitionState acquisitionState;
    bool &is_acquiring = acquisitionState.acquiring;
    bool &hasLatestOpcFrame = acquisitionState.hasLatestOpcFrame;
    bool &hasLatestParticleConcentration = acquisitionState.hasLatestParticleConcentration;
    bool &latestParticleConcentrationValid = acquisitionState.latestParticleConcentrationValid;
    bool &particlePlotFollowLatest = acquisitionState.particlePlotFollowLatest;
    bool &particlePlotAutoY = acquisitionState.particlePlotAutoY;
    double &latestParticleConcentration = acquisitionState.latestParticleConcentration;
    double &smoothedParticleConcentration = acquisitionState.smoothedParticleConcentration;
    double &latestParticleConcentrationTime = acquisitionState.latestParticleConcentrationTime;

    QVector<double> rawTimeBuffer;
    QVector<double> rawVoltageBuffer;
    QVector<double> opcDisplayTimeBuffer;
    QVector<double> opcDisplayVoltageBuffer;
    constexpr double PARTICLE_CONCENTRATION_SMOOTHING_ALPHA = 0.35;
    constexpr int MAX_RAW_BUFFER_SAMPLES = 2000000; // 约 10 秒 @ 200 kSPS，避免长时间运行撑爆内存。
    constexpr int RAW_TRIM_MARGIN_SAMPLES = 200000; // 批量裁剪，避免每帧搬移百万级 QVector。
    constexpr double OPC_DISPLAY_WINDOW_SECONDS = 0.05;
    constexpr int OPC_DISPLAY_POINTS_PER_CHUNK = 1000;

    updateAcqUi = [&]() {
        const bool workerRunning = daqWorker->isRunning();
        const bool isStopping = workerRunning && !is_acquiring;
        const bool acquisitionUnlocked = startupPhase == StartupPhase::Ready;
        ui.btnAcqStart->setEnabled(
            acquisitionUnlocked && !pressureZeroing &&
            !is_acquiring && !workerRunning && pumpReady);
        ui.btnAcqStop->setEnabled(is_acquiring);
        ui.btnSaveRaw->setEnabled(!rawTimeBuffer.isEmpty());
        if (startupPhase == StartupPhase::SelfCheck) {
            if (startupBlockReason.isEmpty()) {
                ui.lblCaptureState->setText("采集: 快速自检");
                ui.lblStatus->setText("状态: 正在确认液位和气泵安全状态");
            } else {
                ui.lblCaptureState->setText("采集: 启动受阻");
                ui.lblStatus->setText(QString("状态: 启动受阻：%1").arg(startupBlockReason));
            }
        } else if (startupPhase == StartupPhase::WarmingUp) {
            ui.lblCaptureState->setText("采集: 热机锁定");
            ui.lblStatus->setText("状态: 三段温控热机中，气泵保持关闭");
        } else {
            ui.lblCaptureState->setText(
                is_acquiring ? "采集: 运行中" : (isStopping ? "采集: 正在停止" : "采集: 已停止"));
            ui.lblStatus->setText(
                is_acquiring ? "状态: 正在采集 OPC 原始信号（气泵 100%）"
                             : (isStopping ? "状态: 正在停止数据采集..." : "状态: 热机完成，可开始采集"));
        }
    };

    QObject::connect(daqWorker, &QThread::finished, &window, [&]() {
        is_acquiring = false;
        updateAcqUi();
    });

    QObject::connect(daqWorker, &DaqWorker::dataReady, ui.opcPlot, [&](QVector<double> time, QVector<double> voltage) {
        if (!is_acquiring || time.isEmpty()) return;

        rawTimeBuffer += time;
        rawVoltageBuffer += voltage;
        if (!ui.btnSaveRaw->isEnabled()) ui.btnSaveRaw->setEnabled(true);
        if (rawTimeBuffer.size() > MAX_RAW_BUFFER_SAMPLES + RAW_TRIM_MARGIN_SAMPLES) {
            int excess = rawTimeBuffer.size() - MAX_RAW_BUFFER_SAMPLES;
            rawTimeBuffer.remove(0, excess);
            rawVoltageBuffer.remove(0, excess);
        }

        int displayStride = qMax(1, time.size() / OPC_DISPLAY_POINTS_PER_CHUNK);
        for (int i = 0; i < time.size() && i < voltage.size(); i += displayStride) {
            opcDisplayTimeBuffer.append(time.at(i));
            opcDisplayVoltageBuffer.append(voltage.at(i));
        }
        double displayCutoff = time.last() - OPC_DISPLAY_WINDOW_SECONDS;
        int firstDisplayPoint = 0;
        while (firstDisplayPoint < opcDisplayTimeBuffer.size() &&
               opcDisplayTimeBuffer.at(firstDisplayPoint) < displayCutoff) {
            ++firstDisplayPoint;
        }
        if (firstDisplayPoint > 0) {
            opcDisplayTimeBuffer.remove(0, firstDisplayPoint);
            opcDisplayVoltageBuffer.remove(0, firstDisplayPoint);
        }

        OpcCountResult opcResult = analyzeOpcPulseSignal(time, voltage, opcParams);
        hasLatestOpcFrame = true;

        // 按操作员手动选择的旁路模式，使用 300/1500 ml/min 计算颗粒浓度。
        double chunkDurationSeconds = estimateChunkDurationSeconds(time);
        bool hasValidSampleFlow =
            std::isfinite(currentSampleFlowMlMin) && currentSampleFlowMlMin > 0.0;
        if (hasValidSampleFlow && chunkDurationSeconds > 0.0) {
            double chunkVolumeMl = currentSampleFlowMlMin * chunkDurationSeconds / 60.0;
            if (chunkVolumeMl > 0.0 && std::isfinite(chunkVolumeMl)) {
                double chunkParticleConcentration =
                    static_cast<double>(opcResult.totalCount) / chunkVolumeMl;
                if (std::isfinite(smoothedParticleConcentration)) {
                    smoothedParticleConcentration =
                        PARTICLE_CONCENTRATION_SMOOTHING_ALPHA * chunkParticleConcentration +
                        (1.0 - PARTICLE_CONCENTRATION_SMOOTHING_ALPHA) * smoothedParticleConcentration;
                } else {
                    smoothedParticleConcentration = chunkParticleConcentration;
                }
                latestParticleConcentration = smoothedParticleConcentration;
                latestParticleConcentrationValid = std::isfinite(latestParticleConcentration);
            } else {
                latestParticleConcentration = std::numeric_limits<double>::quiet_NaN();
                smoothedParticleConcentration = std::numeric_limits<double>::quiet_NaN();
                latestParticleConcentrationValid = false;
            }
        } else {
            latestParticleConcentration = std::numeric_limits<double>::quiet_NaN();
            smoothedParticleConcentration = std::numeric_limits<double>::quiet_NaN();
            latestParticleConcentrationValid = false;
        }
        latestParticleConcentrationTime = time.last();
        hasLatestParticleConcentration = true;
    }, Qt::QueuedConnection);

    QObject::connect(daqWorker, &DaqWorker::errorOccurred, &window, [&](const QString& msg) {
        is_acquiring = false;
        stopPumpSafely();
        updateAcqUi();
        QMessageBox::critical(&window, "数据采集错误", msg);
    });

    QObject::connect(ui.btnAcqStart, &QPushButton::clicked, [&]() {
        if (startupPhase != StartupPhase::Ready) {
            QMessageBox::information(&window, "暂不能采集", "请等待启动自检和 10 分钟热机完成。");
            return;
        }
        if (daqWorker->isRunning()) return;
        QString pumpError;
        if (!startPumpAtFullPower || !startPumpAtFullPower(&pumpError)) {
            QMessageBox::critical(
                &window,
                "无法开始采集",
                QString("气泵无法切换到满功率，已取消本次采集：%1").arg(pumpError));
            updateAcqUi();
            return;
        }
        rawTimeBuffer.clear();
        rawVoltageBuffer.clear();
        opcDisplayTimeBuffer.clear();
        opcDisplayVoltageBuffer.clear();
        hasLatestOpcFrame = false;
        latestParticleConcentration = std::numeric_limits<double>::quiet_NaN();
        smoothedParticleConcentration = std::numeric_limits<double>::quiet_NaN();
        hasLatestParticleConcentration = false;
        latestParticleConcentrationValid = false;
        particlePlotFollowLatest = true;
        particlePlotAutoY = true;
        ui.opcPlot->graph(0)->data()->clear();
        ui.particleConcentrationPlot->graph(0)->data()->clear();
        ui.particleConcentrationPlot->xAxis->setRange(0, 60);
        ui.particleConcentrationPlot->yAxis->setRange(0, 10);
        ui.lblParticleConcentration->setText("-- 个/ml");
        is_acquiring = true;
        daqWorker->startDaq();
        updateAcqUi();
    });

    QObject::connect(ui.btnAcqStop, &QPushButton::clicked, [&]() {
        if (!is_acquiring && !daqWorker->isRunning()) return;
        is_acquiring = false;
        daqWorker->stopDaq();
        updateAcqUi();
    });

    QTimer *plotRefreshTimer = new QTimer(&window);
    QObject::connect(plotRefreshTimer, &QTimer::timeout, [&]() {
        if (!is_acquiring) return;

        if (hasLatestParticleConcentration) {
            ui.lblParticleConcentration->setText(
                latestParticleConcentrationValid
                    ? formatParticleConcentration(latestParticleConcentration)
                    : "-- 个/ml"
            );
            if (latestParticleConcentrationValid) {
                ui.particleConcentrationPlot->graph(0)->addData(
                    latestParticleConcentrationTime,
                    latestParticleConcentration
                );
                if (particlePlotFollowLatest) {
                    ui.particleConcentrationPlot->xAxis->setRange(
                        latestParticleConcentrationTime,
                        60.0,
                        Qt::AlignRight
                    );
                }
                if (particlePlotAutoY &&
                    latestParticleConcentration >
                    ui.particleConcentrationPlot->yAxis->range().upper * 0.85) {
                    ui.particleConcentrationPlot->yAxis->setRange(
                        0,
                        qMax(10.0, latestParticleConcentration * 1.25)
                    );
                }
                ui.particleConcentrationPlot->replot(QCustomPlot::rpQueuedReplot);
            }
            hasLatestParticleConcentration = false;
        }

        if (hasLatestOpcFrame && ui.tabs->currentWidget() == ui.opcTab && !opcDisplayTimeBuffer.isEmpty()) {
            ui.opcPlot->graph(0)->setData(opcDisplayTimeBuffer, opcDisplayVoltageBuffer);
            ui.opcPlot->xAxis->setRange(opcDisplayTimeBuffer.last(), OPC_DISPLAY_WINDOW_SECONDS, Qt::AlignRight);
            ui.opcPlot->replot(QCustomPlot::rpQueuedReplot);
            hasLatestOpcFrame = false;
        }
    });
    plotRefreshTimer->start(100);

    auto resetParticlePlotView = [&]() {
        particlePlotFollowLatest = true;
        particlePlotAutoY = true;
        ui.particleConcentrationPlot->axisRect()->setRangeZoom(Qt::Vertical);
        ui.particleConcentrationPlot->axisRect()->setRangeZoomAxes(nullptr, ui.particleConcentrationPlot->yAxis);
        if (latestParticleConcentrationTime > 0.0) {
            ui.particleConcentrationPlot->xAxis->setRange(
                latestParticleConcentrationTime,
                60.0,
                Qt::AlignRight
            );
        } else {
            ui.particleConcentrationPlot->xAxis->setRange(0, 60);
        }
        ui.particleConcentrationPlot->yAxis->setRange(
            0,
            latestParticleConcentrationValid
                ? qMax(10.0, latestParticleConcentration * 1.25)
                : 10.0
        );
        ui.particleConcentrationPlot->replot(QCustomPlot::rpQueuedReplot);
    };

    QObject::connect(ui.btnResetParticlePlot, &QPushButton::clicked, resetParticlePlotView);

    QObject::connect(ui.particleConcentrationPlot, &QCustomPlot::mousePress, [&](QMouseEvent *event) {
        if (event && event->button() == Qt::LeftButton) {
            particlePlotFollowLatest = false;
        }
    });
    QObject::connect(ui.particleConcentrationPlot, &QCustomPlot::mouseWheel, [&](QWheelEvent *event) {
        if (event && event->modifiers().testFlag(Qt::ControlModifier)) {
            ui.particleConcentrationPlot->axisRect()->setRangeZoom(Qt::Horizontal);
            ui.particleConcentrationPlot->axisRect()->setRangeZoomAxes(ui.particleConcentrationPlot->xAxis, nullptr);
            particlePlotFollowLatest = false;
        } else {
            ui.particleConcentrationPlot->axisRect()->setRangeZoom(Qt::Vertical);
            ui.particleConcentrationPlot->axisRect()->setRangeZoomAxes(nullptr, ui.particleConcentrationPlot->yAxis);
            particlePlotAutoY = false;
        }
    });
    QObject::connect(ui.particleConcentrationPlot, &QCustomPlot::mouseDoubleClick, [&](QMouseEvent *) {
        resetParticlePlotView();
    });

    QObject::connect(ui.btnSaveRaw, &QPushButton::clicked, [&]() {
        if (rawTimeBuffer.isEmpty()) {
            QMessageBox::information(&window, "保存原始数据", "当前没有可保存的原始信号数据。");
            return;
        }

        QString defaultName = QString("opc_raw_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        QString fileName = QFileDialog::getSaveFileName(&window, "保存 OPC 原始信号", defaultName, "CSV 文件 (*.csv)");
        if (fileName.isEmpty()) return;

        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(&window, "保存失败", "无法打开文件进行写入。");
            return;
        }

        QTextStream out(&file);
        out << "Time(s),Voltage(V)\n";
        for (int i = 0; i < rawTimeBuffer.size() && i < rawVoltageBuffer.size(); ++i) {
            out << QString::number(rawTimeBuffer[i], 'f', 6) << ","
                << QString::number(rawVoltageBuffer[i], 'f', 5) << "\n";
        }
        file.close();
        QMessageBox::information(&window, "保存完成", QString("已保存 %1 个采样点。").arg(rawTimeBuffer.size()));
    });

    updateAcqUi();

    auto appendLiquidLog = [&](const QString& msg) {
        QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss");
        ui.liquidLog->append(QString("[%1] %2").arg(timeStr, msg));
    };

    auto setTrafficLightState = [](QLabel *lamp, bool normal) {
        const QString color = normal ? QStringLiteral("#27AE60") : QStringLiteral("#D64541");
        const QString borderColor = normal ? QStringLiteral("#1E8449") : QStringLiteral("#A93226");
        lamp->setStyleSheet(QString("background-color: %1; border: 2px solid %2; border-radius: 10px;")
                                .arg(color, borderColor));
        lamp->setAccessibleName(normal ? QStringLiteral("正常") : QStringLiteral("异常"));
    };

    auto setLiquidUiState = [&](const QString& state) {
        ui.lblLiquidState->setText(state);
        const bool normal = state == QStringLiteral("正常");
        ui.lblLiquidState->setStyleSheet(normal
            ? "font-size: 14px; color: #187A5A; font-weight: bold; background: #E8F8F5; border: 1px solid #A3E4D7; border-radius: 12px; padding: 4px 12px;"
            : "font-size: 14px; color: #A93226; font-weight: bold; background: #FDEDEC; border: 1px solid #F5B7B1; border-radius: 12px; padding: 4px 12px;");
        setTrafficLightState(ui.lblOverviewLiquidLamp, normal);
    };

    auto liquidUiStateFromMessage = [&](const QString& msg) {
        if (msg.contains("严重") || msg.contains("警告") ||
            msg.contains("失败") || msg.contains("错误") ||
            msg.contains("超时")) {
            return QString("异常");
        }
        if (msg.contains("正常") || msg.contains("满液")) return QString("正常");
        if (msg.contains("缺液") || msg.contains("补液")) return QString("异常");
        return QString();
    };

    std::array<QString, 3> compactPressureStates = {{"初始化", "初始化", "初始化"}};
    auto refreshCompactDeviceState = [&]() {
        ui.lblCompactDeviceState->setText(QString("气泵: %1    流量: %2    压差 A0:%3  A1:%4  A2:%5")
            .arg(is_pump_running ? QString("%1 %").arg(pump_current_power, 0, 'f', 0) : "关")
            .arg(is_bypass_valve_open ? "1.5 L/min" : "0.3 L/min")
            .arg(compactPressureStates[0])
            .arg(compactPressureStates[1])
            .arg(compactPressureStates[2]));
    };

    auto refreshFlowModeState = [&]() {
        ui.lblFlowModeState->setText(
            is_bypass_valve_open ? "当前：大流量 1.5 L/min" : "当前：小流量 0.3 L/min");
        ui.btnBypassHighFlow->setChecked(is_bypass_valve_open);
        ui.btnBypassLowFlow->setChecked(!is_bypass_valve_open);
        ui.btnBypassHighFlow->setEnabled(bypassValveReady && !is_bypass_valve_open);
        ui.btnBypassLowFlow->setEnabled(bypassValveReady && is_bypass_valve_open);
        refreshCompactDeviceState();
    };

    auto refreshPumpState = [&]() {
        ui.lblOverviewPump->setText(is_pump_running ? QString("%1 %").arg(pump_current_power, 0, 'f', 0) : "关");
        refreshCompactDeviceState();
    };

    refreshPumpControls = [&]() {
        const bool unlocked = startupPhase == StartupPhase::Ready && !pressureZeroing;
        ui.btnPumpStart->setEnabled(unlocked && pumpReady && !is_pump_running);
        ui.btnPumpStop->setEnabled(unlocked && pumpReady && is_pump_running);
        ui.sliderPump->setEnabled(unlocked && pumpReady);
    };
    stopPumpSafely = [&]() {
        const bool stopped = vacuum_pump.set_duty_cycle(0.0);
        pumpReady = stopped && vacuum_pump.isReady();
        is_pump_running = false;
        refreshPumpControls();
        refreshPumpState();
    };
    startPumpAtFullPower = [&](QString *error) {
        if (startupPhase != StartupPhase::Ready || !pumpReady || pressureZeroing) {
            if (error) *error = "气泵尚未就绪或仍处于启动锁定状态";
            return false;
        }
        ui.sliderPump->setValue(100);
        pump_current_power = 100.0;
        if (!vacuum_pump.set_duty_cycle(100.0)) {
            pumpReady = vacuum_pump.isReady();
            is_pump_running = false;
            if (error) *error = QString::fromStdString(vacuum_pump.errorString());
            refreshPumpControls();
            refreshPumpState();
            return false;
        }
        is_pump_running = true;
        if (error) error->clear();
        refreshPumpControls();
        refreshPumpState();
        return true;
    };

    LiquidControlSystem *liquidSystem = nullptr;
    bool liquidAlertShown = false;
    if (gpio_handle >= 0) {
        LiquidControlSystem *candidate = new LiquidControlSystem(gpio_handle,
                                                                 PinMap::PIN_LEVEL_SENSOR,
                                                                 PinMap::PIN_INLET_VALVE,
                                                                 PinMap::PIN_OUTLET_VALVE);
        if (candidate->isReady()) {
            liquidSystem = candidate;
        } else {
            ui.lblStatus->setText("状态: 液位控制器不可用");
            setLiquidUiState("异常");
            appendLiquidLog(candidate->errorString());
            delete candidate;
        }
    } else {
        ui.lblStatus->setText("状态: GPIO 控制器不可用");
        setLiquidUiState("异常");
        appendLiquidLog(QString("无法打开 gpiochip %1，GPIO 执行器控制已禁用。").arg(PinMap::GPIO_CHIP));
    }

    if (liquidSystem) {
        QObject::connect(liquidSystem, &LiquidControlSystem::statusMessage, [&](const QString& msg) {
            const QString state = liquidUiStateFromMessage(msg);
            if (!state.isEmpty()) {
                setLiquidUiState(state);
                liquidLevelNormal = state == QStringLiteral("正常");
            }
            if (msg.contains("液位正常") || msg.contains("正常满液")) {
                liquidAlertShown = false;
            }
            appendLiquidLog(msg);
            evaluateStartup();
        });
        QObject::connect(liquidSystem, &LiquidControlSystem::alertMessage, [&](const QString& alert) {
            liquidLevelNormal = false;
            setLiquidUiState("异常");
            appendLiquidLog(alert);
            evaluateStartup();
            if (liquidAlertShown) return;
            liquidAlertShown = true;
            QMessageBox::critical(&window, "液位报警", alert);
        });
    }

    bool opcTemperatureValid = false;
    ui.btnCondStart->setEnabled(false);
    ui.btnCondStop->setEnabled(false);
    ui.btnSatStart->setEnabled(false);
    ui.btnSatStop->setEnabled(false);
    ui.btnPumpStart->setEnabled(false);
    ui.btnPumpStop->setEnabled(false);
    ui.sliderPump->setEnabled(false);
    // Wait for the first valid PT100 sample before allowing a heater start.
    ui.btnOpcStart->setEnabled(false);
    ui.btnOpcStop->setEnabled(false);
    refreshFlowModeState();
    ui.btnLiquidStart->setEnabled(false);
    ui.btnLiquidStop->setEnabled(liquidSystem != nullptr);
    ui.btnDrain->setEnabled(false);
    if (liquidSystem) {
        setLiquidUiState("异常");
        liquidSystem->startMonitoring();
    }
    if (!condPwmReady) {
        ui.lblCondPwm->setText("PWM 不可用");
        ui.lblCondPwm->setToolTip(QString::fromStdString(peltier_cond.errorString()));
    }
    if (!satPwmReady) {
        ui.lblSatPwm->setText("PWM 不可用");
        ui.lblSatPwm->setToolTip(QString::fromStdString(heater_sat.errorString()));
    }
    if (!opcHeaterReady) {
        ui.lblOpcPwm->setText("PWM 不可用");
        ui.lblOpcPwm->setToolTip(QString::fromStdString(opc_heater.errorString()));
    } else if (!opc_sensor.isAvailable()) {
        ui.lblOpcPwm->setText("温度传感器不可用");
        ui.lblOpcPwm->setToolTip("无法打开 OPC 段 PT100：/dev/spidev1.2；为安全起见已禁用加热。");
    }

    Ads1115PressureSensor *pressureSensor =
        new Ads1115PressureSensor("/dev/i2c-1", 0x48, &window);
    std::array<double, 3> latestPressurePa = {{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN()
    }};
    std::array<qint64, 3> latestPressureTimeMs = {{0, 0, 0}};
    std::array<QString, 3> latestPressureWarnings;
    QObject::connect(pressureSensor,
                     &Ads1115PressureSensor::zeroCalibrationProgress,
                     &window,
                     [&](int percent) {
        pressureZeroing = true;
        for (int channel = 0; channel < 3; ++channel) {
            ui.lblPressureValue[channel]->setText(QString("校零 %1%").arg(percent));
            ui.lblPressureStatus[channel]->setText(QString("校零中 %1%").arg(percent));
            ui.lblPressureStatus[channel]->setToolTip(
                "零点校准期间请保持气泵关闭，且 H/L 两侧等压。");
            ui.lblPressureStatus[channel]->setStyleSheet(
                "font-size: 11px; color: #B95E00; font-weight: bold; background: #FEF5E7; "
                "border: 1px solid #F5CBA7; border-radius: 10px; padding: 3px 8px;");
            compactPressureStates[channel] = QString("校零%1%").arg(percent);
        }
        ui.btnPressureZero->setEnabled(false);
        ui.btnPressureControlStart->setEnabled(false);
        refreshPumpControls();
        refreshCompactDeviceState();
        evaluateStartup();
    });
    QObject::connect(pressureSensor,
                     &Ads1115PressureSensor::zeroCalibrationFinished,
                     &window,
                     [&](int channel, double zeroVoltage, double uncorrectedPressurePa) {
        if (channel < 0 || channel >= 3) return;
        ui.lblPressureStatus[channel]->setText("校零完成");
        ui.lblPressureStatus[channel]->setToolTip(
            QString("Vzero=%1 V，校正前 %2 Pa")
                .arg(zeroVoltage, 0, 'f', 6)
                .arg(uncorrectedPressurePa, 0, 'f', 1));
        ui.lblPressureStatus[channel]->setStyleSheet(
            "font-size: 11px; color: #187A5A; font-weight: bold; background: #E8F8F5; "
            "border: 1px solid #A3E4D7; border-radius: 10px; padding: 3px 8px;");
        ui.btnPressureZero->setEnabled(true);
        ui.btnPressureControlStart->setEnabled(true);
        pressureZeroing = pressureSensor->isZeroing();
        refreshPumpControls();
        updateAcqUi();
        evaluateStartup();
    });
    QObject::connect(pressureSensor,
                     &Ads1115PressureSensor::pressureUpdated,
                     &window,
                     [&](int channel,
                         double pressurePa,
                         double adcVoltage,
                         double sensorVoltage,
                         double fullScalePercent,
                         const QString& warning) {
        if (channel < 0 || channel >= 3) return;
        latestPressurePa[channel] = pressurePa;
        latestPressureTimeMs[channel] = QDateTime::currentMSecsSinceEpoch();
        latestPressureWarnings[channel] = warning;
        const QString pressureText = channel == 0
            ? QString("%1 kPa").arg(pressurePa / 1000.0, 0, 'f', 3)
            : QString("%1 Pa").arg(pressurePa, 0, 'f', 1);
        ui.lblPressureValue[channel]->setText(pressureText);
        ui.lblPressureDetails[channel]->setText(
            QString("%1 V → %2 V · %3 %FS")
                .arg(adcVoltage, 0, 'f', 4)
                .arg(sensorVoltage, 0, 'f', 4)
                .arg(fullScalePercent, 0, 'f', 1));
        if (warning.isEmpty()) {
            ui.lblPressureStatus[channel]->setText("正常");
            ui.lblPressureStatus[channel]->setToolTip(
                QString("/dev/i2c-1 · 地址 0x48 · A%1 · ADS 128 SPS 轮询").arg(channel));
            ui.lblPressureStatus[channel]->setStyleSheet(
                "font-size: 11px; color: #187A5A; font-weight: bold; background: #E8F8F5; "
                "border: 1px solid #A3E4D7; border-radius: 10px; padding: 3px 8px;");
        } else {
            ui.lblPressureStatus[channel]->setText("警告");
            ui.lblPressureStatus[channel]->setToolTip(warning);
            ui.lblPressureStatus[channel]->setStyleSheet(
                "font-size: 11px; color: #A93226; font-weight: bold; background: #FDEDEC; "
                "border: 1px solid #F5B7B1; border-radius: 10px; padding: 3px 8px;");
        }
        compactPressureStates[channel] = pressureText;
        refreshCompactDeviceState();
        evaluateStartup();
    });
    QObject::connect(pressureSensor,
                     &Ads1115PressureSensor::errorOccurred,
                     &window,
                     [&](const QString& error) {
        for (int channel = 0; channel < 3; ++channel) {
            ui.lblPressureValue[channel]->setText("不可用");
            ui.lblPressureStatus[channel]->setText("通信错误");
            ui.lblPressureStatus[channel]->setToolTip(
                QString("ADS1115 通信错误：%1；请检查 I2C 接线并运行 i2cdetect -y 1").arg(error));
            ui.lblPressureStatus[channel]->setStyleSheet(
                "font-size: 11px; color: #A93226; font-weight: bold; background: #FDEDEC; "
                "border: 1px solid #F5B7B1; border-radius: 10px; padding: 3px 8px;");
            compactPressureStates[channel] = "不可用";
        }
        ui.btnPressureZero->setEnabled(pressureSensor->isReady());
        ui.btnPressureControlStart->setEnabled(false);
        latestPressureTimeMs = {{0, 0, 0}};
        pressureZeroing = pressureSensor->isZeroing();
        refreshPumpControls();
        updateAcqUi();
        refreshCompactDeviceState();
        evaluateStartup();
    });
    QObject::connect(ui.btnPressureZero, &QPushButton::clicked, [&]() {
        if (is_pump_running) {
            stopPumpSafely();
        }
        pressureZeroing = true;
        refreshPumpControls();
        updateAcqUi();
        pressureSensor->startZeroCalibration();
    });
    if (!pressureSensor->start()) {
        ui.btnPressureZero->setEnabled(false);
        ui.btnPressureControlStart->setEnabled(false);
    }

    QObject::connect(ui.sliderPump, &QSlider::valueChanged, [&](int val) {
        pump_current_power = val;
        ui.lblPumpValue->setText(QString("%1 %").arg(val));
        if (is_pump_running) vacuum_pump.set_duty_cycle(pump_current_power);
        refreshPumpState();
    });
    QObject::connect(ui.btnPumpStart, &QPushButton::clicked, [&]() {
        if (startupPhase != StartupPhase::Ready || pressureZeroing || !pumpReady) return;
        if (!vacuum_pump.set_duty_cycle(pump_current_power)) {
            pumpReady = vacuum_pump.isReady();
            is_pump_running = false;
            QMessageBox::critical(&window,
                                  "气泵启动失败",
                                  QString::fromStdString(vacuum_pump.errorString()));
        } else {
            is_pump_running = true;
        }
        refreshPumpControls();
        refreshPumpState();
    });
    QObject::connect(ui.btnPumpStop, &QPushButton::clicked, [&]() {
        stopPumpSafely();
    });

    auto setValveStatus = [&](const QString& text, bool error) {
        ui.lblValveStatus->setText(text);
        ui.lblValveStatus->setStyleSheet(
            QString("font-size: 13px; font-weight: bold; color: %1;")
                .arg(error ? "#C0392B" : "#187A5A"));
    };
    auto showValveError = [&](const QString& operation, const QString& error) {
        const QString message = QString("%1失败: %2").arg(operation, error);
        setValveStatus(message, true);
        QMessageBox::warning(&window, "比例阀通信错误", message);
    };
    auto showValveCurrent = [&](double currentMilliamp) {
        const double opening = N4IOA01Valve::currentToOpening(currentMilliamp);
        ui.lblValveCurrent->setText(QString("%1 mA").arg(currentMilliamp, 0, 'f', 2));
        ui.sbValveOpening->setValue(opening);
    };

    const std::array<double, 3> pressureFullScalePa = {{40000.0, 500.0, 300.0}};
    PressureValveController pressureValveController;
    QTimer pressureControlTimer;
    pressureControlTimer.setInterval(500);
    pressureControlTimer.setTimerType(Qt::PreciseTimer);
    QElapsedTimer pressureControlElapsed;
    bool pressureControlActive = false;

    auto selectedPressureChannel = [&]() {
        const int channel = ui.cmbPressureControlChannel->currentData().toInt();
        return channel >= 0 && channel < 3 ? channel : 0;
    };
    auto targetPressurePa = [&]() {
        return selectedPressureChannel() == 0
            ? ui.sbPressureTarget->value() * 1000.0
            : ui.sbPressureTarget->value();
    };
    auto setPressureControlUi = [&](bool active) {
        pressureControlActive = active;
        ui.cmbPressureControlChannel->setEnabled(!active);
        ui.sbPressureTarget->setEnabled(!active);
        ui.cmbPressureControlDirection->setEnabled(!active);
        ui.sbPressureKp->setEnabled(!active);
        ui.sbPressureKi->setEnabled(!active);
        ui.btnPressureControlStart->setEnabled(
            !active && pressureSensor->isReady() && !pressureSensor->isZeroing());
        ui.btnPressureControlStop->setEnabled(active);
        ui.btnValveApply->setEnabled(!active);
        ui.btnValveRead->setEnabled(!active);
        ui.sbValveOpening->setEnabled(!active);
        ui.btnPressureZero->setEnabled(
            !active && pressureSensor->isReady() && !pressureSensor->isZeroing());
    };
    auto stopPressureControl = [&](const QString& reason,
                                   bool safeClose,
                                   bool showWarning) {
        if (!pressureControlActive) return;
        pressureControlTimer.stop();
        setPressureControlUi(false);

        QString closeError;
        if (safeClose) {
            if (proportional_valve.safeClose(&closeError)) {
                showValveCurrent(4.0);
                setValveStatus("闭环已停止，模块已确认安全输出 4.00 mA。", false);
            } else {
                setValveStatus(QString("闭环已停止，但安全关闭失败: %1").arg(closeError), true);
            }
        }

        QString status = reason;
        if (!closeError.isEmpty()) status += QString("；安全关闭失败：%1").arg(closeError);
        ui.lblPressureControlStatus->setText(status);
        ui.lblPressureControlStatus->setStyleSheet(
            "font-size: 12px; color: #A93226; font-weight: bold; background: #FDEDEC; "
            "border: 1px solid #F5B7B1; border-radius: 6px; padding: 6px 9px;");
        if (showWarning) {
            QMessageBox::warning(&window, "压差闭环已停止", status);
        }
    };

    auto updatePressureTargetEditor = [&]() {
        const bool isKpa = selectedPressureChannel() == 0;
        ui.sbPressureTarget->setRange(0.0, isKpa ? 40.0 : pressureFullScalePa[selectedPressureChannel()]);
        ui.sbPressureTarget->setDecimals(isKpa ? 3 : 1);
        ui.sbPressureTarget->setSingleStep(isKpa ? 0.1 : 1.0);
        ui.sbPressureTarget->setSuffix(isKpa ? " kPa" : " Pa");
        ui.sbPressureTarget->setValue(0.0);
    };
    QObject::connect(ui.cmbPressureControlChannel,
                     QOverload<int>::of(&QComboBox::currentIndexChanged),
                     [&](int) { updatePressureTargetEditor(); });
    updatePressureTargetEditor();

    QObject::connect(ui.btnPressureControlStart, &QPushButton::clicked, [&]() {
        const int channel = selectedPressureChannel();
        const qint64 ageMs = QDateTime::currentMSecsSinceEpoch() - latestPressureTimeMs[channel];
        if (!pressureSensor->isReady() || pressureSensor->isZeroing() ||
            latestPressureTimeMs[channel] == 0 || ageMs > 1500 ||
            !std::isfinite(latestPressurePa[channel])) {
            QMessageBox::warning(&window, "无法启动压差闭环", "所选压差传感器尚无有效的新鲜测量值。");
            return;
        }
        if (!latestPressureWarnings[channel].isEmpty()) {
            QMessageBox::warning(&window,
                                 "无法启动压差闭环",
                                 QString("所选传感器当前报警：%1").arg(latestPressureWarnings[channel]));
            return;
        }
        if (!is_pump_running) {
            QMessageBox::warning(&window, "无法启动压差闭环", "请先启动气泵，再启动压差闭环。");
            return;
        }

        double confirmedCurrent = 0.0;
        QString error;
        const double initialOpening = ui.sbValveOpening->value();
        if (!proportional_valve.setOpeningPercent(initialOpening, &confirmedCurrent, &error)) {
            showValveError("启动闭环前确认比例阀输出", error);
            return;
        }

        PressureValveController::Parameters parameters;
        parameters.kp = ui.sbPressureKp->value();
        parameters.ki = ui.sbPressureKi->value();
        parameters.deadbandPercentOfFullScale = 0.20;
        parameters.maxStepPercent = 5.0;
        pressureValveController.setParameters(parameters);
        pressureValveController.reset(initialOpening);
        showValveCurrent(confirmedCurrent);
        setPressureControlUi(true);
        pressureControlElapsed.start();
        pressureControlTimer.start();
        ui.lblPressureControlStatus->setText(
            QString("闭环运行中：A%1，目标 %2，控制输出从 %3 % 开始。")
                .arg(channel)
                .arg(channel == 0
                         ? QString("%1 kPa").arg(targetPressurePa() / 1000.0, 0, 'f', 3)
                         : QString("%1 Pa").arg(targetPressurePa(), 0, 'f', 1))
                .arg(initialOpening, 0, 'f', 1));
        ui.lblPressureControlStatus->setStyleSheet(
            "font-size: 12px; color: #117A65; font-weight: bold; background: #E8F8F5; "
            "border: 1px solid #A3E4D7; border-radius: 6px; padding: 6px 9px;");
    });

    QObject::connect(&pressureControlTimer, &QTimer::timeout, [&]() {
        if (!pressureControlActive) return;
        const int channel = selectedPressureChannel();
        const qint64 ageMs = QDateTime::currentMSecsSinceEpoch() - latestPressureTimeMs[channel];
        if (!pressureSensor->isReady() || pressureSensor->isZeroing() ||
            latestPressureTimeMs[channel] == 0 || ageMs > 1500 ||
            !std::isfinite(latestPressurePa[channel])) {
            stopPressureControl("压差反馈丢失或超时，闭环已安全停止。", true, true);
            return;
        }
        if (!latestPressureWarnings[channel].isEmpty()) {
            stopPressureControl(
                QString("压差传感器报警：%1。闭环已安全停止。")
                    .arg(latestPressureWarnings[channel]),
                true,
                true);
            return;
        }
        if (!is_pump_running) {
            stopPressureControl("气泵已停止，压差闭环已安全停止。", true, false);
            return;
        }

        const double elapsedSeconds = std::max(
            0.1,
            std::min(2.0, static_cast<double>(pressureControlElapsed.restart()) / 1000.0));
        const bool openingRaisesPressure =
            ui.cmbPressureControlDirection->currentData().toBool();
        const double previousOpening = pressureValveController.openingPercent();
        const double opening = pressureValveController.update(targetPressurePa(),
                                                                latestPressurePa[channel],
                                                                pressureFullScalePa[channel],
                                                                elapsedSeconds,
                                                                openingRaisesPressure);
        double confirmedCurrent = N4IOA01Valve::openingToCurrent(opening);
        if (std::abs(opening - previousOpening) >= 0.05) {
            QString error;
            if (!proportional_valve.setOpeningPercent(opening, &confirmedCurrent, &error)) {
                stopPressureControl(QString("比例阀通信失败：%1。闭环已停止。").arg(error),
                                    true,
                                    true);
                return;
            }
            showValveCurrent(confirmedCurrent);
        }

        const double errorPa = targetPressurePa() - latestPressurePa[channel];
        ui.lblPressureControlStatus->setText(
            QString("闭环运行：A%1  目标 %2 Pa  当前 %3 Pa  误差 %4 Pa  开度 %5 %  输出 %6 mA")
                .arg(channel)
                .arg(targetPressurePa(), 0, 'f', 1)
                .arg(latestPressurePa[channel], 0, 'f', 1)
                .arg(errorPa, 0, 'f', 1)
                .arg(opening, 0, 'f', 1)
                .arg(confirmedCurrent, 0, 'f', 2));
    });

    QObject::connect(ui.btnPressureControlStop, &QPushButton::clicked, [&]() {
        stopPressureControl("用户停止了压差闭环，比例阀已回到安全最小开度。", true, false);
    });
    QObject::connect(ui.btnPumpStop, &QPushButton::clicked, [&]() {
        stopPressureControl("气泵已停止，压差闭环已安全停止。", true, false);
    });
    QObject::connect(pressureSensor, &Ads1115PressureSensor::errorOccurred,
                     [&](const QString& error) {
        stopPressureControl(QString("压差传感器通信错误：%1。闭环已安全停止。").arg(error),
                            true,
                            true);
    });
    QObject::connect(pressureSensor, &Ads1115PressureSensor::zeroCalibrationFinished,
                     [&](int, double, double) {
        if (!pressureControlActive && !pressureSensor->isZeroing()) {
            setPressureControlUi(false);
        }
    });
    QObject::connect(ui.btnValveClose, &QPushButton::clicked, [&]() {
        stopPressureControl("比例阀执行安全关闭，压差闭环已停止。", false, false);
    });

    QObject::connect(ui.sbValveOpening,
                     QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                     [&](double opening) {
        ui.lblValveCurrent->setText(
            QString("%1 mA")
                .arg(N4IOA01Valve::openingToCurrent(opening), 0, 'f', 2));
    });
    QObject::connect(ui.btnValveApply, &QPushButton::clicked, [&]() {
        const double opening = ui.sbValveOpening->value();
        double currentMilliamp = 0.0;
        QString error;
        if (!proportional_valve.setOpeningPercent(opening, &currentMilliamp, &error)) {
            showValveError("设置开度", error);
            return;
        }
        showValveCurrent(currentMilliamp);
        setValveStatus(QString("设置成功: 开度 %1 %，模块已确认输出 %2 mA")
                           .arg(opening, 0, 'f', 1)
                           .arg(currentMilliamp, 0, 'f', 2),
                       false);
    });
    QObject::connect(ui.btnValveRead, &QPushButton::clicked, [&]() {
        double currentMilliamp = 0.0;
        QString error;
        if (!proportional_valve.readCurrentMilliamp(&currentMilliamp, &error)) {
            showValveError("读取输出", error);
            return;
        }
        showValveCurrent(currentMilliamp);
        setValveStatus(QString("读取成功: 当前输出 %1 mA，对应开度 %2 %")
                           .arg(currentMilliamp, 0, 'f', 2)
                           .arg(N4IOA01Valve::currentToOpening(currentMilliamp), 0, 'f', 1),
                       false);
    });
    QObject::connect(ui.btnValveClose, &QPushButton::clicked, [&]() {
        QString error;
        if (!proportional_valve.safeClose(&error)) {
            showValveError("安全关闭", error);
            return;
        }
        showValveCurrent(4.0);
        setValveStatus("安全关闭成功: 模块已确认输出 4.00 mA，阀门开度 0.0 %", false);
    });

    // Apply the value shown in the editor at startup.  The UI default is 80%,
    // so this performs a real N4IOA01 write instead of only displaying it.
    QTimer::singleShot(250, &window, [&]() {
        const double opening = ui.sbValveOpening->value();
        double currentMilliamp = 0.0;
        QString error;
        if (!proportional_valve.setOpeningPercent(opening, &currentMilliamp, &error)) {
            setValveStatus(QString("默认开度 %1 % 自动设置失败: %2")
                               .arg(opening, 0, 'f', 1)
                               .arg(error),
                           true);
            return;
        }
        showValveCurrent(currentMilliamp);
        setValveStatus(QString("默认开度已自动设置: %1 %，模块已确认输出 %2 mA")
                           .arg(opening, 0, 'f', 1)
                           .arg(currentMilliamp, 0, 'f', 2),
                       false);
    });

    QObject::connect(ui.btnBypassHighFlow, &QPushButton::clicked, [&]() {
        is_bypass_valve_open = bypass_valve.set(true);
        if (is_bypass_valve_open) {
            currentSampleFlowMlMin = BYPASS_HIGH_FLOW_ML_MIN;
        }
        refreshFlowModeState();
    });
    QObject::connect(ui.btnBypassLowFlow, &QPushButton::clicked, [&]() {
        if (bypass_valve.set(false)) {
            is_bypass_valve_open = false;
            currentSampleFlowMlMin = BYPASS_LOW_FLOW_ML_MIN;
        }
        refreshFlowModeState();
    });
    QObject::connect(ui.btnLiquidStart, &QPushButton::clicked, [&]() {
        if (!liquidSystem) return;
        setLiquidUiState("异常");
        liquidSystem->startMonitoring();
        ui.btnLiquidStart->setEnabled(false);
        ui.btnLiquidStop->setEnabled(true);
    });
    QObject::connect(ui.btnLiquidStop, &QPushButton::clicked, [&]() {
        if (!liquidSystem) return;
        liquidSystem->stopMonitoring();
        ui.btnLiquidStart->setEnabled(true);
        ui.btnLiquidStop->setEnabled(false);
    });
    QObject::connect(ui.btnDrain, &QPushButton::pressed, [&]() {
        if (startupPhase == StartupPhase::Ready && liquidSystem) liquidSystem->startManualDrain();
    });
    QObject::connect(ui.btnDrain, &QPushButton::released, [&]() {
        if (liquidSystem) liquidSystem->stopManualDrain();
    });

    bool condTemperatureValid = false;
    bool satTemperatureValid = false;
    refreshTemperatureControls = [&]() {
        const bool manualControl = startupPhase == StartupPhase::Ready;
        ui.btnCondStart->setEnabled(
            manualControl && peltier_cond.isReady() && condTemperatureValid && !is_cond_running);
        ui.btnCondStop->setEnabled(manualControl && is_cond_running);
        ui.btnSatStart->setEnabled(
            manualControl && heater_sat.isReady() && satTemperatureValid && !is_sat_running);
        ui.btnSatStop->setEnabled(manualControl && is_sat_running);
        ui.btnOpcStart->setEnabled(
            manualControl && opcHeaterReady && opcTemperatureValid && !is_opc_heater_running);
        ui.btnOpcStop->setEnabled(manualControl && is_opc_heater_running);
    };

    auto startupFailures = [&]() {
        QStringList failures;
        if (!liquidSystem) {
            failures << "液位控制器不可用";
        } else if (!liquidLevelNormal) {
            failures << "等待液位正常";
        }
        if (!pumpReady) {
            const QString pumpError = QString::fromStdString(vacuum_pump.errorString());
            failures << (pumpError.isEmpty()
                ? QString("气泵无法确认关闭")
                : QString("气泵无法确认关闭：%1").arg(pumpError));
        }
        return failures;
    };

    evaluateStartup = [&]() {
        if (startupPhase == StartupPhase::Ready) return;
        if (startupPhase == StartupPhase::SelfCheck) stopPumpSafely();

        const QStringList failures = startupFailures();
        if (!failures.isEmpty()) {
            startupBlockReason = failures.join("；");
            if (startupPhase == StartupPhase::WarmingUp) {
                peltier_cond.set_duty_cycle(0.0);
                heater_sat.set_duty_cycle(0.0);
                opc_heater.set_duty_cycle(0.0);
                is_cond_running = false;
                is_sat_running = false;
                is_opc_heater_running = false;
                cond_pid.reset();
                sat_pid.reset();
                opc_pid.reset();
                warmupElapsed.invalidate();
                warmupRemainingSeconds = static_cast<int>(WARMUP_DURATION_MS / 1000);
                startupPhase = StartupPhase::SelfCheck;
            }
            stopPumpSafely();
            ui.lblWarmupCountdown->setText(
                failures.first().contains("液位") ? "液位异常" : "气泵异常");
            ui.lblWarmupCountdown->setStyleSheet(
                "font-size: 18px; font-weight: 800; color: #A93226; background: transparent;");
            ui.lblWarmupCountdown->setToolTip(
                QString("倒计时开始前仍需通过：\n• %1").arg(failures.join("\n• ")));
            refreshTemperatureControls();
            refreshPumpControls();
            ui.btnDrain->setEnabled(false);
            updateAcqUi();
            return;
        }

        if (startupPhase == StartupPhase::SelfCheck) {
            startupBlockReason.clear();
            cond_pid.reset();
            sat_pid.reset();
            opc_pid.reset();
            is_cond_running = true;
            is_sat_running = true;
            is_opc_heater_running = true;
            startupPhase = StartupPhase::WarmingUp;
            warmupRemainingSeconds = static_cast<int>(WARMUP_DURATION_MS / 1000);
            warmupElapsed.start();
        }

        const qint64 remainingMs = std::max<qint64>(0, WARMUP_DURATION_MS - warmupElapsed.elapsed());
        warmupRemainingSeconds = static_cast<int>((remainingMs + 999) / 1000);
        if (remainingMs == 0) {
            startupPhase = StartupPhase::Ready;
            ui.lblWarmupCountdown->setText("完成");
            ui.lblWarmupCountdown->setStyleSheet(
                "font-size: 18px; font-weight: 800; color: #187A5A; background: transparent;");
            ui.lblWarmupCountdown->setToolTip("启动自检和 10 分钟热机均已完成，可以开始采集。");
        } else {
            ui.lblWarmupCountdown->setText(formatCountdown(warmupRemainingSeconds));
            ui.lblWarmupCountdown->setStyleSheet(
                "font-size: 18px; font-weight: 800; color: #C45F00; background: transparent;");
            ui.lblWarmupCountdown->setToolTip(
                "三段温控已自动开启；倒计时结束前禁止采集和启动气泵。");
        }
        refreshTemperatureControls();
        refreshPumpControls();
        ui.btnDrain->setEnabled(startupPhase == StartupPhase::Ready && liquidSystem);
        updateAcqUi();
    };
    QTimer::singleShot(0, &window, [&]() { evaluateStartup(); });

    QObject::connect(ui.sbCond, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&](double val) {
        cond_pid.target = val;
        cond_pid.reset();
    });
    QObject::connect(ui.sbSat, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&](double val) {
        sat_pid.target = val;
        sat_pid.reset();
    });
    QObject::connect(ui.sbOpc, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&](double val) {
        opc_pid.target = val;
        opc_pid.reset();
    });

    auto recommendedPidTunings = [](int segment) {
        if (segment == 0) return COND_PID_RECOMMENDED;
        if (segment == 1) return SAT_PID_RECOMMENDED;
        return OPC_PID_RECOMMENDED;
    };
    auto pidSegmentName = [](int segment) {
        if (segment == 0) return QStringLiteral("冷凝段");
        if (segment == 1) return QStringLiteral("饱和段");
        return QStringLiteral("OPC段");
    };
    auto pidSettingsKey = [](int segment) {
        if (segment == 0) return QStringLiteral("temperature/cond");
        if (segment == 1) return QStringLiteral("temperature/sat");
        return QStringLiteral("temperature/opc");
    };
    auto showPidTunings = [&](int segment) {
        if (segment == 0) {
            ui.sbTempKp->setValue(cond_pid.kp);
            ui.sbTempKi->setValue(cond_pid.ki);
            ui.sbTempKd->setValue(cond_pid.kd);
        } else if (segment == 1) {
            ui.sbTempKp->setValue(sat_pid.kp);
            ui.sbTempKi->setValue(sat_pid.ki);
            ui.sbTempKd->setValue(sat_pid.kd);
        } else {
            ui.sbTempKp->setValue(opc_pid.kp);
            ui.sbTempKi->setValue(opc_pid.ki);
            ui.sbTempKd->setValue(opc_pid.kd);
        }
        ui.lblTempPidStatus->setText(
            QString("%1当前参数 · 点击数值可修改，点击“应用参数”后生效")
                .arg(pidSegmentName(segment)));
    };
    auto applyPidTunings = [&](bool restoredRecommended) {
        const int segment = ui.cmbTempPidSegment->currentData().toInt();
        const double kp = ui.sbTempKp->value();
        const double ki = ui.sbTempKi->value();
        const double kd = ui.sbTempKd->value();
        if (segment == 0) {
            cond_pid.setTunings(kp, ki, kd);
        } else if (segment == 1) {
            sat_pid.setTunings(kp, ki, kd);
        } else {
            opc_pid.setTunings(kp, ki, kd);
        }
        const QString settingsKey = pidSettingsKey(segment);
        pidSettings.setValue(settingsKey + "/kp", kp);
        pidSettings.setValue(settingsKey + "/ki", ki);
        pidSettings.setValue(settingsKey + "/kd", kd);
        pidSettings.sync();
        const QString saveResult = pidSettings.status() == QSettings::NoError
            ? QStringLiteral("参数已保存")
            : QStringLiteral("参数已生效，但保存失败");
        ui.lblTempPidStatus->setText(
            QString("%1%2 Kp=%3，Ki=%4，Kd=%5；积分已清零，温度趋势保留；%6")
                .arg(pidSegmentName(segment))
                .arg(restoredRecommended ? "已恢复推荐值：" : "已应用：")
                .arg(kp, 0, 'f', 2)
                .arg(ki, 0, 'f', 3)
                .arg(kd, 0, 'f', 1)
                .arg(saveResult));
    };
    QObject::connect(ui.cmbTempPidSegment,
                     QOverload<int>::of(&QComboBox::currentIndexChanged),
                     [&](int) {
        showPidTunings(ui.cmbTempPidSegment->currentData().toInt());
    });
    QObject::connect(ui.btnTempPidApply, &QPushButton::clicked, [&]() {
        applyPidTunings(false);
    });
    QObject::connect(ui.btnTempPidReset, &QPushButton::clicked, [&]() {
        const int segment = ui.cmbTempPidSegment->currentData().toInt();
        const PidTunings recommended = recommendedPidTunings(segment);
        ui.sbTempKp->setValue(recommended.kp);
        ui.sbTempKi->setValue(recommended.ki);
        ui.sbTempKd->setValue(recommended.kd);
        applyPidTunings(true);
    });
    showPidTunings(ui.cmbTempPidSegment->currentData().toInt());

    QObject::connect(ui.btnCondStart, &QPushButton::clicked, [&]() {
        if (startupPhase != StartupPhase::Ready || !peltier_cond.isReady() || !condTemperatureValid) return;
        cond_pid.reset();
        is_cond_running = true;
        refreshTemperatureControls();
    });
    QObject::connect(ui.btnCondStop, &QPushButton::clicked, [&]() {
        if (startupPhase != StartupPhase::Ready) return;
        is_cond_running = false;
        peltier_cond.set_duty_cycle(0);
        cond_pid.reset();
        refreshTemperatureControls();
    });
    QObject::connect(ui.btnSatStart, &QPushButton::clicked, [&]() {
        if (startupPhase != StartupPhase::Ready || !heater_sat.isReady() || !satTemperatureValid) return;
        sat_pid.reset();
        is_sat_running = true;
        refreshTemperatureControls();
    });
    QObject::connect(ui.btnSatStop, &QPushButton::clicked, [&]() {
        if (startupPhase != StartupPhase::Ready) return;
        is_sat_running = false;
        heater_sat.set_duty_cycle(0);
        sat_pid.reset();
        refreshTemperatureControls();
    });
    QObject::connect(ui.btnOpcStart, &QPushButton::clicked, [&]() {
        if (startupPhase != StartupPhase::Ready || !opcHeaterReady || !opcTemperatureValid) return;
        opc_pid.reset();
        is_opc_heater_running = true;
        refreshTemperatureControls();
        ui.lblOpcPwm->setToolTip("单根 100 W 加热棒正在进行闭环温控。");
    });
    QObject::connect(ui.btnOpcStop, &QPushButton::clicked, [&]() {
        if (startupPhase != StartupPhase::Ready) return;
        const bool stopped = opc_heater.set_duty_cycle(0.0);
        is_opc_heater_running = false;
        opc_pid.reset();
        if (!stopped) {
            opcHeaterReady = false;
            ui.lblOpcPwm->setText("PWM 关闭失败");
            ui.lblOpcPwm->setToolTip(QString::fromStdString(opc_heater.errorString()));
        } else {
            ui.lblOpcPwm->setText("功率: 0.0 %");
            ui.lblOpcPwm->setToolTip(QString());
        }
        refreshTemperatureControls();
    });

    QTimer *timer = new QTimer(&window);
    bool safetyShutdownComplete = false;
    auto performSafetyShutdown = [&](const QString& trigger) {
        if (safetyShutdownComplete) return true;

        QStringList results;
        bool allSafe = true;
        auto recordResult = [&](const QString& device, bool success, const QString& error = QString()) {
            results << (success
                ? QString("[成功] %1").arg(device)
                : QString("[失败] %1%2")
                      .arg(device, error.isEmpty() ? QString() : QString("：%1").arg(error)));
            if (!success) allSafe = false;
        };

        // Stop all periodic control work before changing outputs so that no
        // timer can re-enable an actuator during the shutdown sequence.
        timer->stop();
        pressureControlTimer.stop();
        plotRefreshTimer->stop();
        daqWorker->stopDaq();
        results << "[成功] 已请求停止数据采集与全部控制定时器";

        is_cond_running = false;
        is_sat_running = false;
        is_opc_heater_running = false;
        is_pump_running = false;
        is_bypass_valve_open = false;

        const bool condStopped = peltier_cond.safeDisable();
        recordResult("冷凝段 PWM 归零", condStopped,
                     QString::fromStdString(peltier_cond.errorString()));
        const bool satStopped = heater_sat.safeDisable();
        recordResult("饱和段 PWM 归零", satStopped,
                     QString::fromStdString(heater_sat.errorString()));
        const bool opcStopped = opc_heater.set_duty_cycle(0.0);
        recordResult("OPC 段 PWM 归零", opcStopped,
                     QString::fromStdString(opc_heater.errorString()));
        const bool pumpStopped = vacuum_pump.set_duty_cycle(0.0);
        recordResult("气泵 PWM 归零", pumpStopped,
                     QString::fromStdString(vacuum_pump.errorString()));
        recordResult("旁路阀关闭", bypass_valve.set(false));

        if (liquidSystem) {
            QString liquidError;
            const bool liquidStopped = liquidSystem->safeStop(&liquidError);
            recordResult("进液阀和排液阀关闭", liquidStopped, liquidError);
        } else {
            results << "[跳过] 液位控制未初始化";
        }

        QString valveError;
        const bool valveStopped = proportional_valve.safeClose(&valveError);
        recordResult("比例阀安全输出 4.00 mA", valveStopped, valveError);

        pressureSensor->stop();
        results << "[成功] 压差采集已停止";
        appendSafetyShutdownLog(trigger, results);

        if (allSafe) {
            qInfo().noquote() << "安全停机完成，触发原因：" << trigger;
        } else {
            qCritical().noquote() << "安全停机存在失败项，触发原因：" << trigger
                                  << "\n" << results.join("\n");
        }
        safetyShutdownComplete = allSafe;
        return allSafe;
    };

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        performSafetyShutdown(shutdownTrigger);
    });

    QTimer terminationSignalTimer;
    terminationSignalTimer.setInterval(100);
    QObject::connect(&terminationSignalTimer, &QTimer::timeout, [&]() {
        const int signalNumber = pendingTerminationSignal;
        if (signalNumber == 0) return;
        pendingTerminationSignal = 0;
        shutdownTrigger = QString("系统终止信号 %1").arg(signalNumber);
        ui.lblStatus->setText(QString("状态: 收到终止信号 %1，正在安全关闭...").arg(signalNumber));
        app.quit();
    });
    terminationSignalTimer.start();

    QObject::connect(timer, &QTimer::timeout, [&]() {
        float t_cond = cond_sensor.read_temperature();
        float t_sat = sat_sensor.read_temperature();
        float t_opc = opc_sensor.read_temperature();
        constexpr double MIN_VALID_TEMP_C = -50.0;
        constexpr double MAX_VALID_TEMP_C = 150.0;
        condTemperatureValid = std::isfinite(t_cond) &&
            t_cond >= MIN_VALID_TEMP_C && t_cond <= MAX_VALID_TEMP_C;
        satTemperatureValid = std::isfinite(t_sat) &&
            t_sat >= MIN_VALID_TEMP_C && t_sat <= MAX_VALID_TEMP_C;
        opcTemperatureValid = std::isfinite(t_opc) &&
            t_opc >= MIN_VALID_TEMP_C && t_opc <= MAX_VALID_TEMP_C;
        double p_cond = 0.0;
        double p_sat = 0.0;
        double p_opc = 0.0;

        if (is_cond_running && !condTemperatureValid) {
            peltier_cond.set_duty_cycle(0.0);
            is_cond_running = false;
            cond_pid.reset();
            ui.lblCondPwm->setToolTip("冷凝段 PT100 读数无效或超出 -50～150 ℃，输出已关闭。");
        } else if (is_cond_running) {
            p_cond = cond_pid.compute(t_cond, 0.5);
            if (!peltier_cond.set_duty_cycle(p_cond)) {
                is_cond_running = false;
                p_cond = 0.0;
                ui.btnCondStart->setEnabled(false);
                ui.btnCondStop->setEnabled(false);
                ui.lblCondPwm->setToolTip(QString::fromStdString(peltier_cond.errorString()));
            }
        }
        if (is_sat_running && !satTemperatureValid) {
            heater_sat.set_duty_cycle(0.0);
            is_sat_running = false;
            sat_pid.reset();
            ui.lblSatPwm->setToolTip("饱和段 PT100 读数无效或超出 -50～150 ℃，输出已关闭。");
        } else if (is_sat_running) {
            p_sat = sat_pid.compute(t_sat, 0.5);
            if (!heater_sat.set_duty_cycle(p_sat)) {
                is_sat_running = false;
                p_sat = 0.0;
                ui.btnSatStart->setEnabled(false);
                ui.btnSatStop->setEnabled(false);
                ui.lblSatPwm->setToolTip(QString::fromStdString(heater_sat.errorString()));
            }
        }
        if (is_opc_heater_running) {
            if (!opcTemperatureValid) {
                const bool stopped = opc_heater.set_duty_cycle(0.0);
                if (!stopped) opcHeaterReady = false;
                is_opc_heater_running = false;
                opc_pid.reset();
                ui.btnOpcStart->setEnabled(false);
                ui.btnOpcStop->setEnabled(false);
                ui.lblOpcPwm->setToolTip(stopped
                    ? "OPC 段 PT100 读数无效或超出 -50～150 ℃，加热已自动关闭；读数恢复后请手动重新启动。"
                    : QString("OPC 段 PT100 读数无效或超出 -50～150 ℃，且 PWM 关闭失败：%1")
                        .arg(QString::fromStdString(opc_heater.errorString())));
            } else {
                p_opc = opc_pid.compute(t_opc, 0.5);
                if (!opc_heater.set_duty_cycle(p_opc)) {
                    const QString writeError = QString::fromStdString(opc_heater.errorString());
                    const bool stopped = opc_heater.set_duty_cycle(0.0);
                    opcHeaterReady = false;
                    is_opc_heater_running = false;
                    p_opc = 0.0;
                    opc_pid.reset();
                    ui.btnOpcStart->setEnabled(false);
                    ui.btnOpcStop->setEnabled(false);
                    ui.lblOpcPwm->setToolTip(stopped
                        ? writeError + "；加热输出已关闭。"
                        : writeError + "；随后关闭输出也失败：" +
                            QString::fromStdString(opc_heater.errorString()));
                }
            }
        }

        ui.lblCondTemp->setText(QString("当前: %1 ℃").arg(formatTemp(t_cond)));
        ui.lblCondPwm->setText(peltier_cond.isReady()
            ? QString("功率: %1 %").arg(p_cond, 0, 'f', 1)
            : QString("PWM 不可用"));
        ui.lblSatTemp->setText(QString("当前: %1 ℃").arg(formatTemp(t_sat)));
        ui.lblSatPwm->setText(heater_sat.isReady()
            ? QString("功率: %1 %").arg(p_sat, 0, 'f', 1)
            : QString("PWM 不可用"));
        ui.lblOpcTemp->setText(QString("当前: %1 ℃").arg(formatTemp(t_opc)));
        if (!opcHeaterReady) {
            ui.lblOpcPwm->setText("PWM 不可用");
        } else if (!opcTemperatureValid) {
            ui.lblOpcPwm->setText("温度异常 · 加热已关闭");
        } else {
            ui.lblOpcPwm->setText(QString("功率: %1 %").arg(p_opc, 0, 'f', 1));
        }
        constexpr double TEMPERATURE_NORMAL_TOLERANCE_C = 1.0;
        const bool condNormal = is_cond_running && condTemperatureValid &&
            std::abs(static_cast<double>(t_cond) - ui.sbCond->value()) <= TEMPERATURE_NORMAL_TOLERANCE_C;
        const bool satNormal = is_sat_running && satTemperatureValid &&
            std::abs(static_cast<double>(t_sat) - ui.sbSat->value()) <= TEMPERATURE_NORMAL_TOLERANCE_C;
        const bool opcNormal = is_opc_heater_running && opcTemperatureValid &&
            std::abs(static_cast<double>(t_opc) - ui.sbOpc->value()) <= TEMPERATURE_NORMAL_TOLERANCE_C;
        setTrafficLightState(ui.lblOverviewCondLamp, condNormal);
        setTrafficLightState(ui.lblOverviewSatLamp, satNormal);
        setTrafficLightState(ui.lblOverviewOpcLamp, opcNormal);

        auto setTemperatureToolTip = [&](QLabel *lamp, const QString& segment, float current, double target) {
            const QString details = QString("%1当前温度：%2 ℃\n目标温度：%3 ℃\n正常判定范围：目标温度 ±%4 ℃")
                .arg(segment)
                .arg(formatTemp(current))
                .arg(target, 0, 'f', 1)
                .arg(TEMPERATURE_NORMAL_TOLERANCE_C, 0, 'f', 1);
            lamp->setToolTip(details);
        };
        setTemperatureToolTip(ui.lblOverviewCondLamp, "冷凝段", t_cond, ui.sbCond->value());
        setTemperatureToolTip(ui.lblOverviewSatLamp, "饱和段", t_sat, ui.sbSat->value());
        setTemperatureToolTip(ui.lblOverviewOpcLamp, "OPC段", t_opc, ui.sbOpc->value());
        evaluateStartup();
        refreshTemperatureControls();
    });

    timer->start(500);
    window.showMaximized();

    int ret = app.exec();

    // aboutToQuit normally performs this before the event loop returns.  Keep
    // a final retry here for startup failures or an incomplete first attempt.
    if (!safetyShutdownComplete) {
        performSafetyShutdown(shutdownTrigger + QStringLiteral("（退出后复核）"));
    }

    if (liquidSystem) {
        delete liquidSystem;
        liquidSystem = nullptr;
    }
    proportional_valve.close();
    vacuum_pump.release();
    opc_heater.release();
    bypass_valve.release();
    daqWorker->stopDaq();
    if (!daqWorker->wait(6000)) {
        // D2XX 调用理论上会在读取超时后返回。若驱动异常导致超时仍未结束，
        // 退出阶段最后中止采集线程，避免整个程序永久卡在关闭流程。
        daqWorker->terminate();
        daqWorker->wait(1000);
    }
    if (!daqWorker->isRunning()) delete daqWorker;
    plotRefreshTimer->stop();
    delete plotRefreshTimer;
    if (gpio_handle >= 0) lgGpiochipClose(gpio_handle);

    if (shutdownRequested) {
        QProcess::execute("/usr/bin/systemctl", QStringList() << "poweroff");
    }
    return ret;
}
