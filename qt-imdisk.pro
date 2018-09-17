#-------------------------------------------------
#
# Project created by QtCreator 2018-09-17T23:33:33
#
#-------------------------------------------------

QT       += core gui widgets testlib

TARGET = qt-imdisk
TEMPLATE = app

IMDISK_SDK = "../imdisk_source"

INCLUDEPATH += $$IMDISK_SDK/inc

win32:CONFIG(release, debug|release):LIBS += "$$IMDISK_SDK/Release/imdisk.lib"
win32:CONFIG(debug, debug|release):LIBS += "$$IMDISK_SDK/Debug/imdisk.lib"

win32:LIBS += user32.lib ntdll.lib

SOURCES += \
        main.cpp \
        widget.cpp \
    ramdisk.cpp

HEADERS += \
        widget.h \
    ramdisk.h

FORMS += \
        widget.ui

