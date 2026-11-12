# pique — the peek browser: look at a link without paying for it with a tab.
QT += core gui qml quick quickcontrols2 network webenginequick

CONFIG += c++17 release
TARGET = pique
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/hyprland.h \
    src/standby.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/hyprland.cpp \
    src/standby.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
