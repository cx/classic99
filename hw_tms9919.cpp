// Handles the sound chip interface at >84xx

#include <malloc.h>
#include "hwTypes.h"

////////////////////////////////////////////////////////////////
// Write a byte to the sound chip
// Nice notes at http://www.smspower.org/maxim/docs/SN76489.txt
////////////////////////////////////////////////////////////////
void wsndbyte(Byte c)
{
	unsigned int x, idx;								// temp variable
	static int oldFreq[3]={0,0,0};						// tone generator frequencies

	if (NULL == lpds) return;

	// 'c' contains the byte currently being written to the sound chip
	// all functions are 1 or 2 bytes long, as follows					
	//
	// BYTE		BIT		PURPOSE											
	//	1		0		always '1' (latch bit)							
	//			1-3		Operation:	000 - tone 1 frequency				
	//								001 - tone 1 volume					
	//								010 - tone 2 frequency				
	//								011 - tone 2 volume					
	//								100 - tone 3 frequency				
	//								101 - tone 3 volume					
	//								110 - noise control					
	//								111 - noise volume					
	//			4-7		Least sig. frequency bits for tone, or volume	
	//					setting (0-F), or type of noise.				
	//					(volume F is off)								
	//					Noise set:	4 - always 0						
	//								5 - 0=periodic noise, 1=white noise 
	//								6-7 - shift rate from table, or 11	
	//									to get rate from voice 3.		
	//	2		0-1		Always '0'. This byte only used for frequency	
	//			2-7		Most sig. frequency bits						
	//
	// Commands are instantaneous

	// Latch anytime the high bit is set
	// This byte still immediately changes the channel
	if (c&0x80) {
		latch_byte=c;
	}

	switch (c&0xf0)										// check command
	{	
	case 0x90:											// Voice 1 vol
	case 0xb0:											// Voice 2 vol
	case 0xd0:											// Voice 3 vol
	case 0xf0:											// Noise volume
		setvol((c&0x60)>>5, c&0x0f);
		break;

	case 0xe0:
		x=(c&0x07);										// Noise - get type
		setfreq(3, c&0x07);
		break;

//	case 0x80:											// Voice 1 frequency
//	case 0xa0:											// Voice 2 frequency
//	case 0xc0:											// Voice 3 frequency
	default:											// Any other byte
		int nChan=(latch_byte&0x60)>>5;
		if (c&0x80) {
			// latch write - least significant bits of a tone register
			// (definately not noise, noise was broken out earlier)
			oldFreq[nChan]&=0xfff0;
			oldFreq[nChan]|=c&0x0f;
		} else {
			// latch clear - data to whatever is latched
			// TODO: re-verify this on hardware, it doesn't agree with the SMS Power doc
			// as far as the volume and noise goes!
			if (latch_byte&0x10) {
				// volume register
				setvol(nChan, c&0x0f);
			} else if (nChan==3) {
				// noise register
				setfreq(3, c&0x07);
			} else {
				// tone generator - most significant bits
				oldFreq[nChan]&=0xf;
				oldFreq[nChan]|=(c&0x3f)<<4;
			}
		}
		setfreq(nChan, oldFreq[nChan]);
		break;
	}
}

// triggered by a write
void writeTms9919(Word addr, Byte data) {
	wsndbyte(data);
    // sound chip writes eat ~28 additional cycles (verified hardware, reads do not)
    pCurrentCPU->AddCycleCount(28);
}

// called to deconfigure/free RAM/etc
void unconfigureTMS9919(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    for (int idx=0x8400; idx<0x8800; ++idx) {
        pWrites[idx] = writeZero;
    }
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureTMS9919(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // responds to even addresses only
    for (int idx=0x8400; idx<0x8800; idx+=2) {
        pWrites[idx] = writeTms9919;
    }

    return unconfigureTMS9919;
}
