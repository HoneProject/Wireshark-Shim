//----------------------------------------------------------------------------
// Hone Wireshark shim installer
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//----------------------------------------------------------------------------

#ifndef HOOK_H
#define HOOK_H

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

//----------------------------------------------------------------------------
class Hook : public QObject
{
	Q_OBJECT

public:
	explicit Hook(QObject *parent = 0);

	bool Process(QStringList args);

private:
	bool Install(void);
	bool Install(const QString &wiresharkDir);
	bool Is64BitWindows(void);
	void Log(const QString &msg);
	bool ParseArgs(const QStringList &args);
	bool Uninstall(void);
	bool Uninstall(const QString &wiresharkDir);
	bool Usage(const QString &progname);

	QString        m_appPath;
	QTextStream    m_cout;
	QFileInfoList  m_dlls;
	bool           m_install;
	bool           m_pause;
	QStringList    m_wiresharkDirs;
};

#endif // HOOK_H
