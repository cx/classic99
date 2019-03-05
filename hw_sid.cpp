// Handles the SID Blaster DLL interface

#include <malloc.h>
#include "hwTypes.h"

// TODO: the SID handle stuff needs to all move into here

void writeSid(Word addr, Byte data) {
	if (NULL != write_sid) {
		write_sid(addr, data);
	}
}

// called to deconfigure/free RAM/etc
void unconfigureSid(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // TODO DLL management
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureSid(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // TODO: DLL loading, etc, happens in here too
    // No direct modification, available only through hw_dsr
    return unconfigureSid;
}
