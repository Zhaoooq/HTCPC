QT += core
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = ftdi_daq_test

INCLUDEPATH += .. ../acquisition
SOURCES += ftdi_daq_test.cpp ../acquisition/daq_worker.cpp
HEADERS += ../acquisition/daq_worker.h
LIBS += -L$$PWD/.. -lftd2xx
QMAKE_RPATHDIR += $$PWD/..
