// Handles loaded ROM, if it's in the base memory space
// Normally this will just be the console ROMs, and maybe also
// cartridge ROM (if not banked).

#include <malloc.h>
#include <string.h>
#include "hwTypes.h"

// memory slots - always allocated
Byte romSpace[64*1024];
bool claimed[64*1024];

// TODO: can do away with ROMMAP since each handler knows what it's supposed to do

// triggered by a write to ROM
void writeRom(Word addr, Byte data) {
    // do nothing for writes to ROM
}

// triggered by a read to ROM
Byte readRom(Word addr, bool rmw) {
    return romSpace[addr];
}

// called from the ROM loader to copy data in and update the read functions
void copyIntoRom(Word addr, Byte *data, int size, readFunc *pReads, writeFunc *pWrites) {
    // takes over the address space for both >2xxx and >A000-FFFF
    for (int idx=0; idx<size; ++idx) {
        pReads[idx+addr] = readRom;
        pWrites[idx+addr] = writeRom;
        claimed[idx+addr] = true;
    }
}

// called to deconfigure/free RAM/etc
void unconfigureRom(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // nothing to do here
    for (int idx=0; idx<0x10000; ++idx) {
        if (claimed[idx]) {
            pReads[idx] = readZero;
            pWrites[idx] = writeZero;
            claimed[idx] = false;
        }
    }
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureRom(readFunc * /*pReads*/, writeFunc * /*pWrites*/, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // There's no memory allocation, and we don't update the ROM functions
    // until data is actually loaded
    memset(claimed, 0, sizeof(claimed));
    return unconfigureRom;
}
