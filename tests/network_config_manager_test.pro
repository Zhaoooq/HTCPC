QT += core network
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = network_config_manager_test

INCLUDEPATH += ..

SOURCES += \
    network_config_manager_test.cpp \
    ../network/NetworkConfigManager.cpp

HEADERS += \
    ../network/NetworkConfigManager.h
