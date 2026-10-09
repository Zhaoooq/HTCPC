QT += core
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = reliability_test

INCLUDEPATH += .. ../acquisition ../algorithms ../control ../hardware

SOURCES += \
    reliability_test.cpp \
    ../acquisition/OpcProcessingWorker.cpp \
    ../acquisition/RawDataWriter.cpp \
    ../algorithms/OpcCounter.cpp \
    ../hardware/PT100Sensor.cpp \
    ../control/LiquidControlSystem.cpp

HEADERS += \
    ../acquisition/OpcProcessingWorker.h \
    ../acquisition/RawDataWriter.h \
    ../algorithms/OpcCounter.h \
    ../control/TemperaturePid.h \
    ../hardware/PT100Sensor.h \
    ../control/LiquidControlSystem.h

LIBS += -llgpio
