#include "LiquidControlSystem.h"

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
    monitorTimer->stop();
    stopRefill("系统停止监控");
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

void LiquidControlSystem::stopRefill(const QString& reason) {
    // 无论内部状态如何都写低电平，保证停止监控和异常路径也会关阀。
    int closeResult = inletClaimed ? lgGpioWrite(h, p_inlet, 0) : -1;
    if (isRefilling) {
        isRefilling = false;
        refillTimeoutTimer->stop();
        if (closeResult >= 0) {
            emit statusMessage(QString("🛑 [停止补液] 进液阀 GPIO%1 已关闭。原因: %2")
                                   .arg(p_inlet)
                                   .arg(reason));
        } else {
            emit alertMessage(QString("进液阀 GPIO%1 关闭失败（错误码 %2），请立即切断阀门电源。")
                                  .arg(p_inlet)
                                  .arg(closeResult));
        }
    }
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
    if (isRefilling) stopRefill("被手动强排中断");
    lgGpioWrite(h, p_inlet, 0); // 互锁：排液前确保进液阀关闭
    
    // 1. 尝试写入排液阀，并捕获内核返回的错误码
    int err_valve = lgGpioWrite(h, p_outlet, 1); 
    
    if (err_valve < 0) {
        // 如果写入失败，立刻在界面上爆红报警，绝不静默忽略！
        emit statusMessage(QString("❌ 严重失败：排液阀(GPIO %1)无法控制！错误码: %2").arg(p_outlet).arg(err_valve));
        emit alertMessage(QString("警告：排液阀引脚被系统后台进程死锁 (错误码 %1)，无法打开电磁阀！\n请在终端运行 sudo killall python3 来释放引脚。").arg(err_valve));
        isDraining = false;
        return;
    } else {
        isDraining = true;
        emit statusMessage("⚠️ [手动排液] 开始强排废液，已打开排液阀...");
    }
    
}
void LiquidControlSystem::stopManualDrain() {
    lgGpioWrite(h, p_outlet, 0);
    isDraining = false;
    emit statusMessage("✅ [手动排液] 排液阀已关闭。");
}
