//----------------------------------------------------------------------------
// Hone dumpcap replacement
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//----------------------------------------------------------------------------

#include "hone_dumpcap.h"

#ifdef WIN32
const int     HoneDumpcap::m_captureDataSize = 75000;
const QString HoneDumpcap::m_driverFileName("\\\\.\\HoneOut");
#else
const int     HoneDumpcap::m_captureDataSize = 8192;
const QString HoneDumpcap::m_driverFileName("/dev/hone");
#endif

const QRegExp HoneDumpcap::m_newlineRegex("[\r\n]");

//-----------------------------------------------------------------------------
HoneDumpcap::HoneDumpcap(QObject *parent)
	: QObject(parent)
	, m_autoRotateFiles(0)
	, m_autoRotateFileCount(0)
	, m_autoRotateFileSize(0)
	, m_autoRotateMilliseconds(0)
	, m_autoStopFileCount(0)
	, m_autoStopFileSize(0)
	, m_autoStopMilliseconds(0)
	, m_autoStopPacketCount(0)
	, m_captureData(m_captureDataSize, 0)
	, m_captureFileCount(0)
	, m_captureFileSize(0)
	, m_captureStart(0)
	, m_captureState(CaptureStateNormal)
	, m_cout(stdout, QIODevice::WriteOnly)
	, m_driverHandle(InvalidFileHandle)
	, m_dumpcapProcess(this)
	, m_haveHoneInterface(false)
	, m_lastLogHadAutoNewline(true)
	, m_machineReadable(false)
	, m_markCleanup(false)
	, m_markRotate(false)
	, m_needEventLoop(false)
	, m_operation(OperationCapture)
	, m_packetCount(0)
	, m_partialPacketHeader(false)
	, m_partialPacketOffset(0)
	, m_snapLen(65535)
#ifdef WIN32
	, m_signalPipeHandle(InvalidFileHandle)
#endif
{
	// Make sure that capture data buffer size is a multiple of 4 bytes
	Q_ASSERT((m_captureDataSize % 4) == 0);
}

//-----------------------------------------------------------------------------
HoneDumpcap::~HoneDumpcap(void)
{
#ifdef WIN32
	if (m_driverHandle != InvalidFileHandle) {
		::CloseHandle(m_driverHandle);
		m_driverHandle = InvalidFileHandle;
	}
	if (m_signalPipeHandle != InvalidFileHandle) {
		::CloseHandle(m_signalPipeHandle);
		m_signalPipeHandle = InvalidFileHandle;
	}
#else // #ifdef WIN32
	if (m_driverHandle != InvalidFileHandle) {
		::close(m_driverHandle);
		m_driverHandle = InvalidFileHandle;
	}
#endif // #ifdef WIN32
}

//-----------------------------------------------------------------------------
bool HoneDumpcap::CapturePackets(void)
{
	while (m_captureState != CaptureStateDone) {

#ifdef WIN32
		if (m_signalPipeHandle != InvalidFileHandle) {
			DWORD bytesAvailable;
			const BOOL rc = ::PeekNamedPipe(m_signalPipeHandle, NULL, 0, NULL, &bytesAvailable, NULL);
			if (!rc || (bytesAvailable > 0)) {
				Log(QString("Parent process %1 is closing us").arg(m_parentPid));
				m_markCleanup = true;
			}
		}
#else // #ifdef WIN32
		fd_set readfds;
#endif // #ifdef WIN32

		if (m_markCleanup && (m_captureState != CaptureStateCleanUp)) {
			if (!MarkRestart()) {
				return false;
			}
			m_captureState = CaptureStateCleanUp;
			m_markCleanup  = false;
		}
		if (m_markRotate && (m_captureState == CaptureStateNormal)) {
			if (!MarkRestart()) {
				return false;
			}
			m_captureState = CaptureStateRotate;
			m_markRotate       = false;
		}

		quint32 bytesRead;
		if (!ReadDriver(bytesRead)) {
			return false;
		}

		if (bytesRead) {
			const qint32 packetCount  = CountPackets(bytesRead);
			const qint64 bytesWritten = m_captureFile.write(m_captureData.data(), bytesRead);
			if (bytesWritten == -1) {
				return LogError(QString("Cannot write %L1 bytes to %2: %3").arg(bytesRead)
						.arg(m_captureFile.fileName(), m_captureFile.errorString()));
			}
			if (bytesRead != bytesWritten) {
				return LogError(QString("Only wrote %L1 of %L2 bytes to %3").arg(bytesWritten).arg(bytesRead)
						.arg(m_captureFile.fileName()));
			}
			if (!m_captureFile.flush()) {
				return LogError(QString("Cannot flush %1: %2").arg(m_captureFile.fileName(),m_captureFile.errorString()));
			}

			m_captureFileSize    += bytesWritten;
			m_packetCount += packetCount;
			if (m_parentPid.isEmpty()) {
				Log(QString("\rPackets: %1").arg(m_packetCount), false);
			} else {
				WriteCommand('P', QString::number(packetCount));
			}

			// Handle stop and rotate conditions
			if (
					(m_autoStopFileCount    && (m_captureFileCount   >= m_autoStopFileCount  )) ||
					(m_autoStopFileSize     && (m_captureFileSize    >= m_autoStopFileSize   )) ||
					(m_autoStopPacketCount  && (m_packetCount >= m_autoStopPacketCount)) ||
					(m_autoStopMilliseconds && ((QDateTime::currentMSecsSinceEpoch() - m_captureStart) > m_autoStopMilliseconds))) {
				m_markCleanup = true;
			} else if (
					(m_autoRotateFileSize     && (m_captureFileSize >= m_autoRotateFileSize)) ||
					(m_autoRotateMilliseconds && ((QDateTime::currentMSecsSinceEpoch() - m_captureStart) > m_autoRotateMilliseconds))) {
				m_markRotate = true;
			}
		} else { // No data to read
			switch (m_captureState) {
			case CaptureStateCleanUp:
#ifndef WIN32
				if (::ioctl(m_driverHandle, HEIO_GET_AT_HEAD) <= 0) {
					break;
				}
#endif // #ifdef WIN32
				m_captureState = CaptureStateDone;
				break;
			case CaptureStateDone:
				break;
			case CaptureStateNormal:
#ifdef WIN32
				::Sleep(500);
#else // #ifdef WIN32
				FD_ZERO(&readfds);
				FD_SET(m_driverHandle, &readfds);
				if (-1 == ::select(m_driverHandle+1, &readfds, NULL, NULL, NULL)) {
					if (errno == EINTR) {
						m_markCleanup = true;
					} else {
						return LogError("Cannot check for data from the driver", true);
					}
				}
#endif // #ifdef WIN32
				break;
			case CaptureStateRotate:
#ifndef WIN32
				if (::ioctl(m_driverHandle, HEIO_GET_AT_HEAD) <= 0) {
					break;
				}
#endif // #ifdef WIN32
				if (!OpenCaptureFile()) {
					return false;
				}
				m_captureState = CaptureStateNormal;
				break;
			}
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
void HoneDumpcap::Cleanup(void)
{
	m_markCleanup = true;
	if (m_dumpcapProcess.state() != QProcess::NotRunning) {
#ifdef WIN32
		m_dumpcapProcess.kill();
#else
		m_dumpcapProcess.terminate();
#endif
	}
}

//-----------------------------------------------------------------------------
quint32 HoneDumpcap::CountPackets(const quint32 length)
{
	struct PcapNgHeader {
		quint32 blockType;
		quint32 blockLength;
	};

	quint32 packetCount = 0;
	quint32 offset      = 0;

	// Adjust offset if in middle of packet
	if (m_partialPacketHeader) {
		// The block type was at the end of the last buffer
		const quint32 blockLength = *reinterpret_cast<const quint32*>(m_captureData.data());
		offset = blockLength - sizeof(quint32); // Subtract block type
		m_partialPacketHeader = false;
	} else if (m_partialPacketOffset) {
		offset = m_partialPacketOffset;
		m_partialPacketOffset = 0;
	}

	// Handle case where the partial packet continues past end of this buffer
	if (offset > length) {
		m_partialPacketOffset = offset - length;
		return 0;
	}

	// We're at the end of the partial packet, so count it
	if (offset > 0) {
		packetCount++;
	}

	// Count packets
	while (offset + sizeof(PcapNgHeader) <= length) {
		const struct PcapNgHeader *header = reinterpret_cast<const struct PcapNgHeader*>(m_captureData.data() + offset);
		if (offset + header->blockLength > length) {
			// Calculate offset to the start of the next packet
			const quint32 bytesRemaining = length - offset;
			m_partialPacketOffset = header->blockLength - bytesRemaining;
			break;
		}
		packetCount++;
		offset += header->blockLength;
	}

	// Handle case where last partial packet is too small to hold full header
	if ((offset < length) && (m_partialPacketOffset == 0)) {
		m_partialPacketHeader = true;
	}

	return packetCount;
}

//-----------------------------------------------------------------------------
QString HoneDumpcap::FormatError(void)
{
	QString msg;
#ifdef WIN32
	const DWORD  errorCode = GetLastError();
	char        *buffer    = NULL;
	::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<char*>(&buffer), 0, NULL );
	msg = buffer;
	msg.remove(m_newlineRegex);
	::LocalFree(buffer);
#else // #ifdef WIN32
	msg = strerror(errno);
#endif // #ifdef WIN32
	return msg;
}

//-----------------------------------------------------------------------------
bool HoneDumpcap::Initialize(QStringList args, QString appPath)
{
	m_args = args;
	if (!ParseArgs()) {
		return false;
	}

#ifdef WIN32
	m_dumpcapFileName = QString("%1/dumpcap_orig.exe").arg(appPath);
#else
	m_dumpcapFileName = QString("%1/dumpcap_orig").arg(appPath);
#endif

	if (m_operation == OperationCapture) {
		if (m_haveHoneInterface) {
			m_captureStart = QDateTime::currentMSecsSinceEpoch();
			if (m_parentPid.isEmpty()) {
				Log("Capturing on 'Hone'");
			}
			if (!OpenDriver() || !OpenCaptureFile()) {
				return false;
			}
		} else {
			connect(&m_dumpcapProcess, SIGNAL(error(QProcess::ProcessError)),      this, SLOT(OnError(QProcess::ProcessError)));
			connect(&m_dumpcapProcess, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(OnFinished(int,QProcess::ExitStatus)));
			connect(&m_dumpcapProcess, SIGNAL(readyReadStandardError()),           this, SLOT(OnReadyReadStandardError()));
			connect(&m_dumpcapProcess, SIGNAL(readyReadStandardOutput()),          this, SLOT(OnReadyReadStandardOutput()));
			m_dumpcapProcess.start(m_dumpcapFileName, m_args);
			m_needEventLoop = true;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
void HoneDumpcap::Log(const QString &msg, const bool autoNewLine)
{
	if (autoNewLine && !m_lastLogHadAutoNewline) {
		// Add a newline since the last log message didn't
		m_cout << '\n';
	}
	m_cout << msg;
	if (autoNewLine) {
		m_cout << '\n';
	}
	m_cout.flush();
	m_lastLogHadAutoNewline = autoNewLine;
}

//-----------------------------------------------------------------------------
bool HoneDumpcap::LogError(QString msg, const bool useErrorCode, const bool autoNewLine)
{
	if (useErrorCode) {
		msg.append(": ");
		msg.append(FormatError());
	}

	if (m_parentPid.isEmpty()) {
		Log(msg, autoNewLine);
	} else {
		WriteCommand('E', msg);
	}
	return false;
}

//--------------------------------------------------------------------------
bool HoneDumpcap::MarkRestart(void)
{
#ifdef WIN32
	DWORD bytesReturned; // Unused, but required by DeviceIoControl()
	if (!::DeviceIoControl(m_driverHandle, IOCTL_HONE_MARK_RESTART, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
		return LogError("Cannot send log restart IOCTL", true);
	}
#else // #ifdef WIN32
	if (::ioctl(m_driverHandle, HEIO_RESTART) == -1) {
		return LogError("Cannot send log restart IOCTL", true);
	}
#endif // #ifdef WIN32
	return true;
}

//-----------------------------------------------------------------------------
bool HoneDumpcap::NeedEventLoop(void)
{
	return m_needEventLoop;
}

//-----------------------------------------------------------------------------
void HoneDumpcap::OnError(QProcess::ProcessError error)
{
	if (!m_markCleanup) {
		QString errorMsg;
		switch (error) {
		case QProcess::FailedToStart:
			errorMsg = "The process failed to start";
			break;
		case QProcess::Crashed:
			errorMsg = "The process crashed after starting";
			break;
		case QProcess::Timedout:
			errorMsg = "The last wait timed out";
			break;
		case QProcess::WriteError:
			errorMsg = "Cannot write to the process";
			break;
		case QProcess::ReadError:
			errorMsg = "Cannot read from the process";
			break;
		default:
			errorMsg = "An unknown error occurred";
			break;
		}

		Log(QString("Dumpcap received an error: %1").arg(errorMsg));
		QCoreApplication::exit(1);
	}
}

//-----------------------------------------------------------------------------
void HoneDumpcap::OnFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
	if (!m_markCleanup) {
		Log(QString("Dumpcap %1 with exit code %2").arg((exitStatus == QProcess::CrashExit) ? "crashed" : "exited").arg(exitCode));
	}
	QCoreApplication::exit(exitCode);
}

//-----------------------------------------------------------------------------
void HoneDumpcap::OnReadyReadStandardError(void)
{
	const QByteArray err = m_dumpcapProcess.readAllStandardError();
	if (!err.isEmpty()) {
		::fwrite(err.data(), err.length(), 1, stderr);
		::fflush(stderr);
	}
}

//-----------------------------------------------------------------------------
void HoneDumpcap::OnReadyReadStandardOutput(void)
{
	const QByteArray out = m_dumpcapProcess.readAllStandardOutput();
	if (!out.isEmpty()) {
		::fwrite(out.data(), out.length(), 1, stdout);
		::fflush(stdout);
	}
}

//--------------------------------------------------------------------------
bool HoneDumpcap::OpenCaptureFile(void)
{
	const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
	QString filename;

	if (m_captureFileName.isEmpty()) {
		// Format temporary file name
		filename = QString("%1/hone_dumpcap_%2_XXXXXX.pcapng").arg(QDir::tempPath(), timestamp);
		QTemporaryFile tempFile(filename);
		if (!tempFile.open()) {
			return LogError(QString("Cannot create temporary file with template %1: %2").arg(filename, m_captureFile.errorString()));
		}
		filename = tempFile.fileName();
		tempFile.close();
	} else {
		// Format file name
		if (m_autoRotateFiles) {
			QFileInfo fileInfo(m_captureFileName);
			filename = QString("%2/%3_%1_%4.%5").arg(m_captureFileCount).arg(fileInfo.filePath(),
					fileInfo.completeBaseName(), timestamp, fileInfo.suffix());
		} else {
			filename = m_captureFileName;
		}
	}

	m_captureFile.setFileName(filename);
	if (!m_captureFile.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Unbuffered)) {
		return LogError(QString("Cannot open %1 for writing: %2").arg(filename, m_captureFile.errorString()));
	}

	m_captureFileNames.enqueue(filename);
	if (m_autoRotateFileCount) {
		while (m_captureFileNames.size() > static_cast<qint32>(m_autoRotateFileCount)) {
			const QString removeFilename = m_captureFileNames.dequeue();
			if (!QFile::remove(removeFilename)) {
				return LogError(QString("Cannot remove %1").arg(removeFilename));
			}
		}
	}

	m_captureFile.flush();
	m_captureFileCount++;
	m_captureFileSize = 0;
	if (m_parentPid.isEmpty()) {
		Log(QString("File: %1").arg(filename));
	} else {
		WriteCommand('F', filename);
	}
	return true;
}

//-----------------------------------------------------------------------------
bool HoneDumpcap::OpenDriver(void)
{
#ifdef WIN32
	m_driverHandle = ::CreateFileA(m_driverFileName.toLatin1().data(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0);
	if (m_driverHandle == InvalidFileHandle) {
		return LogError(QString("Cannot open driver %1").arg(m_driverFileName), true);
	}

	// On Windows, Wireshark uses a named pipe to signal the child when to exit
	if (!m_parentPid.isEmpty() && (m_parentPid != "none")) {
		QString pipename = QString("\\\\.\\pipe\\wireshark.%1.signal").arg(m_parentPid);
		m_signalPipeHandle = ::CreateFileA(pipename.toLatin1().data(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (m_signalPipeHandle == InvalidFileHandle) {
			return LogError(QString("Cannot open synchronization pipe %1").arg(pipename), true);
		}
	}
#else // #ifdef WIN32
	m_driverHandle = ::open(m_driverFileName.toLatin1().data(), O_RDONLY | O_NONBLOCK, 0);
	if (m_driverHandle == InvalidFileHandle) {
		return LogError(QString("Cannot open driver %1").arg(m_driverFileName), true);
	}
#endif // #ifdef WIN32
	return true;
}

//-----------------------------------------------------------------------------
bool HoneDumpcap::ParseArgs(void)
{
	QStringList errors;

	bool haveInterface   = false;
	bool ok              = false;
	bool printInterfaces = false;
	bool printLinkLayerTypes  = false;

	int  index;
	for (index = 1; index < m_args.size(); index++) {
		if (m_args.at(index) == "-a") {
			if (index+1 >= m_args.size()) {
				errors.append(QString("You must supply a condition with the %1 option").arg(m_args.at(index)));
			}
			index++;
			if (!ParseCondition(m_args.at(index), m_autoStopMilliseconds, m_autoStopFileSize, m_autoStopFileCount)) {
				errors.append(QString("Invalid condition %1 with the %2 option").arg(m_args.at(index), m_args.at(index-1)));
			}
		} else if (m_args.at(index) == "-b") {
			if (index+1 >= m_args.size()) {
				errors.append(QString("You must supply a condition with the %1 option").arg(m_args.at(index)));
			}
			index++;
			if (!ParseCondition(m_args.at(index), m_autoRotateMilliseconds, m_autoRotateFileSize, m_autoRotateFileCount)) {
				errors.append(QString("Invalid condition %1 with the %2 option").arg(m_args.at(index), m_args.at(index-1)));
			}
			m_autoRotateFiles = true;
		} else if (m_args.at(index) == "-c") {
			if (index+1 >= m_args.size()) {
				errors.append(QString("You must supply a packet count with the %1 option").arg(m_args.at(index)));
			}
			index++;
			m_autoStopPacketCount = m_args.at(index).toUInt(&ok);
			if (!ok) {
				errors.append(QString("Invalid packet count %1 with the %2 option").arg(m_args.at(index), m_args.at(index-1)));
			}
		} else if (m_args.at(index) == "-D") {
			printInterfaces = true;
		} else if (m_args.at(index) == "-i") {
			if (index+1 >= m_args.size()) {
				errors.append(QString("You must supply an interface with the %1 option").arg(m_args.at(index)));
			}
			index++;
			if ((m_args.at(index) == "Hone") || (m_args.at(index) == "1")) {
				m_haveHoneInterface = true;
			} else {
				// Fixup interface index before passing to original dumpcap
				quint32 val = m_args.at(index).toUInt(&ok);
				if (ok && (val > 1)) {
					val--;
					m_args[index] = QString::number(val);
				}
			}
			haveInterface = true;
		} else if (m_args.at(index) == "-L") {
			printLinkLayerTypes = true;
		} else if (m_args.at(index) == "-M") {
			m_machineReadable = true;
		} else if (m_args.at(index) == "-s") {
			if (index+1 >= m_args.size()) {
				errors.append(QString("You must supply a snap length with the %1 option").arg(m_args.at(index)));
			}
			index++;
			m_snapLen = m_args.at(index).toUInt(&ok);
			if (!ok) {
				errors.append(QString("Invalid snap length %1 with the %2 option").arg(m_args.at(index), m_args.at(index-1)));
			}
		} else if (m_args.at(index) == "-w") {
			if (index+1 >= m_args.size()) {
				errors.append(QString("You must supply a file name with the %1 option").arg(m_args.at(index)));
			}
			index++;
			m_captureFileName = m_args.at(index);
		} else if (m_args.at(index) == "-Z") {
			if (index+1 >= m_args.size()) {
				errors.append(QString("You must supply a PID or \"none\" with the %1 option").arg(m_args.at(index)));
			}
			index++;
			m_parentPid = m_args.at(index);
		} else if (m_args.at(index) == "-h") {
			return Usage(m_args.at(0));
		} else {
			// Silently ignore unknown options for now
			//Log(QString("Ignoring unknown option %1").arg(m_args.at(index)));
		}
	}

	if (!haveInterface) {
		m_haveHoneInterface = true;
	}

	// Check options
	if (printInterfaces && printLinkLayerTypes) {
		errors.append("The '-D' and '-L' options are mutually exclusive");
	}
	if (printInterfaces) {
		m_operation = OperationPrintInterfaces;
	} else if (printLinkLayerTypes) {
		m_operation = OperationPrintLinkLayerTypes;
	}

	if (!errors.isEmpty()) {
		return Usage(m_args.at(0), errors.join("\n"));
	}

	return true;
}

//----------------------------------------------------------------------------
bool HoneDumpcap::ParseCondition(const QString &condition, qint64 &duration, quint32 &fileSize, quint32 &fileCount)
{
	QStringList tokens = condition.split(':');
	if (tokens.size() != 2) {
		return false;
	}

	bool rc = true;
	const quint32 val = tokens[1].toUInt(&rc);
	if (rc) {
		if (tokens[0] == "duration") {
			duration = val * 1000; // Convert to milliseconds
		} else if (tokens[0] == "filesize") {
			fileSize = val * 1024; // Convert to KB
		} else if (tokens[0] == "files") {
			fileCount = val;
		} else {
			rc = false;
		}
	}
	return rc;
}

//-----------------------------------------------------------------------------
bool HoneDumpcap::PrintInterfaces(void)
{
	// Call dumpcap
	// Get list of interfaces
	// Split into separate lines
	// Increment count on each one
	// Insert our interface at the beginning
	// Print to stdout
	// Print header to stderr

	QStringList args;
	args.append("-D");

	QString interfaces;
	if (m_machineReadable || !m_parentPid.isEmpty()) {
#ifdef WIN32
		interfaces = "1. Hone\tHone capture pseudo-interface\tHone\t\t\tnetwork\n";
#else
		interfaces = "1. Hone\t\tHone capture pseudo-interface\t0\t\tnetwork\n";
#endif
		args.append("-M");
	} else {
		interfaces = "1. Hone (Hone capture pseudo-interface)\n";
	}

	QByteArray out;
	QByteArray err;
	const int rc = RunDumpcap(args, out, err);
	if (rc) {
		return LogError(QString(
				"Cannot get list of interfaces from original dumpcap\n"
				"Execution failed with error code %1\n%2")
				.arg(rc).arg(QString(err)), false, false);
	}

	QStringList lines;
	lines.append(QString(out).split(m_newlineRegex, QString::SkipEmptyParts));
	lines.append(QString(err).split(m_newlineRegex, QString::SkipEmptyParts));
	foreach (const QString line, lines) {
		QStringList parts = line.split('.');
		bool ok;
		quint32 val = parts[0].toUInt(&ok);
		if (!ok) {
			return LogError(QString("Invalid interface '%1' from dumpcap").arg(line));
		}
		val++;
		parts[0] = QString::number(val);
		interfaces.append(QString("%1\n").arg(parts.join(".")));
	}

	if (!m_parentPid.isEmpty()) {
		WriteCommand('S');
	}
	Log(interfaces, false);
	return true;
}

//-----------------------------------------------------------------------------
bool HoneDumpcap::PrintLinkTypes(void)
{
	if (m_haveHoneInterface) {
		if (!m_parentPid.isEmpty()) {
			WriteCommand('S');
			Log("0\n0\tNULL\tNULL");
		} else if (m_machineReadable) {
			Log("Capturing on Hone\n0\n0\tPCAP-NG\tPCAP-NG");
		} else {
			Log("Capturing on Hone\nData link types of interface Hone:\n  PCAP-NG");
		}
	} else {
		QByteArray out;
		QByteArray err;
		const int rc = RunDumpcap(m_args, out, err);
		if (rc) {
			return LogError(QString(
					"Cannot get interface link types from original dumpcap\n"
					"Execution failed with error code %1\n%2")
					.arg(rc).arg(QString(err)), false, false);
		}

		if (!err.isEmpty()) {
			::fwrite(err.data(), err.length(), 1, stderr);
			::fflush(stderr);
		}
		if (!out.isEmpty()) {
			::fwrite(out.data(), out.length(), 1, stdout);
			::fflush(stdout);
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
bool HoneDumpcap::Process(void)
{
	bool rc = false;
	switch (m_operation) {
	case OperationCapture:
		rc = CapturePackets();
		break;
	case OperationPrintInterfaces:
		rc = PrintInterfaces();
		break;
	case OperationPrintLinkLayerTypes:
		rc = PrintLinkTypes();
		break;
	}
	return rc;
}

//-----------------------------------------------------------------------------
bool HoneDumpcap::ReadDriver(quint32 &bytesRead)
{
	bytesRead = 0;
#ifdef WIN32
	DWORD driverBytesRead;
	if (!::ReadFile(m_driverHandle, m_captureData.data(), m_captureData.size(), &driverBytesRead, NULL)) {
		return LogError(QString("Cannot read %L1 bytes from %2").arg(m_captureData.size()).arg(m_driverFileName), true);
	}
	bytesRead = driverBytesRead;
#else // #ifdef WIN32
	const ssize_t driverBytesRead = ::read(m_driverHandle, m_captureData.data(), m_captureData.size());
	if ((driverBytesRead == -1)) {
		if ((errno != EINTR) && (errno != EAGAIN)) {
			return LogError(QString("Cannot read %L1 bytes from %2").arg(m_captureData.size()).arg(m_driverFileName), true);
		}
	} else {
		bytesRead = driverBytesRead;
	}
#endif // #ifdef WIN32

	return true;
}

//-----------------------------------------------------------------------------
int HoneDumpcap::RunDumpcap(const QStringList &args, QByteArray &out, QByteArray &err)
{
	QProcess process;
	process.start(m_dumpcapFileName, args);
	process.waitForFinished();
	out = process.readAllStandardOutput();
	err = process.readAllStandardError();
	return process.exitCode();
}

//-----------------------------------------------------------------------------
bool HoneDumpcap::Usage(const QString progname, const QString &msg)
{
	QString usage;
	if (!msg.isEmpty()) {
		usage.append(msg);
		usage.append("\n\n");
	}

	usage.append(QString(
			"Usage: %1 [options]\n"
			"  -a <cond>         Stop capture after condition <cond>\n"
			"  -b <cond>         Rotate file after condition <cond>\n"
			"  -c <count>        Stop capture after <count> packets\n"
			"  -D                Print list of interfaces and exit\n"
			"  -i <interface>    Capture on interface <interface>\n"
			"  -L                Print inteface link layer types and exit\n"
			"  -M                Use machine-readable output\n"
			"  -s <snap len>     Set capture snap length to <snap len>\n"
			"  -w <file>         Write captured data to <file>\n"
			"  -Z <pid>          Running as child of parent <pid>\n"
			"\n"
			"The -a and -b options take the following condition formats:\n"
			"  duration:NUM  Stop or rotate after NUM seconds\n"
			"  filesize:NUM  Stop or rotate after NUM KB\n"
			"  files:NUM     Stop or rotate after NUM files\n"
			"\n"
			"The PID for the -Z can be 'none'\n"
			"\n"
			"This program uses the same command line arguments as the standard dumpcap\n"
			"utility.  When the interface is set to \"Hone\", it performs the capture\n"
			"using the Hone sensor.  Otherwise, it calls the standard dumpcap utility\n"
			"to perform the capture.\n"
			"\n"
			"This program also intercepts the option to print the list of interfaces\n"
			"and adds the \"Hone\" interface to the list.\n")
			.arg(QFileInfo(progname).fileName()));
	return LogError(usage);
}

//--------------------------------------------------------------------------
void HoneDumpcap::WriteCommand(const char command, const QString &msg)
{
	const int len       = msg.length() + 1;
	char      header[4] = { 0 };

	header[0] = command;
	if (command == 'E') {
		// Error messages have the following format:
		//  E
		//  4 + primary message length + 1 + 4 + secondary message length + 1
		//  E
		//  primary message length + 1
		//  primary message
		//  E
		//  secondary message length + 1
		//  secondary message
		int errorLen = sizeof(header) * 2 + 1;
		if (len > 1) {
			errorLen += len;
		}
		header[1] = (errorLen >> 16) & 0xFF;
		header[2] = (errorLen >>  8) & 0xFF;
		header[3] = (errorLen >>  0) & 0xFF;
		::fwrite(header, sizeof(header), 1, stderr);

		header[1] = (len >> 16) & 0xFF;
		header[2] = (len >>  8) & 0xFF;
		header[3] = (len >>  0) & 0xFF;
		::fwrite(header, sizeof(header), 1, stderr);
		::fwrite(msg.toLatin1().data(), len, 1, stderr);

		header[1] = 0;
		header[2] = 0;
		header[3] = 1;
		::fwrite(header, sizeof(header), 1, stderr);
		::fputc('\0', stderr);
	} else {
		if (len > 1) {
			header[1] = (len >> 16) & 0xFF;
			header[2] = (len >>  8) & 0xFF;
			header[3] = (len >>  0) & 0xFF;
		}
		::fwrite(header, sizeof(header), 1, stderr);
		if (len > 1) {
			::fwrite(msg.toLatin1().data(), len, 1, stderr);
		}
	}

	::fflush(stderr);
}
