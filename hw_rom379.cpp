// Handles the a 379 (inverted) bank switched cartridge

#include <malloc.h>
#include "hwTypes.h"

// TODO: split out 379 and 379, so we don't need to invert at load time (maybe already done?)
// TODO: load the data here

void write379(Word addr, Byte data) {
    // collect bits from address and data buses - x is address, c is data
    // TODO: do I want to include the data bus too? It works the same way...?
    int bits = (c<<13)|(x&0x1fff);

    // uses inverted address lines!
	xbBank=(((~bits)>>1)&xb);		// XB bank switch, up to 4096 banks
}

Byte read379(Word addr, bool rmw) {
	// XB is supposed to only page the upper 4k, but some Atari carts seem to like it all
	// paged. Most XB dumps take this into account so only full 8k paging is implemented.
	if (xb) {
        // make sure xbBank never exceeds xb
		return(CPU2[(xbBank<<13)+(addr-0x6000)]);	// cartridge bank 2 and up
	} else {
		return readRom(addr, !rmw);			// cartridge bank 1
	}
}

// called to deconfigure/free RAM/etc
void unconfigure379(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    for (int idx=0x6000; idx<0x8000; ++idx) {
        pReads[idx] = readZero;
        pWrites[idx] = writeZero;
    }
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
// TODO: maybe the configure function can take in the config line too - then it can decide whether to
// accept it, configure and load its own data...?
// To that end, I kind of think pReads, pWrites, etc should just be globals...
unconfigFunc configure379(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // takes over the address space for >6000-7FFF
    for (int idx=0x6000; idx<0x8000; ++idx) {
        pReads[idx] = read379;
        pWrites[idx] = write379;
    }

    return unconfigure379;
}
