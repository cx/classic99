// Handles the a 378 (non-inverted) bank switched cartridge

#include <malloc.h>
#include "hwTypes.h"

// TODO: split out 378 and 379, so we don't need to invert at load time
// TODO: load the data here

void write378(Word addr, Byte data) {
    // collect bits from address and data buses - x is address, c is data
    // TODO: do I want to include the data bus too? It works the same way...?
    int bits = (data<<13)|(addr&0x1fff);
    xbBank=(((bits)>>1)&xb);		// XB bank switch, up to 4096 banks
}

Byte read378(Word addr, bool rmw) {
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
void unconfigure378(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    for (int idx=0x6000; idx<0x8000; ++idx) {
        pReads[idx] = readZero;
        pWrites[idx] = writeZero;
    }
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configure378(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // takes over the address space for >6000-7FFF
    for (int idx=0x6000; idx<0x8000; ++idx) {
        pReads[idx] = read378;
        pWrites[idx] = write378;
    }

    return unconfigure378;
}
