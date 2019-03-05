// Handles the 256 byte scratchpad RAM expansion

#include <malloc.h>
#include "hwTypes.h"

// memory
Byte scratchpad[256];   // we always need this, so no need to alloc

// triggered by a write to >8xxx
void writeScratch(Word addr, Byte data) {
    scratchpad[addr&0xff] = data;
}

// triggered by a read to >2xxx
Byte readScratch(Word addr, bool rmw) {
    return scratchpad[addr&0xff];
}

// called to deconfigure/free RAM/etc
void unconfigureScratchpad(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    for (int idx=0x8000; idx<0x8400; ++idx) {
        pReads[idx] = readZero;
        pWrites[idx] = writeZero;
    }
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureScratchpad(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // takes over the address space for >8000-83FF
    for (int idx=0x8000; idx<0x8400; ++idx) {
        pReads[idx] = readScratch;
        pWrites[idx] = writeScratch;
    }

    return unconfigureScratchpad;
}
