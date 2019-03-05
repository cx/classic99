// this is just the big array hack handler - it's Classic99 only and probably should be deleted
// This hack has only a handful of registers in cartridge space and has never been ported to hardware:
//
// write: 0x7ffd - sets one byte of the 32-bit address register (shifting the old data up 8 bits)
// read : 0x6000 - resets the address counter to 0x00000000
// read : 0x7ffb - reads the MSB of the address register (and shifts it up 8 bits)
// read : 0x7fff - read next byte of data, with wraparound at maximum (whatever the loaded filesize was)
//
// TODO: keep only if we need it for videostepper...

#include <malloc.h>
#include "hwTypes.h"

void writeBigAddress(Word addr, Byte data) {
	if (BIGARRAYSIZE > 0) {
		// write the address register
		BIGARRAYADD = (BIGARRAYADD<<8) | data;
		debug_write("(Write) Big array address now 0x%08X", BIGARRAYADD);
	}

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

Byte readBigAddress(Word addr, bool rmw) {
    Byte ret = 0;
    if ((BIGARRAYSIZE > 0) && (!rmw)) {
	    // destructive address read - MSB first for consistency
	    ret = (BIGARRAYADD >> 24) & 0xff;
	    BIGARRAYADD <<= 8;
	    debug_write("(Read) Big array address now 0x%08X", BIGARRAYADD);
    }
	return ret;
}

Byte readBigData(Word addr, bool rmw) {
    Byte ret = 0;
    if ((BIGARRAYSIZE > 0) && (!rmw)) {
	    if (BIGARRAYADD >= BIGARRAYSIZE) {
		    // TODO: real hardware probably won't do this either.
		    BIGARRAYADD = 0;
	    }
	    ret = BIGARRAY[BIGARRAYADD++];
    }
	return ret;
}

Byte resetBigArray(Word addr, bool rmw) {
	if (BIGARRAYSIZE > 0) {
		// TODO: real hardware probably will not do this. Don't count on the address being reset.
		debug_write("Reset big array address");
		BIGARRAYADD = 0;
	}
}

// called to deconfigure/free RAM/etc
void unconfigureBigArray(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    pReads[0x7ffb] = readZero;
    pReads[0x7fff] = readZero;
    pReads[0x6000] = readZero;
    pWrites[0x7ffd] = writeZero;
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureBigArray(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
#ifdef USE_BIG_ARRAY
    pReads[0x7ffb] = readBigAddress;
    pReads[0x7fff] = readBigData;
    pReads[0x6000] = resetBigAddress;
    pWrites[0x7ffd] = writeBigAddress;

    return unconfigureBigArray;
#else
    return NULL;
#endif
}
