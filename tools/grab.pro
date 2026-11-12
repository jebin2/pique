QT += core gui qml quick quickcontrols2 network webenginequick
CONFIG += c++17 release
TARGET = grab
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/standby.h ../src/backend.h ../src/hyprland.h ../src/theme.h ../src/translator.h
SOURCES += grab.cpp ../src/standby.cpp ../src/backend.cpp ../src/hyprland.cpp ../src/theme.cpp ../src/translator.cpp
RESOURCES += ../src/resources.qrc
