#include "network/NetworkConfigManager.h"

#include <QCoreApplication>
#include <QTimer>

#include <cassert>

namespace {

NetworkConfigManager::Ipv4Config staticConfig(const QString &address,
                                               int prefix = 24,
                                               const QString &gateway = QString(),
                                               const QString &dns = QString()) {
    NetworkConfigManager::Ipv4Config config;
    config.mode = NetworkConfigManager::Ipv4Mode::Static;
    config.address = address;
    config.prefixLength = prefix;
    config.gateway = gateway;
    config.dns = dns;
    return config;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QString error;

    NetworkConfigManager::Ipv4Config dhcp;
    dhcp.mode = NetworkConfigManager::Ipv4Mode::Dhcp;
    assert(NetworkConfigManager::validateConfig(dhcp, &error));
    const QStringList dhcpArguments = NetworkConfigManager::nmcliIpv4Arguments(dhcp);
    assert(dhcpArguments.contains("ipv4.method"));
    assert(dhcpArguments.contains("auto"));

    assert(NetworkConfigManager::validateConfig(
        staticConfig("192.168.50.2", 24, QString(), QString()), &error));
    assert(NetworkConfigManager::validateConfig(
        staticConfig("192.168.50.2", 24, "192.168.50.1", "8.8.8.8"), &error));
    const QStringList staticArguments = NetworkConfigManager::nmcliIpv4Arguments(
        staticConfig("192.168.50.2", 24, "192.168.50.1", "8.8.8.8"));
    assert(staticArguments.contains("manual"));
    assert(staticArguments.contains("192.168.50.2/24"));
    assert(staticArguments.contains("192.168.50.1"));
    assert(staticArguments.contains("8.8.8.8"));

    assert(!NetworkConfigManager::validateConfig(staticConfig("192.168.50"), &error));
    assert(!NetworkConfigManager::validateConfig(staticConfig("256.1.1.1"), &error));
    assert(!NetworkConfigManager::validateConfig(staticConfig("0.0.0.0"), &error));
    assert(!NetworkConfigManager::validateConfig(staticConfig("255.255.255.255"), &error));
    assert(!NetworkConfigManager::validateConfig(staticConfig("127.0.0.1"), &error));
    assert(!NetworkConfigManager::validateConfig(staticConfig("192.168.50.2", 0), &error));
    assert(!NetworkConfigManager::validateConfig(staticConfig("192.168.50.2", 33), &error));
    assert(!NetworkConfigManager::validateConfig(staticConfig("192.168.50.2", 32), &error));
    assert(!NetworkConfigManager::validateConfig(staticConfig("192.168.50.0", 24), &error));
    assert(!NetworkConfigManager::validateConfig(staticConfig("192.168.50.255", 24), &error));
    assert(!NetworkConfigManager::validateConfig(
        staticConfig("192.168.50.2", 24, "192.168.60.1"), &error));
    assert(!NetworkConfigManager::validateConfig(
        staticConfig("192.168.50.2", 24, QString(), "not-an-address"), &error));

    assert(NetworkConfigManager::netmaskForPrefix(24) == "255.255.255.0");
    assert(NetworkConfigManager::netmaskForPrefix(16) == "255.255.0.0");
    assert(NetworkConfigManager::netmaskForPrefix(32) == "255.255.255.255");
    assert(NetworkConfigManager::recommendedIpcAddress(staticConfig("192.168.50.2")) ==
           "192.168.50.1/24");
    assert(NetworkConfigManager::recommendedIpcAddress(staticConfig("10.0.0.2")) ==
           QStringLiteral("请将工控机设置为同一子网内的其他可用地址"));

    // The refresh path is read-only and must finish cleanly whether this test
    // host has a managed Ethernet interface or no accessible NetworkManager.
    NetworkConfigManager manager;
    bool refreshCompleted = false;
    QObject::connect(&manager, &NetworkConfigManager::statusUpdated,
                     &application,
                     [&](const NetworkConfigManager::NetworkStatus &status) {
                         if (!status.profileQueryComplete) return;
                         refreshCompleted = true;
                         application.quit();
                     });
    QTimer::singleShot(15000, &application, &QCoreApplication::quit);
    manager.refresh();
    application.exec();
    assert(refreshCompleted);
    assert(!manager.isBusy());

    bool invalidApplyRejected = false;
    QObject::connect(&manager, &NetworkConfigManager::operationFailed,
                     &application,
                     [&](const QString &, bool) { invalidApplyRejected = true; });
    manager.applyConfig(staticConfig("999.1.1.1"));
    assert(invalidApplyRejected);
    assert(!manager.isBusy());
    return 0;
}
