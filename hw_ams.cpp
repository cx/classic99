// Handles the AMS interface

#include <malloc.h>
#include "hwTypes.h"

// TODO: move the AMS code into here... for now just interface to it
// TODO: configure the AMS size (256k, 512k, 1MB, 32MB)

// convert config into meaningful values for AMS system
void SetupSams(int sams_mode, int sams_size) {
	EmulationMode emuMode = None;
	AmsMemorySize amsSize = Mem128k;
	
	// TODO: We don't really NEED this translation layer, but we can remove it later.
	if (sams_mode) {
		// currently only SuperAMS, so if anything set use that
		emuMode = Sams;

		switch (sams_size) {
		case 1:
			amsSize = Mem256k;
			break;
		case 2:
			amsSize = Mem512k;
			break;
		case 3:
			amsSize = Mem1024k;
			break;
		default:
			break;
		}
	}

	SetAmsMemorySize(amsSize);
	InitializeMemorySystem(emuMode);
	SetMemoryMapperMode(Map);
}

// triggered by a write to >8xxx
void writeAMS(Word addr, Byte data) {
	WriteMemoryByte(addr, data, false);
}

// triggered by a read to >2xxx
Byte readAMS(Word addr, bool rmw) {
    return ReadMemoryByte(addr, !rmw);
}

void writeAMSRegisters(Word addr, Byte data) {
	// registers are selected by A11 through A14 when in
	// the >4000->5FFF range, but this function does a little
	// extra shifting itself. (We may want more bits later for
	// Thierry's bigger AMS card hack, but not for now).
	if (MapperRegistersEnabled()) {
		// 0000 0000 000x xxx0
		Byte reg = (addr & 0x1e) >> 1;
		bool hiByte = ((addr & 1) == 0);	// registers are 16 bit!
		WriteMapperRegisterByte(reg, data, hiByte);
	}
}

Byte readAMSRegisters(Word addr, bool rmw) {
	// registers are selected by A11 through A14 when in
	// the >4000->5FFF range, but this function does a little
	// extra shifting itself. (We may want more bits later for
	// Thierry's bigger AMS card hack, but not for now).
	if (MapperRegistersEnabled()) {
		// 0000 0000 000x xxx0
		Byte reg = (addr & 0x1e) >> 1;
		bool hiByte = ((addr & 1) == 0);		// 16 bit registers!
		return ReadMapperRegisterByte(reg, hiByte);
	}
	return 0;
}

// called to deconfigure/free RAM/etc
void unconfigureAMS(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
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
unconfigFunc configureAMS(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // takes over the 32k of RAM space
    // takes over the address space for both >2xxx and >A000-FFFF
    for (int idx=0; idx<0x2000; ++idx) {
        pReads[idx+0x2000] = readAMS;
        pWrites[idx+0x2000] = writeAMS;
    }
    for (int idx=0; idx<0x6000; ++idx) {
        pReads[idx+0xA000] = readAMS;
        pWrites[idx+0xA000] = writeAMS;
    }

    // Prepare the AMS system
    SetupSams(sams_enabled, sams_size);	

    return unconfigureAMS;
}
