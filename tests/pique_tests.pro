QT += core gui quick quickcontrols2 testlib dbus network
CONFIG += c++17 testcase
TARGET = pique_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/hyprland.h ../src/standby.h ../src/theme.h ../src/translator.h
SOURCES += pique_tests.cpp ../src/backend.cpp ../src/hyprland.cpp ../src/standby.cpp ../src/theme.cpp ../src/translator.cpp
