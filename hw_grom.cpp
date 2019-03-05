//////////////////////////////////////////////////////////////////
// Handles the GROM interface
// GROM base 0 (console GROMS) manage all address operations
//////////////////////////////////////////////////////////////////

#include <malloc.h>
#include "hwTypes.h"

// NOTE: Classic99 does not emulate the 6k GROM behaviour that causes mixed
// data in the range between 6k and 8k - because Classic99 assumes all GROMS
// are actually 8k devices. It will return whatever was in the ROM data it
// loaded (or zeros if no data was loaded there). I don't intend to reproduce
// this behaviour (but I can certainly conceive of using it for copy protection,
// if only real GROMs could still be manufactured...)
// TODO: actually, we can easily incorporate it by generating the extra 2k when we
// load a 6k grom... but it would only work if we loaded 6k groms instead of
// the combined banks lots of people use. But at least we'd do it.

// Note that UberGROM access time (in the pre-release version) was 15 cycles (14.6), but it
// does not apply as long as other GROMs are in the system (and they have to be due to lack
// of address counter.) So this is still valid.

void writeGromAddress(Word addr, Byte data) {
    scratchpad[addr&0xff] = data;
}

void writeGromData(Word addr, Byte data) {
    wgrmbyte(x,c);
}

Byte readGromAddress(Word addr, bool rmw) {
    Byte z;

    // address
	GROMBase[0].grmaccess=2;
	z=(GROMBase[0].GRMADD&0xff00)>>8;
	// read is destructive
	GROMBase[0].GRMADD=(((GROMBase[0].GRMADD&0xff)<<8)|(GROMBase[0].GRMADD&0xff));		
	// TODO: Is the address incremented anyway? ie: if you keep reading, what do you get?

    // GROM read address always adds about 13 cycles
	pCurrentCPU->AddCycleCount(13);

	return(z);
}

Byte readGromData(Word addr, bool rmw) {
	int nBank;
    Byte z;

    // if the address is less than >6000, then it's a console GROM
    // which is not banked.
	if ((grombanking) && ((Word)(GROMBase[0].GRMADD-1) >= 0x6000)) {
		nBank=(x&0x3ff)>>2;								// maximum possible range to >9BFF - not all supported here though
		if (nBank >= PCODEGROMBASE) {
			debug_write("Invalid GROM base 0x%04X read", x);
			return 0;
		}
//	    if (nBase > 0) {
//		    debug_write("Read GROM base %d(>%04X), >%04x, >%02x", nBase, x, GROMBase[0].GRMADD, GROMBase[nBase].grmdata);
//	    }
    } else {
		nBank=0;
	}

	if (!rmw) {
		// Check for breakpoints
		for (int idx=0; idx<nBreakPoints; idx++) {
			switch (BreakPoints[idx].Type) {
				case BREAK_READGROM:
					if (CheckRange(idx, GROMBase[0].GRMADD-1)) {
						TriggerBreakPoint();
					}
					break;
			}
		}
	}

	// data
	UpdateHeatGROM(GROMBase[0].GRMADD);

    // this saves some debug off for Rich
    GROMBase[0].LastRead = GROMBase[0].GRMADD;
    GROMBase[0].LastBase = nBank;

	GROMBase[0].grmaccess=2;
	z=GROMBase[nBank].grmdata;

	// a test for the Distorter project - special cases - GROM base is always 0 for console GROMs!
	if (bMpdActive) {
		z=GetMpdOverride(GROMBase[0].GRMADD - 1, z);
		// the rest of the MPD works like MESS does, copying data into the GROM array. Less efficient, better for debug though
	}
	if ((bUberGROMActive) && ((Word)(GROMBase[0].GRMADD-1) >= 0x6000)) {
		z=UberGromRead(GROMBase[0].GRMADD-1, nBank);
	}

	// update all bases prefetch
	for (int idx=0; idx<PCODEGROMBASE; idx++) {
		GROMBase[idx].grmdata=GROMBase[idx].GROM[GROMBase[0].GRMADD];
	}

    // TODO: This is not correct emulation for the gigacart, which ACTUALLY maintains
    // an 8-bit address latch and a 1 bit select (for GROM >8000)
    // But it's enough to let me test some theories...
   	GROMBase[0].GRMADD++;

    // GROM read data always adds about 19 cycles
	pCurrentCPU->AddCycleCount(19);

	return(z);
}

// called to deconfigure/free RAM/etc
void unconfigureGrom(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    for (int idx=0x9800; idx<0x9C00; idx+=4) {
        pReads[idx] = readZero;
        pReads[idx+2] = readZero;
    }
    for (int idx=0x9C00; idx<0xA000; idx+=4) {
        pWrites[idx] = writeZero;
        pWrites[idx+2] = writeZero;
    }
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureGrom(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // fill in the GROM accesses, even address only
    for (int idx=0x9800; idx<0x9C00; idx+=4) {
        pReads[idx] = readGromData;
        pReads[idx+2] = readGromAddress;
    }
    for (int idx=0x9C00; idx<0xA000; idx+=4) {
        pWrites[idx] = writeGromData;
        pWrites[idx+2] = writeGromAddress;
    }

    return unconfigureGrom;
}
