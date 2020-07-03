#-------------------------------------------------
#
# Project created by QtCreator 2018-01-26T14:05:30
#
#-------------------------------------------------

QT       += core gui widgets svg
CONFIG   += c++14 warn_on

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    $$PWD/textedit.cpp \
    $$PWD/texteditgutter.cpp \
    $$PWD/editorlabel.cpp \
    $$PWD/plugins/colorlabelsplugin.cpp \
    $$PWD/plugins/latexplugin.cpp \
    $$PWD/plugins/pluginbase.cpp \
    $$PWD/JKQTMath/jkqtmathtext.cpp \
    #$PWD/JKQTMath/jkqtphighrestimer.cpp \
    $$PWD/JKQTMath/jkqtptools.cpp \
    $$PWD/plugins/bracketmatcherplugin.cpp

HEADERS += \
    $$PWD/JKQTMath/jkqtsymbl.h \
    $$PWD/textedit.h \
    $$PWD/texteditgutter.h \
    $$PWD/editorlabel.h \
    $$PWD/plugins/colorlabelsplugin.h \
    $$PWD/plugins/latexplugin.h \
    $$PWD/plugins/pluginbase.h \
    $$PWD/JKQTMath/jkqtmathtext.h \
    $$PWD/JKQTMath/jkqtp_imexport.h \
    #$$PWD/JKQTMath/jkqtphighrestimer.h \
    $$PWD/JKQTMath/jkqtptools.h \
    $$PWD/plugins/bracketmatcherplugin.h \
    $$PWD/util/scopeguard.h
 
RESOURCES += $$PWD/JKQTMath/asana.qrc \
    $$PWD/icons/icons.qrc
DEFINES += AUTOLOAD_Asana_FONTS

include(Highlighter/Highlighter.pri)

DISTFILES +=
