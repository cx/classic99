// Handles the 256 byte scratchpad RAM expansion

#include <malloc.h>
#include "hwTypes.h"

// TODO: bring in the RS232 hardware code
// CRU note: on the PEB controllers, the UART CRU bits are always available, even when the ROM is not banked in
// But on the NanoPeb, which I don't support right now anyway, the CRU bits are not available if the ROM is not banked in

// triggered by a write to >8xxx
void writeRS232(Word addr, Byte data) {
	// currently we aren't mapping any ROM space - this will change!
	WriteRS232Mem(addr-0x4000, data);
}

// triggered by a read to >2xxx
Byte readRS232(Word addr, bool rmw) {
	// currently we aren't mapping any ROM space - this will change!
	return ReadRS232Mem(addr-0x4000);
}

// called to deconfigure/free RAM/etc
void unconfigureRS232(readFunc * /*pReads*/, writeFunc * /*pWrites*/, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureRS232(readFunc * /*pReads*/, writeFunc * /*pWrites*/, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    return unconfigureRS232;
}
