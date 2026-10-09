#include "NetworkConfigManager.h"

#include <QAbstractSocket>
#include <QDebug>
#include <QHostAddress>
#include <QHash>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

namespace {

const char CPC_CONNECTION_NAME[] = "HTCPC-ETH0";
constexpr int NMCLI_QUERY_TIMEOUT_MS = 5000;
constexpr int NMCLI_ACTIVATE_TIMEOUT_MS = 20000;

QString profileField(const QHash<QString, QString> &fields, const QString &name) {
    return fields.value(name).trimmed();
}

} // namespace

NetworkConfigManager::NetworkConfigManager(QObject *parent)
    : QObject(parent),
      m_process(new QProcess(this)),
      m_commandTimer(new QTimer(this)) {
    qRegisterMetaType<NetworkConfigManager::NetworkStatus>(
        "NetworkConfigManager::NetworkStatus");
    m_commandTimer->setSingleShot(true);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    m_process->setProcessEnvironment(environment);

    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                const bool success = !m_commandTimedOut &&
                                     exitStatus == QProcess::NormalExit && exitCode == 0;
                finishCommand(success);
            });
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart && m_commandActive) {
                    finishCommand(false, m_process->errorString());
                }
            });
    connect(m_commandTimer, &QTimer::timeout, this, [this]() {
        if (!m_commandActive) return;
        m_commandTimedOut = true;
        m_process->kill();
    });
}

NetworkConfigManager::~NetworkConfigManager() {
    if (m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->kill();
    }
}

NetworkConfigManager::OperationState NetworkConfigManager::state() const {
    return m_state;
}

bool NetworkConfigManager::isBusy() const {
    return m_state != OperationState::Idle;
}

NetworkConfigManager::NetworkStatus NetworkConfigManager::currentStatus() const {
    return m_status;
}

void NetworkConfigManager::setState(OperationState state) {
    m_state = state;
}

void NetworkConfigManager::refresh() {
    if (isBusy()) return;

    setState(OperationState::Refreshing);
    m_status = readInterfaceStatus();
    if (!m_status.interfaceFound) {
        m_status.error = QStringLiteral("未检测到有线网卡");
        m_status.profileQueryComplete = true;
        setState(OperationState::Idle);
        emit statusUpdated(m_status);
        return;
    }

    if (QStandardPaths::findExecutable(QStringLiteral("nmcli")).isEmpty()) {
        m_status.error = QStringLiteral("未安装 nmcli，无法读取 NetworkManager 配置");
        m_status.profileQueryComplete = true;
        setState(OperationState::Idle);
        emit statusUpdated(m_status);
        return;
    }
    m_status.networkManagerAvailable = true;
    emit statusUpdated(m_status);

    runNmcli({QStringLiteral("-g"), QStringLiteral("GENERAL.CON-UUID"),
              QStringLiteral("device"), QStringLiteral("show"), m_status.interfaceName},
             NMCLI_QUERY_TIMEOUT_MS,
             [this](bool success, const QString &output, const QString &error) {
                 const QString activeUuid = success ? firstNonEmptyLine(output) : QString();
                 if (!success) m_status.error = error;
                 resolveReadableProfile(activeUuid == QStringLiteral("--")
                                            ? QString() : activeUuid);
             });
}

void NetworkConfigManager::resolveReadableProfile(const QString &activeUuid) {
    if (!activeUuid.isEmpty()) {
        readProfile(activeUuid,
                    [this](bool success, const ProfileSnapshot &profile,
                           const QString &error) {
                        if (success) {
                            m_status.profileFound = true;
                            m_status.profileName = profile.name;
                            m_status.profileUuid = profile.uuid;
                            m_status.configuredIpv4 = profile.config;
                            m_status.error.clear();
                        } else {
                            m_status.error = error;
                        }
                        m_status.profileQueryComplete = true;
                        setState(OperationState::Idle);
                        emit statusUpdated(m_status);
                    });
        return;
    }

    findDedicatedOrBoundProfile([this](const QString &uuid) {
        if (uuid.isEmpty()) {
            m_status.profileQueryComplete = true;
            setState(OperationState::Idle);
            emit statusUpdated(m_status);
            return;
        }
        readProfile(uuid,
                    [this](bool success, const ProfileSnapshot &profile,
                           const QString &error) {
                        if (success) {
                            m_status.profileFound = true;
                            m_status.profileName = profile.name;
                            m_status.profileUuid = profile.uuid;
                            m_status.configuredIpv4 = profile.config;
                            m_status.error.clear();
                        } else {
                            m_status.error = error;
                        }
                        m_status.profileQueryComplete = true;
                        setState(OperationState::Idle);
                        emit statusUpdated(m_status);
                    });
    });
}

void NetworkConfigManager::findDedicatedOrBoundProfile(const UuidCallback &callback) {
    runNmcli({QStringLiteral("-g"), QStringLiteral("connection.uuid"),
              QStringLiteral("connection"), QStringLiteral("show"),
              QString::fromLatin1(CPC_CONNECTION_NAME)},
             NMCLI_QUERY_TIMEOUT_MS,
             [this, callback](bool success, const QString &output, const QString &) {
                 if (success) {
                     const QString uuid = firstNonEmptyLine(output);
                     if (!uuid.isEmpty()) {
                         callback(uuid);
                         return;
                     }
                 }

                 runNmcli({QStringLiteral("-t"), QStringLiteral("--escape"),
                           QStringLiteral("no"), QStringLiteral("-f"),
                           QStringLiteral("UUID,TYPE"), QStringLiteral("connection"),
                           QStringLiteral("show")},
                          NMCLI_QUERY_TIMEOUT_MS,
                          [this, callback](bool listOk, const QString &listOutput,
                                           const QString &) {
                              QStringList candidates;
                              if (listOk) {
                                  const QStringList lines = listOutput.split(
                                      '\n', Qt::SkipEmptyParts);
                                  for (const QString &line : lines) {
                                      const int separator = line.indexOf(':');
                                      if (separator <= 0) continue;
                                      const QString type = line.mid(separator + 1).trimmed();
                                      if (type == QStringLiteral("802-3-ethernet") ||
                                          type == QStringLiteral("ethernet")) {
                                          candidates.append(line.left(separator).trimmed());
                                      }
                                  }
                              }
                              findBoundProfile(candidates, 0, callback);
                          });
             });
}

void NetworkConfigManager::findBoundProfile(const QStringList &candidateUuids,
                                            int index,
                                            const UuidCallback &callback) {
    if (index >= candidateUuids.size()) {
        callback(QString());
        return;
    }

    const QString uuid = candidateUuids.at(index);
    runNmcli({QStringLiteral("-g"), QStringLiteral("connection.interface-name"),
              QStringLiteral("connection"), QStringLiteral("show"), uuid},
             NMCLI_QUERY_TIMEOUT_MS,
             [this, candidateUuids, index, callback, uuid](
                 bool success, const QString &output, const QString &) {
                 if (success && firstNonEmptyLine(output) == m_status.interfaceName) {
                     callback(uuid);
                     return;
                 }
                 findBoundProfile(candidateUuids, index + 1, callback);
             });
}

void NetworkConfigManager::readProfile(const QString &uuidOrName,
                                       const ProfileCallback &callback) {
    const QString fields = QStringLiteral(
        "connection.id,connection.uuid,connection.interface-name,ipv4.method,"
        "ipv4.addresses,ipv4.gateway,ipv4.dns,ipv4.ignore-auto-dns");
    runNmcli({QStringLiteral("-t"), QStringLiteral("--escape"), QStringLiteral("yes"),
              QStringLiteral("-f"), fields, QStringLiteral("connection"),
              QStringLiteral("show"), uuidOrName},
             NMCLI_QUERY_TIMEOUT_MS,
             [callback](bool success, const QString &output, const QString &error) {
                 if (!success) {
                     callback(false, ProfileSnapshot(), error);
                     return;
                 }
                 ProfileSnapshot profile = parseProfile(output);
                 if (!profile.valid) {
                     callback(false, ProfileSnapshot(),
                              QStringLiteral("无法解析 NetworkManager 连接配置"));
                     return;
                 }
                 callback(true, profile, QString());
             });
}

void NetworkConfigManager::applyConfig(const Ipv4Config &config) {
    if (isBusy()) {
        emit operationFailed(QStringLiteral("网络操作正在进行，请稍候。"), false);
        return;
    }

    QString validationError;
    if (!validateConfig(config, &validationError)) {
        emit operationFailed(validationError, false);
        return;
    }

    m_status = readInterfaceStatus();
    if (!m_status.interfaceFound) {
        emit operationFailed(QStringLiteral("未检测到有线网卡，无法应用网络设置。"), false);
        return;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("nmcli")).isEmpty()) {
        emit operationFailed(QStringLiteral("未安装 nmcli，无法修改 NetworkManager 配置。"),
                             false);
        return;
    }

    m_requestedConfig = config;
    m_oldActiveProfile = ProfileSnapshot();
    m_oldDedicatedProfile = ProfileSnapshot();
    m_oldActiveUuid.clear();
    m_targetProfileId.clear();
    m_dedicatedProfileExisted = false;
    m_dedicatedProfileMayHaveBeenCreated = false;
    m_applyFailureReason.clear();
    m_rollbackSucceeded = true;
    m_rollbackProblems.clear();

    setState(OperationState::ReadingOldConfig);
    emit operationStarted();
    emit operationProgress(QStringLiteral("正在保存当前 NetworkManager 配置..."));
    readOldActiveProfile();
}

void NetworkConfigManager::readOldActiveProfile() {
    runNmcli({QStringLiteral("-g"), QStringLiteral("GENERAL.CON-UUID"),
              QStringLiteral("device"), QStringLiteral("show"), m_status.interfaceName},
             NMCLI_QUERY_TIMEOUT_MS,
             [this](bool success, const QString &output, const QString &error) {
                 if (!success) {
                     failBeforeModification(
                         QStringLiteral("读取当前活动连接失败：%1").arg(error));
                     return;
                 }
                 m_oldActiveUuid = firstNonEmptyLine(output);
                 if (m_oldActiveUuid == QStringLiteral("--")) m_oldActiveUuid.clear();
                 if (m_oldActiveUuid.isEmpty()) {
                     readDedicatedProfile();
                     return;
                 }
                 readProfile(m_oldActiveUuid,
                             [this](bool profileOk, const ProfileSnapshot &profile,
                                    const QString &profileError) {
                                 if (!profileOk) {
                                     failBeforeModification(
                                         QStringLiteral("保存当前活动连接失败：%1")
                                             .arg(profileError));
                                     return;
                                 }
                                 m_oldActiveProfile = profile;
                                 readDedicatedProfile();
                             });
             });
}

void NetworkConfigManager::readDedicatedProfile() {
    runNmcli({QStringLiteral("-g"), QStringLiteral("connection.uuid"),
              QStringLiteral("connection"), QStringLiteral("show"),
              QString::fromLatin1(CPC_CONNECTION_NAME)},
             NMCLI_QUERY_TIMEOUT_MS,
             [this](bool success, const QString &output, const QString &) {
                 if (!success || firstNonEmptyLine(output).isEmpty()) {
                     m_dedicatedProfileExisted = false;
                     applyDedicatedProfile();
                     return;
                 }

                 m_dedicatedProfileExisted = true;
                 const QString uuid = firstNonEmptyLine(output);
                 readProfile(uuid,
                             [this](bool profileOk, const ProfileSnapshot &profile,
                                    const QString &error) {
                                 if (!profileOk) {
                                     failBeforeModification(
                                         QStringLiteral("保存 HTCPC-ETH0 原配置失败：%1")
                                             .arg(error));
                                     return;
                                 }
                                 m_oldDedicatedProfile = profile;
                                 m_targetProfileId = profile.uuid;
                                 applyDedicatedProfile();
                             });
             });
}

void NetworkConfigManager::applyDedicatedProfile() {
    setState(OperationState::Applying);
    emit operationProgress(m_dedicatedProfileExisted
        ? QStringLiteral("正在更新 HTCPC-ETH0 配置...")
        : QStringLiteral("正在创建 HTCPC-ETH0 专用连接..."));

    QStringList arguments;
    if (m_dedicatedProfileExisted) {
        arguments << QStringLiteral("connection") << QStringLiteral("modify")
                  << m_targetProfileId
                  << QStringLiteral("connection.interface-name") << m_status.interfaceName;
    } else {
        m_targetProfileId = QString::fromLatin1(CPC_CONNECTION_NAME);
        arguments << QStringLiteral("connection") << QStringLiteral("add")
                  << QStringLiteral("type") << QStringLiteral("ethernet")
                  << QStringLiteral("ifname") << m_status.interfaceName
                  << QStringLiteral("con-name") << QString::fromLatin1(CPC_CONNECTION_NAME);
    }
    arguments << nmcliIpv4Arguments(m_requestedConfig);

    runNmcli(arguments, NMCLI_QUERY_TIMEOUT_MS,
             [this](bool success, const QString &, const QString &error) {
                 if (!success) {
                     beginRollback(QStringLiteral("写入 NetworkManager 配置失败：%1")
                                       .arg(error));
                     return;
                 }
                 if (!m_dedicatedProfileExisted) {
                     m_dedicatedProfileMayHaveBeenCreated = true;
                 }
                 reactivateDedicatedProfile();
             });
}

void NetworkConfigManager::reactivateDedicatedProfile() {
    setState(OperationState::Reactivating);
    emit operationProgress(QStringLiteral("正在重新激活 %1...").arg(m_status.interfaceName));
    runNmcli({QStringLiteral("--wait"), QStringLiteral("15"),
              QStringLiteral("connection"), QStringLiteral("up"), m_targetProfileId,
              QStringLiteral("ifname"), m_status.interfaceName},
             NMCLI_ACTIVATE_TIMEOUT_MS,
             [this](bool success, const QString &, const QString &error) {
                 if (!success) {
                     beginRollback(QStringLiteral("重新激活有线连接失败：%1").arg(error));
                     return;
                 }
                 verifyAppliedProfile();
             });
}

void NetworkConfigManager::verifyAppliedProfile() {
    setState(OperationState::Verifying);
    emit operationProgress(QStringLiteral("正在验证新网络配置..."));
    readProfile(m_targetProfileId,
                [this](bool success, const ProfileSnapshot &profile,
                       const QString &error) {
                    if (!success) {
                        beginRollback(QStringLiteral("读取应用后的配置失败：%1").arg(error));
                        return;
                    }
                    const bool modeMatches = profile.config.mode == m_requestedConfig.mode;
                    if (!modeMatches) {
                        beginRollback(QStringLiteral("NetworkManager 未保存所选 IPv4 模式。"));
                        return;
                    }
                    if (m_requestedConfig.mode == Ipv4Mode::Dhcp) {
                        completeApplySuccess();
                        return;
                    }
                    if (profile.config.address != m_requestedConfig.address ||
                        profile.config.prefixLength != m_requestedConfig.prefixLength ||
                        profile.config.gateway != m_requestedConfig.gateway ||
                        profile.config.dns != m_requestedConfig.dns) {
                        beginRollback(QStringLiteral("NetworkManager 保存后的静态参数与输入不一致。"));
                        return;
                    }
                    verifyStaticAddress(6);
                });
}

void NetworkConfigManager::verifyStaticAddress(int attemptsRemaining) {
    if (interfaceHasIpv4(m_status.interfaceName, m_requestedConfig.address)) {
        completeApplySuccess();
        return;
    }
    if (attemptsRemaining <= 0) {
        beginRollback(QStringLiteral("有线网卡实际 IPv4 未切换为 %1。")
                          .arg(m_requestedConfig.address));
        return;
    }
    emit operationProgress(QStringLiteral("等待有线网卡获得新地址..."));
    QTimer::singleShot(800, this, [this, attemptsRemaining]() {
        verifyStaticAddress(attemptsRemaining - 1);
    });
}

void NetworkConfigManager::completeApplySuccess() {
    const NetworkStatus live = readInterfaceStatus();
    const QString message = m_requestedConfig.mode == Ipv4Mode::Dhcp &&
                            live.actualIpv4.isEmpty()
        ? QStringLiteral("DHCP 已启用，当前尚未获取 IPv4 地址。")
        : QStringLiteral("网络设置已应用。当前 IPv4：%1")
              .arg(live.actualIpv4.isEmpty() ? QStringLiteral("尚未获取")
                                             : live.actualIpv4);
    setState(OperationState::Idle);
    emit operationSucceeded(message);
    refresh();
}

void NetworkConfigManager::failBeforeModification(const QString &error) {
    setState(OperationState::Idle);
    emit operationFailed(QStringLiteral("网络设置应用失败，原配置未更改。%1").arg(error),
                         true);
    refresh();
}

void NetworkConfigManager::beginRollback(const QString &reason) {
    if (m_state == OperationState::RollingBack) return;
    m_applyFailureReason = reason;
    setState(OperationState::RollingBack);
    emit operationProgress(QStringLiteral("应用失败，正在恢复原网络配置..."));
    restoreOrDeleteDedicatedProfile();
}

void NetworkConfigManager::restoreOrDeleteDedicatedProfile() {
    if (m_dedicatedProfileExisted) {
        QStringList arguments;
        arguments << QStringLiteral("connection") << QStringLiteral("modify")
                  << m_oldDedicatedProfile.uuid
                  << buildRestoreArguments(m_oldDedicatedProfile);
        runNmcli(arguments, NMCLI_QUERY_TIMEOUT_MS,
                 [this](bool success, const QString &, const QString &error) {
                     if (!success) {
                         m_rollbackSucceeded = false;
                         m_rollbackProblems << QStringLiteral("恢复 HTCPC-ETH0 参数失败：%1")
                                                   .arg(error);
                     }
                     reactivateOldProfile();
                 });
        return;
    }

    if (m_dedicatedProfileMayHaveBeenCreated) {
        runNmcli({QStringLiteral("connection"), QStringLiteral("delete"),
                  QString::fromLatin1(CPC_CONNECTION_NAME)},
                 NMCLI_QUERY_TIMEOUT_MS,
                 [this](bool success, const QString &, const QString &error) {
                     const bool profileWasAlreadyAbsent =
                         error.contains(QStringLiteral("unknown connection"),
                                        Qt::CaseInsensitive) ||
                         error.contains(QStringLiteral("not found"), Qt::CaseInsensitive);
                     if (!success && !profileWasAlreadyAbsent) {
                         m_rollbackSucceeded = false;
                         m_rollbackProblems << QStringLiteral("删除新建 HTCPC-ETH0 失败：%1")
                                                   .arg(error);
                     }
                     reactivateOldProfile();
                 });
        return;
    }
    reactivateOldProfile();
}

void NetworkConfigManager::reactivateOldProfile() {
    if (!m_oldActiveUuid.isEmpty()) {
        runNmcli({QStringLiteral("--wait"), QStringLiteral("15"),
                  QStringLiteral("connection"), QStringLiteral("up"), m_oldActiveUuid,
                  QStringLiteral("ifname"), m_status.interfaceName},
                 NMCLI_ACTIVATE_TIMEOUT_MS,
                 [this](bool success, const QString &, const QString &error) {
                     if (!success) {
                         m_rollbackSucceeded = false;
                         m_rollbackProblems << QStringLiteral("重新激活原连接失败：%1")
                                                   .arg(error);
                     }
                     completeRollback();
                 });
        return;
    }

    if (m_dedicatedProfileExisted) {
        runNmcli({QStringLiteral("connection"), QStringLiteral("down"),
                  m_oldDedicatedProfile.uuid},
                 NMCLI_QUERY_TIMEOUT_MS,
                 [this](bool success, const QString &, const QString &error) {
                     if (!success) {
                         m_rollbackSucceeded = false;
                         m_rollbackProblems << QStringLiteral("恢复原断开状态失败：%1")
                                                   .arg(error);
                     }
                     completeRollback();
                 });
        return;
    }
    completeRollback();
}

void NetworkConfigManager::completeRollback() {
    QString message;
    if (m_rollbackSucceeded) {
        message = QStringLiteral("网络设置应用失败，已恢复原配置。%1")
                      .arg(m_applyFailureReason);
        qWarning().noquote() << message;
    } else {
        message = QStringLiteral("网络配置异常，自动恢复失败。请检查 NetworkManager 配置。%1；%2")
                      .arg(m_applyFailureReason,
                           m_rollbackProblems.join(QStringLiteral("；")));
        qCritical().noquote() << message;
    }
    setState(OperationState::Idle);
    emit operationFailed(message, m_rollbackSucceeded);
    refresh();
}

void NetworkConfigManager::runNmcli(const QStringList &arguments,
                                    int timeoutMs,
                                    const CommandCallback &callback) {
    if (m_commandActive) {
        callback(false, QString(), QStringLiteral("内部错误：nmcli 命令重叠"));
        return;
    }
    m_commandCallback = callback;
    m_commandActive = true;
    m_commandTimedOut = false;
    m_process->setProgram(QStringLiteral("nmcli"));
    m_process->setArguments(arguments);
    m_process->start();
    m_commandTimer->start(timeoutMs);
}

void NetworkConfigManager::finishCommand(bool success, const QString &forcedError) {
    if (!m_commandActive) return;
    m_commandTimer->stop();
    const QString output = QString::fromUtf8(m_process->readAllStandardOutput()).trimmed();
    QString error = forcedError.trimmed();
    const QString standardError = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
    if (error.isEmpty()) error = standardError;
    if (!success && error.isEmpty()) {
        error = m_commandTimedOut ? QStringLiteral("nmcli 操作超时")
                                  : m_process->errorString();
    }
    const CommandCallback callback = m_commandCallback;
    m_commandCallback = CommandCallback();
    m_commandActive = false;
    m_commandTimedOut = false;
    callback(success, output, error);
}

void NetworkConfigManager::failRefresh(const QString &error) {
    m_status.error = error;
    setState(OperationState::Idle);
    emit statusUpdated(m_status);
}

NetworkConfigManager::NetworkStatus NetworkConfigManager::readInterfaceStatus() const {
    NetworkStatus status;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    QNetworkInterface selected;
    for (const QNetworkInterface &networkInterface : interfaces) {
        if (networkInterface.name() == QStringLiteral("eth0")) {
            selected = networkInterface;
            break;
        }
    }
    if (!selected.isValid()) {
        for (const QNetworkInterface &networkInterface : interfaces) {
            if (networkInterface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
            if (networkInterface.type() == QNetworkInterface::Ethernet) {
                selected = networkInterface;
                break;
            }
        }
    }
    if (!selected.isValid()) return status;

    status.interfaceFound = true;
    status.interfaceName = selected.name();
    const QNetworkInterface::InterfaceFlags flags = selected.flags();
    status.linkUp = flags.testFlag(QNetworkInterface::IsUp);
    status.linkRunning = flags.testFlag(QNetworkInterface::IsRunning);
    status.actualIpv4 = firstIpv4ForInterface(selected.name());
    return status;
}

QString NetworkConfigManager::firstIpv4ForInterface(const QString &interfaceName,
                                                     const QString &preferredAddress) {
    const QNetworkInterface networkInterface =
        QNetworkInterface::interfaceFromName(interfaceName);
    if (!networkInterface.isValid()) return QString();
    QString first;
    for (const QNetworkAddressEntry &entry : networkInterface.addressEntries()) {
        const QHostAddress address = entry.ip();
        if (address.protocol() != QAbstractSocket::IPv4Protocol ||
            address.isNull() || address.isLoopback()) {
            continue;
        }
        const QString text = address.toString();
        if (text == preferredAddress) return text;
        if (first.isEmpty()) first = text;
    }
    return first;
}

bool NetworkConfigManager::interfaceHasIpv4(const QString &interfaceName,
                                            const QString &address) {
    return firstIpv4ForInterface(interfaceName, address) == address;
}

bool NetworkConfigManager::parseIpv4(const QString &text, quint32 *value) {
    const QStringList parts = text.split('.');
    if (parts.size() != 4) return false;
    quint32 result = 0;
    for (const QString &part : parts) {
        if (part.isEmpty()) return false;
        for (const QChar character : part) {
            if (!character.isDigit()) return false;
        }
        bool ok = false;
        const uint octet = part.toUInt(&ok);
        if (!ok || octet > 255U) return false;
        result = (result << 8) | octet;
    }
    if (value) *value = result;
    return true;
}

bool NetworkConfigManager::validateConfig(const Ipv4Config &config,
                                          QString *errorMessage) {
    auto fail = [errorMessage](const QString &message) {
        if (errorMessage) *errorMessage = message;
        return false;
    };
    if (config.mode == Ipv4Mode::Dhcp) return true;
    if (config.prefixLength < 1 || config.prefixLength > 32) {
        return fail(QStringLiteral("前缀长度必须在 1～32 之间。"));
    }

    quint32 address = 0;
    if (!parseIpv4(config.address, &address)) {
        return fail(QStringLiteral("IPv4 地址格式不正确。"));
    }
    const quint32 firstOctet = address >> 24;
    if (address == 0U || address == 0xFFFFFFFFU || firstOctet == 127U ||
        firstOctet == 0U || firstOctet >= 224U) {
        return fail(QStringLiteral("该 IPv4 地址不能用作本机静态地址。"));
    }

    const quint32 mask = config.prefixLength == 32
        ? 0xFFFFFFFFU
        : (0xFFFFFFFFU << (32 - config.prefixLength));
    const quint32 network = address & mask;
    const quint32 broadcast = network | ~mask;
    if (address == network || address == broadcast) {
        return fail(QStringLiteral("本机地址不能等于当前子网的网络地址或广播地址。"));
    }

    if (!config.gateway.isEmpty()) {
        quint32 gateway = 0;
        if (!parseIpv4(config.gateway, &gateway)) {
            return fail(QStringLiteral("网关 IPv4 格式不正确。"));
        }
        if ((gateway & mask) != network) {
            return fail(QStringLiteral("网关必须与本机静态地址处于同一子网。"));
        }
        if (gateway == address || gateway == network || gateway == broadcast) {
            return fail(QStringLiteral("网关必须是同一子网内的其他有效主机地址。"));
        }
    }

    if (!config.dns.isEmpty() && !parseIpv4(config.dns)) {
        return fail(QStringLiteral("DNS IPv4 格式不正确。"));
    }
    return true;
}

QString NetworkConfigManager::netmaskForPrefix(int prefixLength) {
    if (prefixLength < 0 || prefixLength > 32) return QStringLiteral("--");
    const quint32 mask = prefixLength == 0
        ? 0U
        : (prefixLength == 32 ? 0xFFFFFFFFU
                              : (0xFFFFFFFFU << (32 - prefixLength)));
    return QStringLiteral("%1.%2.%3.%4")
        .arg((mask >> 24) & 0xFFU)
        .arg((mask >> 16) & 0xFFU)
        .arg((mask >> 8) & 0xFFU)
        .arg(mask & 0xFFU);
}

QString NetworkConfigManager::recommendedIpcAddress(const Ipv4Config &config) {
    quint32 address = 0;
    if (config.mode != Ipv4Mode::Static || config.prefixLength != 24 ||
        !parseIpv4(config.address, &address) ||
        (address & 0xFFFFFF00U) != 0xC0A83200U) {
        return QStringLiteral("请将工控机设置为同一子网内的其他可用地址");
    }
    const QString candidate = config.address == QStringLiteral("192.168.50.1")
        ? QStringLiteral("192.168.50.2/24")
        : QStringLiteral("192.168.50.1/24");
    return candidate;
}

QString NetworkConfigManager::unescapeNmcli(const QString &text) {
    QString result;
    bool escaped = false;
    for (const QChar character : text) {
        if (escaped) {
            result.append(character);
            escaped = false;
        } else if (character == QLatin1Char('\\')) {
            escaped = true;
        } else {
            result.append(character);
        }
    }
    if (escaped) result.append(QLatin1Char('\\'));
    return result;
}

NetworkConfigManager::ProfileSnapshot
NetworkConfigManager::parseProfile(const QString &output) {
    QHash<QString, QString> fields;
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        bool escaped = false;
        int separator = -1;
        for (int i = 0; i < line.size(); ++i) {
            if (!escaped && line.at(i) == QLatin1Char(':')) {
                separator = i;
                break;
            }
            if (!escaped && line.at(i) == QLatin1Char('\\')) escaped = true;
            else escaped = false;
        }
        if (separator <= 0) continue;
        fields.insert(line.left(separator), unescapeNmcli(line.mid(separator + 1)));
    }

    ProfileSnapshot profile;
    profile.name = profileField(fields, QStringLiteral("connection.id"));
    profile.uuid = profileField(fields, QStringLiteral("connection.uuid"));
    profile.interfaceName = profileField(fields, QStringLiteral("connection.interface-name"));
    profile.method = profileField(fields, QStringLiteral("ipv4.method"));
    profile.addresses = profileField(fields, QStringLiteral("ipv4.addresses"));
    profile.gateway = profileField(fields, QStringLiteral("ipv4.gateway"));
    profile.dns = profileField(fields, QStringLiteral("ipv4.dns"));
    profile.ignoreAutoDns = profileField(fields, QStringLiteral("ipv4.ignore-auto-dns"));
    profile.valid = !profile.uuid.isEmpty() && !profile.method.isEmpty();
    profile.config.mode = profile.method == QStringLiteral("manual")
        ? Ipv4Mode::Static : Ipv4Mode::Dhcp;
    profile.config.gateway = profile.gateway;
    profile.config.dns = profile.dns.section(',', 0, 0).trimmed();
    const QString firstAddress = profile.addresses.section(',', 0, 0).trimmed();
    profile.config.address = firstAddress.section('/', 0, 0);
    bool prefixOk = false;
    const int prefix = firstAddress.section('/', 1, 1).toInt(&prefixOk);
    profile.config.prefixLength = prefixOk ? prefix : 24;
    return profile;
}

QStringList NetworkConfigManager::nmcliIpv4Arguments(const Ipv4Config &config) {
    QStringList arguments;
    if (config.mode == Ipv4Mode::Dhcp) {
        arguments << QStringLiteral("ipv4.method") << QStringLiteral("auto")
                  << QStringLiteral("ipv4.addresses") << QString()
                  << QStringLiteral("ipv4.gateway") << QString()
                  << QStringLiteral("ipv4.dns") << QString()
                  << QStringLiteral("ipv4.ignore-auto-dns") << QStringLiteral("no");
    } else {
        arguments << QStringLiteral("ipv4.method") << QStringLiteral("manual")
                  << QStringLiteral("ipv4.addresses")
                  << QStringLiteral("%1/%2").arg(config.address).arg(config.prefixLength)
                  << QStringLiteral("ipv4.gateway") << config.gateway
                  << QStringLiteral("ipv4.dns") << config.dns
                  << QStringLiteral("ipv4.ignore-auto-dns") << QStringLiteral("yes");
    }
    return arguments;
}

QStringList NetworkConfigManager::buildRestoreArguments(const ProfileSnapshot &profile) {
    QStringList arguments;
    arguments << QStringLiteral("connection.interface-name") << profile.interfaceName
              << QStringLiteral("ipv4.method") << profile.method
              << QStringLiteral("ipv4.addresses") << profile.addresses
              << QStringLiteral("ipv4.gateway") << profile.gateway
              << QStringLiteral("ipv4.dns") << profile.dns
              << QStringLiteral("ipv4.ignore-auto-dns") << profile.ignoreAutoDns;
    return arguments;
}

QString NetworkConfigManager::firstNonEmptyLine(const QString &output) {
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (!line.trimmed().isEmpty()) return line.trimmed();
    }
    return QString();
}
