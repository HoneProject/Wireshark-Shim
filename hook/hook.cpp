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

#include "hook.h"

#include <windows.h>
#include <conio.h>

//-----------------------------------------------------------------------------
Hook::Hook(QObject *parent)
	: QObject(parent)
	, m_appPath(QCoreApplication::applicationDirPath())
	, m_cout(stdout, QIODevice::WriteOnly)
	, m_install(false)
	, m_pause(false)
{
	QStringList pfDirs;

	if (Is64BitWindows()) {
		pfDirs.append(QString(qgetenv("ProgramW6432")));
		pfDirs.append(QString(qgetenv("ProgramFiles(x86)")));
	} else {
		pfDirs.append(QString(qgetenv("ProgramFiles")));
	}

	foreach (const QString &pfDir, pfDirs) {
		if (!pfDir.isEmpty()) {
			QString wireshark = QString("%1/Wireshark").arg(pfDir);
			wireshark.replace('\\', '/');
			if (QFileInfo(wireshark).exists()) {
				m_wiresharkDirs.append(wireshark);
			}
		}
	}

	// Get list of all DLLs bundled with the application
	QDir appDir(m_appPath);
	m_dlls = appDir.entryInfoList(QStringList() << "*.dll", QDir::Files);
}

//-----------------------------------------------------------------------------
bool Hook::Install(void)
{
	bool rc = true;

	foreach (const QString &wiresharkDir, m_wiresharkDirs) {
		Log(QString("Installing shim to %1").arg(wiresharkDir));
		if (!Install(wiresharkDir)) {
			// Clean up failed install
			Uninstall(wiresharkDir);
			rc = false;
		}
	}
	return rc;
}

//-----------------------------------------------------------------------------
bool Hook::Is64BitWindows(void)
{
	// From http://blogs.msdn.com/b/oldnewthing/archive/2005/02/01/364563.aspx
#if defined(_WIN64)
	return true;  // 64-bit programs run only on Win64
#elif defined(_WIN32)
	// 32-bit programs run on both 32-bit and 64-bit Windows so must sniff
	BOOL is64bit = FALSE;
	return IsWow64Process(GetCurrentProcess(), &is64bit) && is64bit;
#else
	return false; // Win64 does not support Win16
#endif
}

//-----------------------------------------------------------------------------
bool Hook::Install(const QString &wiresharkDir)
{
	const QString   honeDumpcap = QString("%1/hone-dumpcap.exe").arg(m_appPath);
	const QFileInfo dumpcap     = QString("%1/dumpcap.exe").arg(wiresharkDir);
	const QFileInfo dumpcapOrig = QString("%1/dumpcap_orig.exe").arg(wiresharkDir);

	// Print error if neither dumpcap.exe nor dumpcap_orig.exe exist
	if (!dumpcap.exists() && !dumpcapOrig.exists()) {
		Log(QString("Cannot find dumpcap.exe in %1").arg(wiresharkDir));
		return false;
	}

	// Move dumpcap.exe to dumpcap_orig.exe if dumpcap_orig.exe doesn't exist
	bool renamed = false;
	if (!dumpcapOrig.exists()) {
		QDir dir;
		if (!dir.rename(dumpcap.absoluteFilePath(), dumpcapOrig.absoluteFilePath())) {
			Log(QString("Cannot rename %1 to %2").arg(dumpcap.absoluteFilePath(),
					dumpcapOrig.absoluteFilePath()));
			return false;
		}
		renamed = true;
	}

	// Remove dumpcap.exe if it already exists
	if (!renamed && dumpcap.exists()) {
		if (!QFile::remove(dumpcap.absoluteFilePath())) {
			Log(QString("Cannot remove %1").arg(dumpcap.absoluteFilePath()));
			return false;
		}
	}

	// Copy the shim to dumpcap.exe
	if (!QFile::copy(honeDumpcap, dumpcap.absoluteFilePath())) {
		Log(QString("Cannot copy %1 to %2").arg(honeDumpcap,
				dumpcap.absoluteFilePath()));
		return false;
	}

	// Copy DLLs used by the shim
	foreach (const QFileInfo &dll, m_dlls) {
		const QString destName = QString("%1/%2").arg(wiresharkDir, dll.fileName());
		QFile::copy(dll.absoluteFilePath(), destName);
	}

	return true;
}

//-----------------------------------------------------------------------------
void Hook::Log(const QString &msg)
{
	m_cout << msg << '\n';
	m_cout.flush();
}

//--------------------------------------------------------------------------
bool Hook::ParseArgs(const QStringList &args)
{
	int  index;
	bool rc = true;

	if (args.size() < 2) {
		Log("You must specify an operation to perform");
		return false;
	}

	if (args.at(1) == "-h") {
		return false;
	} else if (args.at(1) == "install") {
		m_install = true;
	} else if (args.at(1) == "uninstall") {
		m_install = false;
	} else {
		Log(QString("Unknown command \"%1\"").arg(args.at(1)));
		return false;
	}

	for (index = 2; index < args.size(); index++) {
		if (args.at(index) == "-p") {
			m_pause = true;
		} else {
			Log(QString("Unknown option \"%s\"").arg(args.at(index)));
			rc = false;
			break;
		}
	}

	return rc;
}

//-----------------------------------------------------------------------------
bool Hook::Process(QStringList args)
{
	if (!ParseArgs(args)) {
		return Usage(args.at(0));
	}

	bool rc = true;
	if (m_wiresharkDirs.isEmpty()) {
		Log("No Wireshark installations detected");
	} if (m_install) {
		rc = Install();
	} else  {
		rc = Uninstall();
	}

	if (m_pause) {
		Log("\nPress any key to continue . . .");
		_getch();
	}

	return rc;
}

//-----------------------------------------------------------------------------
bool Hook::Uninstall(void)
{
	bool rc = true;

	foreach (const QString &wiresharkDir, m_wiresharkDirs) {
		Log(QString("Uninstalling shim from %1").arg(wiresharkDir));
		if (!Uninstall(wiresharkDir)) {
			rc = false;
		}
	}
	return rc;
}

//-----------------------------------------------------------------------------
bool Hook::Uninstall(const QString &wiresharkDir)
{
	const QFileInfo dumpcap     = QString("%1/dumpcap.exe").arg(wiresharkDir);
	const QFileInfo dumpcapOrig = QString("%1/dumpcap_orig.exe").arg(wiresharkDir);
	QDir            dir;

	// Remove DLLs used by the shim
	foreach (const QFileInfo &dll, m_dlls) {
		const QString destName = QString("%1/%2").arg(wiresharkDir, dll.fileName());
		QFile::remove(destName);
	}

	// Print error if dumpcap_orig.exe does not exist
	if (!dumpcapOrig.exists()) {
		Log(QString("Cannot find dumpcap_orig.exe in %1").arg(wiresharkDir));
		return false;
	}

	// Remove dumpcap.exe if it already exists
	if (dumpcap.exists()) {
		if (!QFile::remove(dumpcap.absoluteFilePath())) {
			Log(QString("Cannot remove %1").arg(dumpcap.absoluteFilePath()));
			return false;
		}
	}

	// Rename dumpcap_orig.exe to dumpcap.exe
	if (!dir.rename(dumpcapOrig.absoluteFilePath(), dumpcap.absoluteFilePath())) {
		Log(QString("Cannot rename %1 to %2").arg(dumpcap.absoluteFilePath(),
				dumpcapOrig.absoluteFilePath()));
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
bool Hook::Usage(const QString &progname)
{
	Log(QString("Usage: %1 command [options]\n"
			"Commands:\n"
			"  install    Install the Hone dumpcap shim in Wireshark\n"
			"  uninstall  Uninstall the Hone dumpcap shim from Wireshark\n"
			"Options:\n"
			"  -p  Pause before exiting\n").arg(QFileInfo(progname).fileName()));
	return false;
}
