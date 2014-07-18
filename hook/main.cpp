//----------------------------------------------------------------------------
// Hone Wireshark shim installer entry point
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//----------------------------------------------------------------------------

#include "hook.h"

//--------------------------------------------------------------------------
int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	Hook             hook;

	return hook.Process(app.arguments()) ? 0 : 1;
}
