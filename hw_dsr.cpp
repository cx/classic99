// DSR discriminator - no memory here, we just check to see
// which hardware is banked in and call the appropriate one

#include <malloc.h>
#include "hwTypes.h"

// triggered by a write to DSR space
void writeDSR(Word addr, Byte data) {
    switch (nCurrentDSR) {
        case -1:    // explicitly, nothing is active - so might be SID
            writeSid(addr, data);
            return;

        case 0:     // CF7 (Classic99 version)
            return writeCF7(addr, rmw);

        case 1:     // Disk controller
            return writeDisk(addr, rmw);

        //case 2:   // Reserved

        case 3:     // RS232/PIO
            return writeRS232(addr, rmw);

        //case 4:   // Reserved
        //case 5:   // RS232/PIO card 2
        //case 6:   // reserved
        //case 7:   // reserved
        //case 8:   // thermal printer
        //case 9:   // future
        //case 10:  // future
        //case 11:  // future
        //case 12:  // future
        //case 13:  // future

        case 14:    // SAMS card
            return writeAMSRegisters(addr, rmw);

        case 15:    // pCode card
            return writePCode(addr, rmw);
    }

}

// triggered by a read to DSR space
Byte readDSR(Word addr, bool rmw) {
    switch (nCurrentDSR) {
        case -1:    // explicitly, nothing is active
            return 0;

        case 0:     // CF7 (Classic99 version)
            return readCF7(addr, rmw);

        case 1:     // Disk controller
            return readDisk(addr, rmw);

        //case 2:   // Reserved

        case 3:     // RS232/PIO
            return readRS232(addr, rmw);

        //case 4:   // Reserved
        //case 5:   // RS232/PIO card 2
        //case 6:   // reserved
        //case 7:   // reserved
        //case 8:   // thermal printer
        //case 9:   // future
        //case 10:  // future
        //case 11:  // future
        //case 12:  // future
        //case 13:  // future

        case 14:    // SAMS card
            return readAMSRegisters(addr, rmw);

        case 15:    // pCode card
            return readPCode(addr, rmw);
    }

    // If there is a DSR active, the user may have manually loaded a DSR ROM. Accomodate this.
    if ((nCurrentDSR >= 0) && (nCurrentDSR <= 0xf)) {
	    if (nDSRBank[nCurrentDSR]) {
		    return DSR[nCurrentDSR][addr-0x2000];	// page 1: -0x4000 for base, +0x2000 for second page
	    } else {
		    return DSR[nCurrentDSR][addr-0x4000];	// page 0: -0x4000 for base address
	    }
    }

    return 0;
}

// called to deconfigure/free RAM/etc
void unconfigureDSR(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // nothing to do here
    for (int idx=0x4000; idx<0x6000; ++idx) {
        pReads[idx] = readZero;
        pWrites[idx] = writeZero;
    }
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureDSR(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // takes over the >4xxx range
    for (int idx=0x4000; idx<0x6000; ++idx) {
        pReads[idx] = readDSR;
        pWrites[idx] = writeDSR;
    }

    return unconfigureDSR;
}
