// Handles the MBX cartridge format
// MBX is weird. The lower 4k is fixed, but the top 1k of that is RAM
// The upper 4k is bank switched. Address >6FFE has a bank switch
// register updated from the data bus. (Doesn't use 'bits')

#include <malloc.h>
#include "hwTypes.h"

// memory (small enough to keep it allocated)
Byte mbx_ram[1024];							// MBX cartridge RAM (1k)

// TODO: move MBX ROM into here
// TODO: give it its own banking latch
// TOOD: RAM should use the hw_ramCart type

void writeMBXRAM(Word addr, Byte data) {
	mbx_ram[addr-0x6c00] = data;
}

void writeMBXRegister(Word addr, Byte data) {
	xbBank = addr&xb;
	// the theory is this also writes to RAM
	mbx_ram[addr-0x6c00] = data;
}

Byte readMBXFixed(Word addr, bool rmw) {
	// MBX is weird. The lower 4k is fixed, but the top 1k of that is RAM
	// The upper 4k is bank switched. Address >6FFE has a bank switch
	// register updated from the data bus.
	if ((addr>=0x6C00)&&(addr<0x6FFE)) {
		return mbx_ram[addr-0x6c00];				// MBX RAM
	} else if (addr < 0x6c00) {
		return CPU2[addr-0x6000];					// MBX fixed ROM
	} else {
		return(CPU2[(xbBank<<13)+(addr-0x6000)]);	// MBX paged ROM	// TODO: isn't this 8k paging? Why does this work? What do the ROMs look like?
	}
	// anything else is ignored
    return 0;
}

Byte readMBXBanked(Word addr, bool rmw) {
	// MBX is weird. The lower 4k is fixed, but the top 1k of that is RAM
	// The upper 4k is bank switched. Address >6FFE has a bank switch
	// register updated from the data bus.
	if ((addr>=0x6C00)&&(addr<0x6FFE)) {
		return mbx_ram[addr-0x6c00];				// MBX RAM
	} else if (addr < 0x6c00) {
		return CPU2[addr-0x6000];					// MBX fixed ROM
	} else {
		return(CPU2[(xbBank<<13)+(addr-0x6000)]);	// MBX paged ROM	// TODO: isn't this 8k paging? Why does this work? What do the ROMs look like?
	}
	// anything else is ignored
    return 0;
}

Byte readMBXRAM(Word addr, bool rmw) {
	// MBX is weird. The lower 4k is fixed, but the top 1k of that is RAM
	// The upper 4k is bank switched. Address >6FFE has a bank switch
	// register updated from the data bus.
	if ((addr>=0x6C00)&&(addr<0x6FFE)) {
		return mbx_ram[addr-0x6c00];				// MBX RAM
	} else if (addr < 0x6c00) {
		return CPU2[addr-0x6000];					// MBX fixed ROM
	} else {
		return(CPU2[(xbBank<<13)+(addr-0x6000)]);	// MBX paged ROM	// TODO: isn't this 8k paging? Why does this work? What do the ROMs look like?
	}
	// anything else is ignored
    return 0;
}

// called to deconfigure/free RAM/etc
void unconfigureMBX(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    for (int idx=0x6000; idx<0x6C00; ++idx) {
        pReads[idx] = readZero;
    }
    for (int idx=0x6C00; idx<0x6FFE; ++idx) {
        pReads[idx] = readZero;
        pWrites[idx] = writeZero;
    }
    for (int idx=0x7000; idx<0x8000; ++idx) {
        pReads[idx] = readZero;
    }
    pReads[0x6ffe] = readZero;
    pReads[0x6fff] = readZero;
    pWrites[0x6ffe] = writeZero;
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureMBX(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    for (int idx=0x6000; idx<0x6C00; ++idx) {
        pReads[idx] = readMBXFixed;
    }
    for (int idx=0x6C00; idx<0x6FFE; ++idx) {
        pReads[idx] = readMBXRAM;
        pWrites[idx] = writeMBXRAM;
    }
    for (int idx=0x7000; idx<0x8000; ++idx) {
        pReads[idx] = readMBXBanked;
    }
    // TODO: This register is unclear - does it also fall through to RAM?
    pReads[0x6ffe] = readMBXRAM;
    pReads[0x6fff] = readMBXRAM;
    pWrites[0x6ffe] = writeMBXRegister;

    return unconfigureMBX;
}
