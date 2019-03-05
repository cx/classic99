// Handles the 32k RAM expansion
// TOOD: add an option for 16-bit RAM speeds
// For 16 bit mode, just execute:
//   if ((!rmw) && ((x & 0x01) == 0))
//     pCurrentCPU->AddCycleCount(-4);
// This will cancel out the automatic memory access cycles

#include <malloc.h>
#include "hwTypes.h"

// memory slots
Byte *lowRam = NULL;    // 8k at >2000
Byte *highRam = NULL;   // 24k at >A000

// triggered by a write to >2xxx
void writeLow(Word addr, Byte data) {
    lowRam[addr-0x2000] = data;
}

// triggered by a read to >2xxx
Byte readLow(Word addr, bool rmw) {
    return lowRam[addr-0x2000];
}

// triggered by a write to >Axxx
void writeHigh(Word addr, Byte data) {
    highRam[addr-0xa000] = data;
}

// triggered by a read to >Axxx
Byte readHigh(Word addr, bool rmw) {
    return highRam[addr-0xa000];
}

// called to deconfigure/free RAM/etc
void unconfigureRAM32k(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    free(lowRam);
    lowRam = NULL;
    free(highRam);
    highRam = NULL;

    for (int idx=0; idx<0x2000; ++idx) {
        pReads[idx+0x2000] = readZero;
        pWrites[idx+0x2000] = writeZero;
    }
    for (int idx=0; idx<0x6000; ++idx) {
        pReads[idx+0xA000] = readZero;
        pWrites[idx+0xA000] = writeZero;
    }

}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureGigacartProgram(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    if (NULL == lowRam) {
        lowRam = (Byte*)malloc(8*1024);
        if (NULL == lowRam) return NULL;
    }

    if (NULL == highRam) {
        highRam = (Byte*)malloc(24*1024);
        if (NULL == highRam) {
            free(lowRam);
            return false;
        }
    }

    // takes over the address space for both >2xxx and >A000-FFFF
    for (int idx=0; idx<0x2000; ++idx) {
        pReads[idx+0x2000] = readLow;
        pWrites[idx+0x2000] = writeLow;
    }
    for (int idx=0; idx<0x6000; ++idx) {
        pReads[idx+0xA000] = readHigh;
        pWrites[idx+0xA000] = writeHigh;
    }

    return unconfigureRAM32k;
}
