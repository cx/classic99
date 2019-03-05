// Handles the 256 byte scratchpad RAM expansion

#include <malloc.h>
#include "hwTypes.h"

// TODO: bring in the CF7 hardware

void writeCF7(Word addr, Byte data) {
    if (csCf7Bios.GetLength() <= 0) {
        return;
    }

    if ((addr>=0x5e00) && (addr<0x5f00)) {
        write_cf7(addr, data);
    }
}

Byte readCF7(Word addr, bool rmw) {
    if (csCf7Bios.GetLength() <= 0) {
        return 0;
    }

    if ((addr>=0x5e00) && (addr<0x5f00)) {
        return read_cf7(addr);
    }

    // just access memory
	if (nDSRBank[nCurrentDSR]) {
		return DSR[nCurrentDSR][addr-0x2000];	// page 1: -0x4000 for base, +0x2000 for second page
	} else {
		return DSR[nCurrentDSR][addr-0x4000];	// page 0: -0x4000 for base address
	}
}

// called to deconfigure/free RAM/etc
void unconfigureCF7(readFunc * /*pReads*/, writeFunc * /*pWrites*/, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureCF7(readFunc * /*pReads*/, writeFunc * /*pWrites*/, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // This is called in by the DSR code
    return unconfigureCF7;
}
