QT += widgets printsupport network

CONFIG += c++17
CONFIG -= app_bundle

QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic -Wshadow

TEMPLATE = app
TARGET = HTCPC

INCLUDEPATH += . \
    acquisition \
    hardware \
    control \
    algorithms \
    ui \
    state

SOURCES += \
    main.cpp \
    acquisition/daq_worker.cpp \
    acquisition/AcquisitionController.cpp \
    acquisition/OpcProcessingWorker.cpp \
    acquisition/RawDataWriter.cpp \
    algorithms/OpcCounter.cpp \
    control/PressureValveController.cpp \
    hardware/Ads1115PressureSensor.cpp \
    hardware/N4IOA01Valve.cpp \
    hardware/PT100Sensor.cpp \
    hardware/PwmOutputs.cpp \
    network/CpcTcpServer.cpp \
    network/NetworkConfigManager.cpp \
    network/RemoteDashboard.cpp \
    ui/ControlWidgets.cpp \
    ui/Formatters.cpp \
    ui/MainWindowUi.cpp \
    ui/PlotSetup.cpp \
    ui/TouchDoubleSpinBox.cpp \
    ui/WatermarkWidget.cpp \
    qcustomplot.cpp \
    control/LiquidControlSystem.cpp

HEADERS += \
    acquisition/AcquisitionController.h \
    acquisition/OpcProcessingWorker.h \
    acquisition/RawDataWriter.h \
    algorithms/OpcCounter.h \
    control/PressureValveController.h \
    control/TemperaturePid.h \
    acquisition/daq_worker.h \
    hardware/Ads1115PressureSensor.h \
    hardware/N4IOA01Valve.h \
    hardware/PT100Sensor.h \
    hardware/PinMap.h \
    hardware/PwmOutputs.h \
    network/CpcTcpServer.h \
    network/NetworkConfigManager.h \
    network/RemoteDashboard.h \
    state/AppRuntimeState.h \
    ui/ControlWidgets.h \
    ui/Formatters.h \
    ui/MainWindowUi.h \
    ui/PlotSetup.h \
    ui/TouchDoubleSpinBox.h \
    ui/WatermarkWidget.h \
    qcustomplot.h \
    control/LiquidControlSystem.h

RESOURCES += resources.qrc

# The vendor D2XX shared object is deployed beside the CPC executable.
LIBS += -L$$PWD -llgpio -lftd2xx
QMAKE_RPATHDIR += $$PWD
