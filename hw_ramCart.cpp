// This handles RAM in the cartridge memory space
// centralizing it here lets us make the nvram concept
// a little more centralized

#include <malloc.h>
#include "hwTypes.h"

// if cartridge RAM is written to (also needs an NVRAM type to be saved)
bool nvRamUpdated = false;		

// memory - we always allocate the full 8k range
Byte *pRAM = NULL;

// triggered by a write to >8xxx
void writeScratch(Word addr, Byte data) {
    pRam[addr&0x1fff] = data;
    nvRamUpdated = true;
}

// triggered by a read to >2xxx
Byte readScratch(Word addr, bool rmw) {
    return pRam[addr&0x1fff];
}

// called to deconfigure/free RAM/etc
// we assume EVERYONE is being unloaded, so it's okay if we just clear the whole cart space
void unconfigureCartRam(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // TODO: save the NVRAM to disk here if nvRamUpdated is true
    for (int idx=0x8000; idx<0x8400; ++idx) {
        pReads[idx] = readZero;
        pWrites[idx] = writeZero;
    }

    if (NULL != pRam) {
        free(pRam);
        pRam = NULL;
    }
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureCartRam(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    if (pRAM == NULL) {
        pRAM = (Byte*)malloc(8*1024);
    }
    if (NULL == pRam) {
        return NULL;
    }

    // configure all cartridge space - split carts can override the rest
    for (int idx=0x6000; idx<0x8000; ++idx) {
        pReads[idx] = readCartRam;
        pWrites[idx] = writeCartRam;
    }

    // TODO: load the NVRAM from disk here - make sure ROM is always configured second so it overrides the RAM setting

    return unconfigureScratchpad;
}
