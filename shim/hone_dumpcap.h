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

#ifndef HONE_DUMPCAP_H
#define HONE_DUMPCAP_H

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QQueue>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextStream>

#ifdef WIN32
#include <Windows.h>

#define IOCTL_HONE_MARK_RESTART CTL_CODE(FILE_DEVICE_UNKNOWN, 2048, \
	METHOD_NEITHER, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

typedef HANDLE FileHandle;
#define InvalidFileHandle INVALID_HANDLE_VALUE

#else // #ifdef WIN32

#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>

#define HEIO_RESTART        _IO(0xE0, 0x01)
#define HEIO_GET_AT_HEAD    _IO(0xE0, 0x03)
#define HEIO_GET_SNAPLEN    _IOR(0xE0, 0x04, int)
#define HEIO_SET_SNAPLEN    _IOW(0xE0, 0x05, int)
#define HEIO_GET_RING_PAGES _IOR(0xE0, 0x06, int)
#define HEIO_SET_RING_PAGES _IOW(0xE0, 0x07, int)

typedef int FileHandle;
#define InvalidFileHandle -1

#endif // #ifdef WIN32

//----------------------------------------------------------------------------
class HoneDumpcap : public QObject
{
	Q_OBJECT

public:
	explicit HoneDumpcap(QObject *parent = 0);
	~HoneDumpcap(void);

	bool NeedEventLoop(void);
	void Cleanup(void);
	bool Initialize(QStringList args, QString appPath);
	bool Process(void);

private slots:
	void OnError(QProcess::ProcessError error);
	void OnFinished(int exitCode, QProcess::ExitStatus exitStatus);
	void OnReadyReadStandardError(void);
	void OnReadyReadStandardOutput(void);

private:
	enum Operation {
		OperationCapture,              // Capture packets
		OperationPrintInterfaces,      // Print network interfaces
		OperationPrintLinkLayerTypes,  // Print link layer types for an interface
	};

	enum CaptureState {
		CaptureStateNormal,   // Normal operation
		CaptureStateRotate,   // Rotate the log
		CaptureStateCleanUp,  // Ensure last packet is fully read before exiting
		CaptureStateDone,     // Done capturing
	};

	bool CapturePackets(void);
	quint32 CountPackets(const quint32 length);
	QString FormatError(void);
	void Log(const QString &msg, const bool autoNewLine = true);
	bool LogError(QString msg, const bool useErrorCode = false, const bool autoNewLine = true);
	bool MarkRestart(void);
	bool OpenCaptureFile(void);
	bool OpenDriver(void);
	bool ParseArgs(void);
	bool ParseCondition(const QString &condition, qint64 &duration, quint32 &fileSize, quint32 &fileCount);
	bool PrintInterfaces(void);
	bool PrintLinkTypes(void);
	bool ReadDriver(quint32 &bytesRead);
	int  RunDumpcap(const QStringList &args, QByteArray &out, QByteArray &err);
	bool Usage(const QString progname, const QString &msg = QString());
	void WriteCommand(const char command, const QString &msg = QString());

	QStringList           m_args;
	bool                  m_autoRotateFiles;
	quint32               m_autoRotateFileCount;
	quint32               m_autoRotateFileSize;
	qint64                m_autoRotateMilliseconds;
	quint32               m_autoStopFileCount;
	quint32               m_autoStopFileSize;
	qint64                m_autoStopMilliseconds;
	quint32               m_autoStopPacketCount;
	QByteArray            m_captureData;
	static const int      m_captureDataSize;
	QFile                 m_captureFile;
	quint32               m_captureFileCount;
	QString               m_captureFileName;
	QQueue<QString>       m_captureFileNames;
	quint32               m_captureFileSize;
	qint64                m_captureStart;
	CaptureState          m_captureState;
	QTextStream           m_cout;
	static const QString  m_driverFileName;
	FileHandle            m_driverHandle;
	QString               m_dumpcapFileName;
	QProcess              m_dumpcapProcess;
	bool                  m_haveHoneInterface;
	bool                  m_lastLogHadAutoNewline;
	bool                  m_machineReadable;
	bool                  m_markCleanup;
	bool                  m_markRotate;
	bool                  m_needEventLoop;
	static const QRegExp  m_newlineRegex;
	Operation             m_operation;
	quint32               m_packetCount;
	QString               m_parentPid;
	bool                  m_partialPacketHeader;
	quint32               m_partialPacketOffset;
	quint32               m_snapLen;
#ifdef WIN32
	FileHandle            m_signalPipeHandle;
#endif
};

#endif // HONE_DUMPCAP_H
