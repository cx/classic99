// Handles the speech synthesis interface

#include <malloc.h>
#include "hwTypes.h"

//////////////////////////////////////////////////////
// Speech Update function - runs every x instructions
// Pass in number of samples to process.
//////////////////////////////////////////////////////
void SpeechUpdate(int nSamples) {
	if ((speechbuf==NULL) || (SpeechProcess == NULL)) {
		// nothing to write to, so don't bother to halt
		CPUSpeechHalt=false;
		pCPU->StopHalt(HALT_SPEECH);
		return;
	}

	if (nSpeechTmpPos+nSamples >= SPEECHRATE*2) {
		// in theory, this should not happen, though it may if we
		// run the 9900 very fast
//			debug_write("Speech buffer full... dropping");
		return;
	}

	SpeechProcess((unsigned char*)&SpeechTmp[nSpeechTmpPos], nSamples);
	nSpeechTmpPos+=nSamples;
}

//
// Function to copy the speech audio buffer
//
void SpeechBufferCopy() {
	DWORD iRead, iWrite;
	Byte *ptr1, *ptr2;
	DWORD len1, len2;
	static DWORD lastRead=0;

	if (nSpeechTmpPos == 0) {
		// no data to write
		return;
	}

	// just for statistics
	speechbuf->GetCurrentPosition(&iRead, &iWrite);
//	debug_write("Read/Write bytes: %5d/%5d", iRead-lastRead, nSpeechTmpPos*2);
	lastRead=iRead;

	if (SUCCEEDED(speechbuf->Lock(iWrite, nSpeechTmpPos*2, (void**)&ptr1, &len1, (void**)&ptr2, &len2, DSBLOCK_FROMWRITECURSOR))) 
	{
		memcpy(ptr1, SpeechTmp, len1);
		if (len2 > 0) {							// handle wraparound
			memcpy(ptr2, &SpeechTmp[len1/2], len2);
		}
		speechbuf->Unlock(ptr1, len1, ptr2, len2);	
		
		// reset the buffer
		nSpeechTmpPos=0;
	} else {
//		debug_write("Speech buffer lock failed");
		// don't reset the buffer, we may get it next time
	}
}

// triggered by a write to >8xxx
void writeSpeech(Word addr, Byte data) {
	static int cnt = 0;

	if ((SpeechWrite)&&(SpeechEnabled)) {
		if (!SpeechWrite(data, CPUSpeechHalt)) {
			if (!CPUSpeechHalt) {
				debug_write("Speech halt triggered.");
				CPUSpeechHalt=true;
				CPUSpeechHaltByte=data;
				pCPU->StartHalt(HALT_SPEECH);
				cnt = 0;
			} else {
				cnt++;
			}
		} else {
			// must be unblocked!
			if (CPUSpeechHalt) {
				debug_write("Speech halt cleared at %d cycles.", cnt*10);
			}
			// always clear it, just to be safe
			pCPU->StopHalt(HALT_SPEECH);
			CPUSpeechHalt = false;
			cnt = 0;
		}
        // speech chip, if attached, writes eat 64 additional cycles (verified hardware)
        // TODO: not verified if this is still true after a halt occurs, but since a halt
        // is variable length, maybe it doesn't matter...
		pCurrentCPU->AddCycleCount(64);
	}
}

// triggered by a read to >2xxx
Byte readSpeech(Word addr, bool rmw) {
	Byte ret=0;

	if ((SpeechRead)&&(SpeechEnabled)) {
		ret=SpeechRead();
        // speech chip, if attached, reads eat 48 additional cycles (verified hardware)
		pCurrentCPU->AddCycleCount(48);
	}

	return ret;
}

// called to deconfigure/free RAM/etc
void unconfigureSpeech(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    for (int idx=0x9000; idx<0x9400; idx+=2) {
        pReads[idx] = readZero;
    }
    for (int idx=0x9400; idx<0x9800; idx+=2) {
        pWrites[idx] = writeZero;
    }
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureSpeech(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // takes over the address space for >9000-97FF, even only
    for (int idx=0x9000; idx<0x9400; idx+=2) {
        pReads[idx] = readSpeech;
    }
    for (int idx=0x9400; idx<0x9800; idx+=2) {
        pWrites[idx] = writeSpeech;
    }

    return unconfigureSpeech;
}

