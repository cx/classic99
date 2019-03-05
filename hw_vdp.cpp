// Handles the VDP chip interface at >88xx,>8c00

#include <malloc.h>
#include "hwTypes.h"

//////////////////////////////////////////////////////
// Increment VDP Address
//////////////////////////////////////////////////////
void increment_vdpadd() 
{
	VDPADD=(++VDPADD) & 0x3fff;
}

//////////////////////////////////////////////////////
// Return the actual 16k address taking the 4k mode bit
// into account.
//////////////////////////////////////////////////////
int GetRealVDP() {
	int RealVDP;

	// force 4k drams (16k bit not emulated)
//	return VDPADD&0x0fff;

	// The 9938 and 9958 don't honor this bit, they always assume 128k (which we don't emulate, but we can at least do 16k like the F18A)
	// note that the 128k hack actually does support 128k now... as needed. So if bEnable128k is on, we take VDPREG[14]&0x07 for the next 3 bits.
	if ((bEnable80Columns) || (VDPREG[1]&0x80)) {
		// 16k mode - address is 7 bits + 7 bits, so use it raw
		// xx65 4321 0654 3210
		// This mask is not really needed because VDPADD already tracks only 14 bits
		RealVDP = VDPADD;	// & 0x3FFF;

		if (bEnable128k) {
			RealVDP|=VDPREG[14]<<14;
		}
		
	} else {
		// 4k mode -- address is 6 bits + 6 bits, but because of the 16k RAMs,
		// it gets padded back up to 7 bit each for row and col
		// The actual method used is a little complex to describe (although
		// I'm sure it's simple in silicon). The lower 6 bits are used as-is.
		// The next /7/ bits are rotated left one position.. not really sure
		// why they didn't just do a 6 bit shift and lose the top bit, but
		// this does seem to match every test I throw at it now. Finally, the
		// 13th bit (MSB for the VDP) is left untouched. There are no fixed bits.
		// Test values confirmed on real console:
		// 1100 -> 0240
		// 1810 -> 1050
		// 2210 -> 2410
		// 2211 -> 2411
		// 2240 -> 2280
		// 3210 -> 2450
		// 3810 -> 3050
		// Of course, only after working all this out did I look at Sean Young's
		// document, which describes this same thing from the hardware side. Those
		// notes confirm mine.
		//
		//         static bits       shifted bits           rotated bit
		RealVDP = (VDPADD&0x203f) | ((VDPADD&0x0fc0)<<1) | ((VDPADD&0x1000)>>7);
	}

	// force 8k DRAMs (strip top row bit - this should be right - console doesn't work though)
//	RealVDP&=0x1FFF;

// To watch the console VDP RAM detect code
//	if (GROMBase[0].GRMADD < 0x100) {
//		debug_write("VDP Address prefetch %02X from %04X, real address %04X", vdpprefetch, VDPADD-1, RealVDP-1);
//	}

	return RealVDP;
}

////////////////////////////////////////////////////////////////
// Write to VDP Register
////////////////////////////////////////////////////////////////
void wVDPreg(Byte r, Byte v)
{ 
	int t;

	if (r > 58) {
		debug_write("Writing VDP register more than 58 (>%02X) ignored...", r);
		return;
	}

	VDPREG[r]=v;

	// check breakpoints against what was written to where
	for (int idx=0; idx<nBreakPoints; idx++) {
		switch (BreakPoints[idx].Type) {
			case BREAK_EQUALS_VDPREG:
				if ((r == BreakPoints[idx].A) && ((v&BreakPoints[idx].Mask) == BreakPoints[idx].Data)) {
					TriggerBreakPoint();
				}
				break;
		}
	}

	if (r==7)
	{	/* color of screen, set color 0 (trans) to match */
		/* todo: does this save time in drawing the screen? it's dumb */
		t=v&0xf;
		if (t) {
			F18APalette[0]=F18APalette[t];
		} else {
			F18APalette[0]=0x000000;	// black
		}
		redraw_needed=REDRAW_LINES;
	}

	if (!bEnable80Columns) {
		// warn if setting 4k mode - the console ROMs actually do this often! However,
		// this bit is not honored on the 9938 and later, so is usually set to 0 there
		if ((r == 1) && ((v&0x80) == 0)) {
			// ignore if it's a console ROM access - it does this to size VRAM
			if (pCurrentCPU->GetPC() > 0x2000) {
				debug_write("WARNING: Setting VDP 4k mode at PC >%04X", pCurrentCPU->GetPC());
			}
		}
	}

	// for the F18A GPU, copy it to RAM
	VDP[0x6000+r]=v;
}

void writeVdpData(Word addr, Byte data) {
	/* write data */
	// TODO: cold reset incompletely resets the F18 state - after TI Scramble runs we see a sprite on the master title page
	// Added by RasmusM
	// Write data to F18A palette registers
	if (bF18AActive && bF18ADataPortMode) {
		if (F18APaletteRegisterData == -1) {
			// Read first byte
			F18APaletteRegisterData = data;
		}
		else {
			// Read second byte
			{
				int r=(F18APaletteRegisterData & 0x0f);
				int g=(data & 0xf0)>>4;
				int b=(data & 0x0f);
				F18APalette[F18APaletteRegisterNo] = (r<<20)|(r<<16)|(g<<12)|(g<<8)|(b<<4)|b;	// double up each palette gun, suggestion by Sometimes99er
				redraw_needed = REDRAW_LINES;
			}
			debug_write("F18A palette register >%02X set to >%04X", F18APaletteRegisterNo, F18APalette[F18APaletteRegisterNo]);
			if (bF18AAutoIncPaletteReg) {
				F18APaletteRegisterNo++;
			}
			// The F18A turns off DPM after each register is written if auto increment is off
			// or after writing to last register if auto increment in on
			if ((!bF18AAutoIncPaletteReg) || (F18APaletteRegisterNo == 64)) {
				bF18ADataPortMode = 0;
				F18APaletteRegisterNo = 0;
				debug_write("F18A Data port mode off (auto).");
			}
			F18APaletteRegisterData = -1;
		}
		return;
	}
	// RasmusM added end

	vdpaccess=0;		// reset byte flag (confirmed in hardware)

	RealVDP = GetRealVDP();
	UpdateHeatVDP(RealVDP);
	VDP[RealVDP]=data;
	VDPMemInited[RealVDP]=1;

	// before the breakpoint, check and emit debug if we messed up the disk buffers
	{
		int nTop = (staticCPU[0x8370]<<8) | staticCPU[0x8371];
		if (nTop <= 0x3be3) {
			// room for at least one disk buffer
			bool bFlag = false;

			if ((RealVDP == nTop+1) && (data != 0xaa)) {	bFlag = true;	}
			if ((RealVDP == nTop+2) && (data != 0x3f)) {	bFlag = true;	}
			if ((RealVDP == nTop+4) && (data != 0x11)) {	bFlag = true;	}
			if ((RealVDP == nTop+5) && (data > 9))		{	bFlag = true;	}

			if (bFlag) {
				debug_write("VDP disk buffer header corrupted at PC >%04X", pCurrentCPU->GetPC());
			}
		}
	}

	// check breakpoints against what was written to where - still assume internal address
	for (int idx=0; idx<nBreakPoints; idx++) {
		switch (BreakPoints[idx].Type) {
			case BREAK_EQUALS_VDP:
				if ((CheckRange(idx, VDPADD)) && ((data&BreakPoints[idx].Mask) == BreakPoints[idx].Data)) {
					TriggerBreakPoint();
				}
				break;

			case BREAK_WRITEVDP:
				if (CheckRange(idx, VDPADD)) {
					TriggerBreakPoint();
				}
				break;
		}
	}

	// verified on hardware
	vdpprefetch=data;
	vdpprefetchuninited = true;		// is it? you are reading back what you wrote. Probably not deliberate

	increment_vdpadd();
	redraw_needed=REDRAW_LINES;
}

void writeVdpRegister(Word addr, Byte data) {
	/* write address */
	// count down access cycles to help detect write address/read vdp overruns (there may be others but we don't think so!)
	// anyway, we need 8uS or we warn
	if (max_cpf > 0) {
		// TODO: this is still wrong. Since the issue is mid-instruction, we need to
		// count this down either at each phase, or calculate better what it needs
		// to be before the read. Where this is now, it will subtract the cycles from
		// this instruction, when it probably shouldn't. The write to the VDP will
		// happen just 4 (5?) cycles before the end of the instruction (multiplexer)
#if 0
		if (hzRate == HZ50) {
			vdpwroteaddress = (HZ50 * max_cpf) * 8 / 1000000;
		} else {
			vdpwroteaddress = (HZ60 * max_cpf) * 8 / 1000000;
		}
#endif
	}
	if (0 == vdpaccess) {
		// LSB (confirmed in hardware)
		VDPADD = (VDPADD & 0xff00) | data;
		vdpaccess = 1;
	} else {
		// MSB - flip-flop is reset and triggers action (confirmed in hardware)
		VDPADD = (VDPADD & 0x00FF) | (data<<8);
		vdpaccess = 0;

        // check if the user is probably trying to do DSR filename tracking
        // This is a TI disk controller side effect and shouldn't be relied
        // upon - particularly since it involved investigating freed buffers ;)
        if (((VDPADD == 0x3fe1)||(VDPADD == 0x3fe2)) && (GetSafeCpuWord(0x8356,0) == 0x3fe1)) {
            debug_write("Software may be trying to track filenames using deleted TI VDP buffers... (>8356)");
            if (BreakOnDiskCorrupt) TriggerBreakPoint();
        }

        // check what to do with the write
		if (VDPADD&0x8000) { 
			int nReg = (VDPADD&0x3f00)>>8;
			int nData = VDPADD&0xff;

			if (bF18Enabled) {
				if ((nReg == 57) && (nData == 0x1c)) {
					// F18A unlock sequence? Supposed to be twice but for now we'll just take equal
					// TODO: that's hacky and it's wrong. Fix it. 
					if (VDPREG[nReg] == nData) {	// but wait -- isn't this already verifying twice? TODO: Double-check procedure
						bF18AActive = true;
						debug_write("F18A Enhanced registers unlocked.");
					} else {
						VDPREG[nReg] = nData;
					}
					return;
				}
			} else {
				// this is hacky ;)
				bF18AActive = false;
			}
			if (bF18AActive) {
				// check extended registers. 
				// TODO: the 80 column stuff below should be included in the F18 specific stuff, but it's not right now

				// The F18 has a crapload of registers. But I'm only interested in a few right now, the rest can fall through

				// TODO: a lot of these side effects need to move to wVDPReg
				// Added by RasmusM
				if (nReg == 15) {
					// Status register select
					//debug_write("F18A status register 0x%02X selected", nData & 0x0f);
					F18AStatusRegisterNo = nData & 0x0f;
					return;
				}
				if (nReg == 47) {
					// Palette control
					bF18ADataPortMode = (nData & 0x80) != 0;
					bF18AAutoIncPaletteReg = (nData & 0x40) != 0;
					F18APaletteRegisterNo = nData & 0x3f;
					F18APaletteRegisterData = -1;
					if (bF18ADataPortMode) {
						debug_write("F18A Data port mode on.");
					}
					else {
						debug_write("F18A Data port mode off.");
					}
					return;
				}
				if (nReg == 49) {
					// Enhanced color mode
					F18AECModeSprite = nData & 0x03;
					F18ASpritePaletteSize = 1 << F18AECModeSprite;	
					debug_write("F18A Enhanced Color Mode 0x%02X selected for sprites", nData & 0x03);
					// TODO: read remaining bits: fixed tile enable, 30 rows, ECM tiles, real sprite y coord, sprite linking. 
					return;
				}
				// RasmusM added end

				if (nReg == 54) {
					// GPU PC MSB
					VDPREG[nReg] = nData;
					return;
				}
				if (nReg == 55) {
					// GPU PC LSB -- writes trigger the GPU
					VDPREG[nReg] = nData;
					pGPU->SetPC((VDPREG[54]<<8)|VDPREG[55]);
					debug_write("GPU PC LSB written, starting GPU at >%04X", pGPU->GetPC());
					pGPU->StopIdle();
					if (!bInterleaveGPU) {
						pCurrentCPU = pGPU;
					}
					return;
				}
				if (nReg == 56) {
					// GPU control register
					if (nData & 0x01) {
						if (pGPU->idling) {
							// going to run the code anyway to be sure, but only debug on transition
							debug_write("GPU GO bit written, starting GPU at >%04X", pGPU->GetPC());
						}
						pGPU->StopIdle();
						if (!bInterleaveGPU) {
							pCurrentCPU = pGPU;
						}
					} else {
						if (!pGPU->idling) {
							debug_write("GPU GO bit cleared, stopping GPU at >%04X", pGPU->GetPC());
						}
						pGPU->StartIdle();
						pCurrentCPU = pCPU;		// probably redundant
					}
					return;
				}
			}

			if (bEnable80Columns) {
				// active only when 80 column is enabled
				// special hack for RAM... good lord.
				if ((bEnable128k) && (nReg == 14)) {
					VDPREG[nReg] = nData&0x07;
					redraw_needed=REDRAW_LINES;
					return;
				}

			}

			if (bF18AActive) {
				wVDPreg((Byte)(nReg&0x3f),(Byte)(nData));
			} else {
				if (nReg&0xf8) {
					debug_write("Warning: writing >%02X to VDP register >%X ignored (PC=>%04X)", nData, nReg, pCPU->GetPC());
					return;
				}
				// verified correct against real hardware - register is masked to 3 bits
				wVDPreg((Byte)(nReg&0x07),(Byte)(nData));
			}
			redraw_needed=REDRAW_LINES;
		}

		// And the address remains set even when the target is a register
		if ((VDPADD&0xC000)==0) {	// prefetch inhibit? Verified on hardware - either bit inhibits.
			RealVDP = GetRealVDP();
			vdpprefetch=VDP[RealVDP];
			vdpprefetchuninited = (VDPMemInited[RealVDP] == 0);
			increment_vdpadd();
		} else {
			VDPADD&=0x3fff;			// writing or register, just mask the bits off
		}
	}
	// verified on hardware - write register does not update the prefetch buffer
}

Byte readVdpData(Word addr, bool rmw) {
	int RealVDP;

	if ((vdpwroteaddress > 0) && (pCurrentCPU == pCPU)) {
		// todo: need some defines - 0 is top border, not top blanking
		if ((vdpscanline >= 13) && (vdpscanline < 192+13) && (VDPREG[1]&0x40)) {
			debug_write("Warning - may be reading VDP too quickly after address write at >%04X!", pCurrentCPU->GetPC());
			vdpwroteaddress = 0;
		}
	}

	vdpaccess=0;		// reset byte flag (confirmed in hardware)
	RealVDP = GetRealVDP();
	UpdateHeatVDP(RealVDP);

	if (!rmw) {
		// Check for breakpoints
		for (int idx=0; idx<nBreakPoints; idx++) {
			switch (BreakPoints[idx].Type) {
				case BREAK_READVDP:
					if (CheckRange(idx, VDPADD-1)) {
						TriggerBreakPoint();
					}
					break;
			}
		}
	}

	// VDP Address is +1, so we need to check -1
	if ((g_bCheckUninit) && (vdpprefetchuninited)) {
		TriggerBreakPoint();
		// we have to remember if the prefetch was initted, since there are other things it could have
		char buf[128];
		sprintf(buf, "Breakpoint - reading uninitialized VDP memory at >%04X (or other prefetch)", (RealVDP-1)&0x3fff);
		MessageBox(myWnd, buf, "Classic99 Debugger", MB_OK);
	}

	z=vdpprefetch;
	vdpprefetch=VDP[RealVDP];
	vdpprefetchuninited = (VDPMemInited[RealVDP] == 0);
	increment_vdpadd();
	return ((Byte)z);
}

Byte readVdpStatus(Word addr, bool rmw) {
	unsigned short z;

    // This works around code that requires the VDP state to be able to change
	// DURING an instruction (the old code, the VDP state and the VDP interrupt
	// would both change and be recognized between instructions). With this
	// approach, we can update the VDP in the future, then run the instruction
	// against the updated VDP state. This allows Lee's fbForth random number
	// code to function, which worked by watching for the interrupt bit while
	// leaving interrupts enabled.
	updateVDP(-pCurrentCPU->GetCycleCount());

	// The F18A turns off DPM if any status register is read
	if ((bF18AActive)&&(bF18ADataPortMode)) {
		bF18ADataPortMode = 0;
		F18APaletteRegisterNo = 0;
		debug_write("F18A Data port mode off (status register read).");
	}

	// Added by RasmusM
	if ((bF18AActive) && (F18AStatusRegisterNo > 0)) {
		return getF18AStatus();
	}
	// RasmusM added end

	z=VDPS;				// does not affect prefetch or address (tested on hardware)
	VDPS&=0x1f;			// top flags are cleared on read (tested on hardware)
	vdpaccess=0;		// reset byte flag

	// TODO: hack to make Miner2049 work. If we are reading the status register mid-frame,
	// and neither 5S or F are set, return a random sprite index as if we were counting up.
	// Remove this when the proper scanline VDP is in. (Miner2049 cares about bit 0x02)
	if ((z&(VDPS_5SPR|VDPS_INT)) == 0) {
		// This search code borrowed from the sprite draw code
		int highest=31;
		int SAL=((VDPREG[5]&0x7f)<<7);

		// find the highest active sprite
		for (int i1=0; i1<32; i1++)			// 32 sprites 
		{
			if (VDP[SAL+(i1<<2)]==0xd0)
			{
				highest=i1-1;
				break;
			}
		}
		if (highest > 0) {
			z=(z&0xe0)|(rand()%highest);
		}
	}

	return((Byte)z);
}

// called to deconfigure/free RAM/etc
void unconfigureVDP9918(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // read space
    for (int idx=0x8800; idx<0x8bff; idx+=4) {
        pReads[idx] = readZero;
        pReads[idx+2] = writeZero;
    }
    // write space
    for (int idx=0x8C00; idx<0x8fff; idx+=4) {
        pWrites[idx] = readZero;
        pWrites[idx+2] = writeZero;
    }
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configureVDP9918(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    // takes over the address space for >8800-8fFF, even addresses only
    // read space
    for (int idx=0x8800; idx<0x8bff; idx+=4) {
        pReads[idx] = readVdpData;
        pReads[idx+2] = readVdpStatus;
    }
    // write space
    for (int idx=0x8C00; idx<0x8fff; idx+=4) {
        pWrites[idx] = writeVdpData;
        pWrites[idx+2] = writeVdpRegister;
    }

    return unconfigureVDP9918;
}
