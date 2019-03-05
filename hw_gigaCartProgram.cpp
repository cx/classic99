// This file emulates the programmer for the Gigacart
// This is a hardware modification to the console - it requires
// that the GROM select pin is remapped to the >E000 memory space
//
// There is also a special CPLD load where the ROM side at >6000
// operates normally, with the addition of three extra bits.
//
// Writes to >E000 run directly to the flash chip (no reads),
// with gates for LSB/MSB, and that allows for direct access.
//
// This file emulates the flash interface so I can write and test
// programming software.
//

#include <malloc.h>
#include "hwTypes.h"

// CPLD emulation
bool flashInReset = true;   // triggered by data bit >10
bool msbWriteOkay = false;  // triggered by data bit >08
bool msbReadOkay = false;   // triggered by data bit >04
unsigned int latch = 0;     // triggered by address bits 0x1FFE and data bits 0x03

// flash chip modes
enum CHIPMODE {
    MODE_RESET = 0,         // reset chip
    MODE_READ,              // normal boot mode
    MODE_READCFI,           // reading the CFI data
    MODE_COMMAND,           // after an unlock
    MODE_CHIPERASE,         // erasing the chip
    MODE_SECTORERASE,       // erasing a sector
    MODE_BUFFERWRITE,       // filling a buffer
    MODE_WRITING,           // writing flash
};

// flash chip emulation
int  chipMode = MODE_RESET;     // modes above
bool unlockSequence1 = false;   // 0xaaa = 0xaa
bool unlockSequence2 = false;   // 0x554 = 0x55
bool accelerateUnlock = false;  // command 0xaaa = 0x20
int writeCountdown = 0;         // counting down write functions
Byte flashBuffer[32];           // write buffer
int bufferCountdown = 0;        // number of bytes remaining in a buffer write
Byte *flashSpace = NULL;        // allocated memory for the flash chip itself

// triggered by a write to >6xxx
void writeCpld(Word addr, Byte data) {

}

// triggered by a read to >6xxx
Byte readCpld(Word addr, bool rmw) {

}

// triggered by a write to >Exxx
void writeFlash(Word addr, Byte data) {

}

// triggered by a read to >Exxx
Byte readFlash(Word addr, bool rmw) {
    // the CPLD blocks reads directly to the flash
    return 0;
}

void unconfigureGigacartProgram(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    free(flashSpace);
    flashSpace = NULL;

    for (int idx=0; idx<0x2000; ++idx) {
        pReads[idx+0x6000] = readZero;
        pReads[idx+0xE000] = readZero;
        pWrites[idx+0x6000] = writeZero;
        pWrites[idx+0xE000] = writeZero;
    }
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureGigacartProgram(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    if (NULL == flashSpace) {
        flashSpace = (Byte*)malloc(128*1024*1024);  // 128MB flash
        if (NULL == flashSpace) return false;
    }

    // takes over the address space for both >6xxx and >Exxx
    for (int idx=0; idx<0x2000; ++idx) {
        pReads[idx+0x6000] = readCpld;
        pReads[idx+0xE000] = readFlash;
        pWrites[idx+0x6000] = writeCpld;
        pWrites[idx+0xE000] = writeFlash;
    }

    return unconfigureGigacartProgram;
}

