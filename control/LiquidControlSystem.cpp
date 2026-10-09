#include "control/LiquidControlSystem.h"

#include <QStringList>

LiquidControlSystem::LiquidControlSystem(int lgpio_handle, int sensor_pin, int inlet_pin, int outlet_pin, QObject *parent)
    : QObject(parent), h(lgpio_handle), p_sensor(sensor_pin), p_inlet(inlet_pin), p_outlet(outlet_pin),
      isRefilling(false), isDraining(false), hasLastLevelState(false), lastHasLiquid(false),
      refillFaultLatched(false), sensorClaimed(false), inletClaimed(false), outletClaimed(false),
      hardwareReady(false), consecutiveLowSamples(0)
{
    // 1. 硬件引脚初始化 (采用内部下拉，适配分压直接入主板的接法)
    sensorClaimed = h >= 0 && lgGpioClaimInput(h, LG_SET_PULL_DOWN, p_sensor) >= 0;

    // 初始化输出，默认状态为 0 (关闭)
    inletClaimed = h >= 0 && lgGpioClaimOutput(h, 0, p_inlet, 0) >= 0;
    outletClaimed = h >= 0 && lgGpioClaimOutput(h, 0, p_outlet, 0) >= 0;

    QStringList claimErrors;
    if (!sensorClaimed) claimErrors << QString("液位传感器 GPIO%1 初始化失败").arg(p_sensor);
    if (!inletClaimed) claimErrors << QString("进液阀 GPIO%1 初始化失败").arg(p_inlet);
    if (!outletClaimed) claimErrors << QString("排液阀 GPIO%1 初始化失败").arg(p_outlet);
    hardwareReady = claimErrors.isEmpty();
    hardwareError = claimErrors.join("；");

    // 2. 定时器初始化
    monitorTimer = new QTimer(this);
    connect(monitorTimer, &QTimer::timeout, this, &LiquidControlSystem::checkLiquidLevel);

    refillTimeoutTimer = new QTimer(this);
    refillTimeoutTimer->setSingleShot(true); // 单次触发
    connect(refillTimeoutTimer, &QTimer::timeout, this, &LiquidControlSystem::onRefillTimeout);
}

LiquidControlSystem::~LiquidControlSystem() {
    stopMonitoring();
    if (inletClaimed) {
        lgGpioWrite(h, p_inlet, 0);
        lgGpioFree(h, p_inlet);
    }
    if (outletClaimed) {
        lgGpioWrite(h, p_outlet, 0);
        lgGpioFree(h, p_outlet);
    }
    if (sensorClaimed) lgGpioFree(h, p_sensor);
}

bool LiquidControlSystem::isReady() const {
    return hardwareReady;
}

QString LiquidControlSystem::errorString() const {
    return hardwareError;
}

void LiquidControlSystem::startMonitoring() {
    if (!hardwareReady) {
        emit alertMessage(QString("液位控制不可用：%1").arg(hardwareError));
        return;
    }

    monitorTimer->start(500); // 每500ms检查一次液位
    hasLastLevelState = false;
    consecutiveLowSamples = 0;
    refillFaultLatched = false;
    emit statusMessage("液位监控已启动，正在检测传感器...");
    checkLiquidLevel();
}

void LiquidControlSystem::stopMonitoring() {
    safeStop(nullptr);
}

bool LiquidControlSystem::safeStop(QString *error) {
    monitorTimer->stop();
    refillTimeoutTimer->stop();

    QStringList failures;
    const int inletResult = inletClaimed ? lgGpioWrite(h, p_inlet, 0) : -1;
    const int outletResult = outletClaimed ? lgGpioWrite(h, p_outlet, 0) : -1;
    if (inletResult < 0) {
        failures << QString("进液阀 GPIO%1 关闭失败（错误码 %2）")
                        .arg(p_inlet)
                        .arg(inletResult);
    }
    if (outletResult < 0) {
        failures << QString("排液阀 GPIO%1 关闭失败（错误码 %2）")
                        .arg(p_outlet)
                        .arg(outletResult);
    }

    if (inletResult >= 0) isRefilling = false;
    if (outletResult >= 0) isDraining = false;
    if (error) *error = failures.join("；");
    return failures.isEmpty();
}

void LiquidControlSystem::checkLiquidLevel() {
    if (isDraining) return; // 手动排液时，暂停自动补液逻辑

    int level = lgGpioRead(h, p_sensor);
    if (level < 0) {
        monitorTimer->stop();
        stopRefill("液位传感器读取失败");
        emit alertMessage(QString("液位传感器 GPIO%1 读取失败（错误码 %2），已停止自动补液。")
                              .arg(p_sensor)
                              .arg(level));
        return;
    }

    bool hasLiquid = (level == 1); // 1为有液满，0为缺液

    if (!hasLastLevelState || hasLiquid != lastHasLiquid) {
        emit statusMessage(hasLiquid ? "液位正常" : "缺液");
        lastHasLiquid = hasLiquid;
        hasLastLevelState = true;
    }

    if (hasLiquid) {
        consecutiveLowSamples = 0;
        if (isRefilling) stopRefill("液位恢复正常");
        refillFaultLatched = false;
        return;
    }

    if (consecutiveLowSamples < LOW_LEVEL_CONFIRM_SAMPLES) {
        ++consecutiveLowSamples;
    }
    if (consecutiveLowSamples >= LOW_LEVEL_CONFIRM_SAMPLES &&
        !isRefilling && !refillFaultLatched) {
        startRefill();
    }
}

void LiquidControlSystem::startRefill() {
    if (isDraining) {
        emit statusMessage("手动排液中，自动补液请求已忽略。");
        return;
    }

    // 互锁：只有确认排液阀已关闭后才允许打开进液阀。
    int outletResult = lgGpioWrite(h, p_outlet, 0);
    if (outletResult < 0) {
        refillFaultLatched = true;
        emit alertMessage(QString("排液阀 GPIO%1 无法关闭（错误码 %2），已禁止自动补液。")
                              .arg(p_outlet)
                              .arg(outletResult));
        return;
    }

    int inletResult = lgGpioWrite(h, p_inlet, 1); // GPIO22 高电平打开进液阀
    if (inletResult < 0) {
        refillFaultLatched = true;
        lgGpioWrite(h, p_inlet, 0);
        emit alertMessage(QString("进液阀 GPIO%1 无法打开（错误码 %2），自动补液已停止。")
                              .arg(p_inlet)
                              .arg(inletResult));
        return;
    }

    isRefilling = true;
    refillTimeoutTimer->start(10000); // 启动 10 秒超时看门狗
    emit statusMessage(QString("💧 [自动补液] 确认缺液，已打开进液阀 GPIO%1！").arg(p_inlet));
}

bool LiquidControlSystem::stopRefill(const QString& reason) {
    // 无论内部状态如何都写低电平，保证停止监控和异常路径也会关阀。
    int closeResult = inletClaimed ? lgGpioWrite(h, p_inlet, 0) : -1;
    if (isRefilling) {
        refillTimeoutTimer->stop();
        if (closeResult >= 0) {
            isRefilling = false;
            emit statusMessage(QString("🛑 [停止补液] 进液阀 GPIO%1 已关闭。原因: %2")
                                   .arg(p_inlet)
                                   .arg(reason));
        } else {
            emit alertMessage(QString("进液阀 GPIO%1 关闭失败（错误码 %2），请立即切断阀门电源。")
                                  .arg(p_inlet)
                                  .arg(closeResult));
        }
    }
    return closeResult >= 0;
}

void LiquidControlSystem::onRefillTimeout() {
    // 超时后锁定，避免液位仍低时监控定时器反复重启进液阀。
    refillFaultLatched = true;
    stopRefill("🚨 补液超时强制停止");
    emit alertMessage("严重警告：补液已持续10秒，系统已强制停止。\n"
                      "请检查储液罐是否耗尽，或补液管路是否堵塞。");
}

// ---------------- 手动排液逻辑 (包含非阻塞延时) ----------------
void LiquidControlSystem::startManualDrain() {
    if (!hardwareReady) {
        emit alertMessage(QString("液位控制不可用：%1").arg(hardwareError));
        return;
    }
    if (isRefilling && !stopRefill("被手动排液中断")) {
        emit alertMessage(QString("严重错误：进液阀 GPIO%1 无法确认关闭，已禁止打开排液阀。")
                              .arg(p_inlet));
        return;
    }

    QString interlockError;
    const bool opened = closeInletThenOpenOutlet(
        [&](int pin, int level) { return lgGpioWrite(h, pin, level); },
        p_inlet,
        p_outlet,
        &interlockError);
    if (!opened) {
        emit statusMessage(QString("❌ 严重失败：手动排液互锁失败：%1").arg(interlockError));
        emit alertMessage(QString("%1\nGPIO 操作失败，可能被其他进程或服务占用。"
                                  "\n请检查 GPIO 占用和相关后台进程。")
                              .arg(interlockError));
        isDraining = false;
        return;
    }
    isDraining = true;
    emit statusMessage("⚠️ [手动排液] 已确认进液阀关闭，开始排液。");
}

void LiquidControlSystem::stopManualDrain() {
    const int result = outletClaimed ? lgGpioWrite(h, p_outlet, 0) : -1;
    if (result < 0) {
        emit statusMessage(QString("❌ 排液阀 GPIO%1 关闭失败（错误码 %2）。")
                               .arg(p_outlet).arg(result));
        emit alertMessage(QString("严重错误：排液阀 GPIO%1 无法确认关闭（错误码 %2）。"
                                  "\n请立即切断阀门电源并检查 GPIO 占用。")
                              .arg(p_outlet).arg(result));
        return;
    }
    isDraining = false;
    emit statusMessage("✅ [手动排液] 排液阀已确认关闭。");
}

bool LiquidControlSystem::closeInletThenOpenOutlet(
    const std::function<int(int, int)>& writePin,
    int inletPin,
    int outletPin,
    QString *error) {
    const int inletResult = writePin(inletPin, 0);
    if (inletResult < 0) {
        if (error) {
            *error = QString("进液阀 GPIO%1 关闭失败（错误码 %2），排液阀未打开")
                         .arg(inletPin).arg(inletResult);
        }
        return false;
    }
    const int outletResult = writePin(outletPin, 1);
    if (outletResult < 0) {
        if (error) {
            *error = QString("排液阀 GPIO%1 打开失败（错误码 %2）")
                         .arg(outletPin).arg(outletResult);
        }
        return false;
    }
    if (error) error->clear();
    return true;
}
