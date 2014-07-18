QT += core
QT -= gui

TARGET = hook
CONFIG += console
CONFIG -= app_bundle

QMAKE_CFLAGS_RELEASE += /Zi
QMAKE_LFLAGS_RELEASE += /MAP /debug /opt:ref
QMAKE_LFLAGS += /MANIFESTUAC:level=\'requireAdministrator\'

TEMPLATE = app

RC_FILE = hook.rc

SOURCES += \
	hook.cpp \
	main.cpp

HEADERS += \
	../version.h \
	../version_info.h \
	hook.h \
	hook_info.h

OTHER_FILES += \
	hook.rc
