// Handles the Gigacart bank switched cartridge with GROM

#include <malloc.h>
#include "hwTypes.h"

void writeGigacart(Word addr, Byte data) {
    // collect bits from address and data buses
    int bits = (data<<13)|(addr&0x1fff);
    xbBank=(((bits)>>1)&xb);		// XB bank switch

	// check breakpoints against what was written to where
	for (int idx=0; idx<nBreakPoints; idx++) {
		switch (BreakPoints[idx].Type) {
			case BREAK_EQUALS_BYTE:
				if ((CheckRange(idx, addr)) && ((data&BreakPoints[idx].Mask) == BreakPoints[idx].Data)) {
					TriggerBreakPoint();
				}
				break;
		}
	}
}

Byte readGigacart(Word addr, bool rmw) {
	// XB is supposed to only page the upper 4k, but some Atari carts seem to like it all
	// paged. Most XB dumps take this into account so only full 8k paging is implemented.
	if (xb) {
        // make sure xbBank never exceeds xb
		return(CPU2[(xbBank<<13)+(addr-0x6000)]);	// cartridge bank 2 and up
	} else {
		return ReadMemoryByte(addr, !rmw);			// cartridge bank 1
	}
}

// called to deconfigure/free RAM/etc
void unconfigureGigacart(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    for (int idx=0x6000; idx<0x8000; ++idx) {
        pReads[idx] = readZero;
        pWrites[idx] = writeZero;
    }
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureGigacart(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // takes over the address space for >6000-7FFF
    for (int idx=0x6000; idx<0x8000; ++idx) {
        pReads[idx] = readGigacart;
        pWrites[idx] = writeGigacart;
    }

    return unconfigureGigacart;
}
