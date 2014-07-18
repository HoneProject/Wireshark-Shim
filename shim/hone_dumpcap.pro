QT += core
QT -= gui

TARGET = hone-dumpcap
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

win32 {
	QMAKE_CFLAGS_RELEASE += /Zi
	QMAKE_LFLAGS_RELEASE += /MAP /debug /opt:ref
	QMAKE_LFLAGS += /MANIFESTUAC:level=\'requireAdministrator\'

	RC_FILE = hone_dumpcap.rc

	HEADERS += \
		../version.h \
		../version_info.h \
		hone_dumpcap_info.h

	OTHER_FILES += hone_dumpcap.rc
}

SOURCES += \
	main.cpp \
	hone_dumpcap.cpp

HEADERS += \
	hone_dumpcap.h
