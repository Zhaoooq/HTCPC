#ifndef CPC_NETWORK_NETWORKCONFIGMANAGER_H
#define CPC_NETWORK_NETWORKCONFIGMANAGER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;

class NetworkConfigManager final : public QObject {
    Q_OBJECT

public:
    enum class Ipv4Mode {
        Dhcp,
        Static
    };
    Q_ENUM(Ipv4Mode)

    enum class OperationState {
        Idle,
        Refreshing,
        ReadingOldConfig,
        Applying,
        Reactivating,
        Verifying,
        RollingBack
    };
    Q_ENUM(OperationState)

    struct Ipv4Config {
        Ipv4Mode mode = Ipv4Mode::Dhcp;
        QString address;
        int prefixLength = 24;
        QString gateway;
        QString dns;
    };

    struct NetworkStatus {
        QString interfaceName;
        bool interfaceFound = false;
        bool linkUp = false;
        bool linkRunning = false;
        QString actualIpv4;
        bool networkManagerAvailable = false;
        bool profileQueryComplete = false;
        bool profileFound = false;
        QString profileName;
        QString profileUuid;
        Ipv4Config configuredIpv4;
        QString error;
    };

    explicit NetworkConfigManager(QObject *parent = nullptr);
    ~NetworkConfigManager() override;

    OperationState state() const;
    bool isBusy() const;
    NetworkStatus currentStatus() const;

    void refresh();
    void applyConfig(const Ipv4Config &config);

    static bool validateConfig(const Ipv4Config &config, QString *errorMessage = nullptr);
    static QString netmaskForPrefix(int prefixLength);
    static QString recommendedIpcAddress(const Ipv4Config &config);
    static QStringList nmcliIpv4Arguments(const Ipv4Config &config);

signals:
    void statusUpdated(const NetworkConfigManager::NetworkStatus &status);
    void operationStarted();
    void operationProgress(const QString &message);
    void operationSucceeded(const QString &message);
    void operationFailed(const QString &message, bool rollbackSucceeded);

private:
    struct ProfileSnapshot {
        bool valid = false;
        QString name;
        QString uuid;
        QString interfaceName;
        QString method;
        QString addresses;
        QString gateway;
        QString dns;
        QString ignoreAutoDns;
        Ipv4Config config;
    };

    using CommandCallback = std::function<void(bool, const QString &, const QString &)>;
    using ProfileCallback = std::function<void(bool, const ProfileSnapshot &, const QString &)>;
    using UuidCallback = std::function<void(const QString &)>;

    void setState(OperationState state);
    NetworkStatus readInterfaceStatus() const;
    static QString firstIpv4ForInterface(const QString &interfaceName,
                                         const QString &preferredAddress = QString());
    static bool interfaceHasIpv4(const QString &interfaceName, const QString &address);

    void runNmcli(const QStringList &arguments, int timeoutMs, const CommandCallback &callback);
    void finishCommand(bool success, const QString &forcedError = QString());
    void failRefresh(const QString &error);

    void resolveReadableProfile(const QString &activeUuid);
    void findDedicatedOrBoundProfile(const UuidCallback &callback);
    void findBoundProfile(const QStringList &candidateUuids,
                          int index,
                          const UuidCallback &callback);
    void readProfile(const QString &uuidOrName, const ProfileCallback &callback);

    void readOldActiveProfile();
    void readDedicatedProfile();
    void applyDedicatedProfile();
    void reactivateDedicatedProfile();
    void verifyAppliedProfile();
    void verifyStaticAddress(int attemptsRemaining);
    void completeApplySuccess();
    void failBeforeModification(const QString &error);

    void beginRollback(const QString &reason);
    void restoreOrDeleteDedicatedProfile();
    void reactivateOldProfile();
    void completeRollback();

    static bool parseIpv4(const QString &text, quint32 *value = nullptr);
    static QString unescapeNmcli(const QString &text);
    static ProfileSnapshot parseProfile(const QString &output);
    static QStringList buildRestoreArguments(const ProfileSnapshot &profile);
    static QString firstNonEmptyLine(const QString &output);

    OperationState m_state = OperationState::Idle;
    NetworkStatus m_status;
    QProcess *m_process = nullptr;
    QTimer *m_commandTimer = nullptr;
    CommandCallback m_commandCallback;
    bool m_commandActive = false;
    bool m_commandTimedOut = false;

    Ipv4Config m_requestedConfig;
    ProfileSnapshot m_oldActiveProfile;
    ProfileSnapshot m_oldDedicatedProfile;
    QString m_oldActiveUuid;
    QString m_targetProfileId;
    bool m_dedicatedProfileExisted = false;
    bool m_dedicatedProfileMayHaveBeenCreated = false;
    QString m_applyFailureReason;
    bool m_rollbackSucceeded = true;
    QStringList m_rollbackProblems;
};

Q_DECLARE_METATYPE(NetworkConfigManager::NetworkStatus)

#endif // CPC_NETWORK_NETWORKCONFIGMANAGER_H
