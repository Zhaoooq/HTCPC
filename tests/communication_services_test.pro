QT += core network
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = communication_services_test

INCLUDEPATH += ..

SOURCES += \
    communication_services_test.cpp \
    ../network/CpcTcpServer.cpp \
    ../network/RemoteDashboard.cpp

HEADERS += \
    ../network/CpcTcpServer.h \
    ../network/RemoteDashboard.h
