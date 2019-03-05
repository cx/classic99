// Handles the Classic99 disk interface - a banked
// DSR with bank 0 being the Classic99 ROM and bank 1
// being the TI disk controller.

#include <malloc.h>
#include "hwTypes.h"

// triggered by a write to >8xxx
void writeDiskDSR(Word addr, Byte data) {
	if ((nDSRBank[1] > 0) && (addr>=0x5ff0) && (addr<=0x5fff)) {
		WriteTICCRegister(x, c);
	}
}

// triggered by a read to >2xxx
Byte readDiskDSR(Word addr, bool rmw) {
	// TI Disk controller, if active
	if ((nDSRBank[1] > 0) && (addr>=0x5ff0) && (addr<=0x5fff)) {
		return ReadTICCRegister(addr);
	}

    // TODO: needs to maintain its own bank flag, not shared
    // TODO: needs to move its data into this file
	if (nDSRBank[nCurrentDSR]) {
		return DSR[nCurrentDSR][addr-0x2000];	// page 1: -0x4000 for base, +0x2000 for second page
	} else {
		return DSR[nCurrentDSR][addr-0x4000];	// page 0: -0x4000 for base address
	}
}

// called to deconfigure/free RAM/etc
void unconfigureDiskDSR(readFunc * /*pReads*/, writeFunc * /*pWrites*/, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureDiskDSR(readFunc * /*pReads*/, writeFunc * /*pWrites*/, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    return unconfigureDiskDSR;
}
