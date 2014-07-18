//----------------------------------------------------------------------------
// Hone dumpcap replacement entry point
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//----------------------------------------------------------------------------

#include "hone_dumpcap.h"
#include <stdio.h>

#ifndef WIN32
#include <signal.h>
#endif

HoneDumpcap *g_honeDumpcap = NULL;

//--------------------------------------------------------------------------
#ifdef WIN32
BOOL WINAPI ConsoleHandler(DWORD ctrlType)
{
	Q_UNUSED(ctrlType);
	if (g_honeDumpcap) {
		g_honeDumpcap->Cleanup();
	}
	return TRUE;
}
#else // #ifdef WIN32
static void SignalHandler(int signum)
{
	switch(signum) {
	case SIGINT:
	case SIGTERM:
		if (g_honeDumpcap) {
			g_honeDumpcap->Cleanup();
		}
		break;
	default:
		break;
	}

}
#endif // #ifdef WIN32

//--------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	HoneDumpcap      honeDumpcap;
	int              rc = 0;

	g_honeDumpcap = &honeDumpcap;

#ifdef WIN32
	if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
		printf("Cannot set console control handler: %08X\n", GetLastError());
		return 1;
	}
#else // #ifdef WIN32
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SignalHandler;

	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
#endif // #ifdef WIN32

	if (!honeDumpcap.Initialize(app.arguments(), app.applicationDirPath())) {
		return 1;
	}

	if (honeDumpcap.NeedEventLoop()) {
		rc = app.exec();
	} else {
		rc = honeDumpcap.Process() ? 0 : 1;
	}

	return rc;
}
