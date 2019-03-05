// Handles the pCode interface - this is largely called from the hw_dsr code
// rather than direct from the memory interface

#include <malloc.h>
#include "hwTypes.h"

//////////////////////////////////////////////////////////////////
// Read a byte from P-Code GROM
//////////////////////////////////////////////////////////////////
Byte rpcodebyte(Word x)
{
	Byte z;

	if (x>=0x5ffc)
	{
		return(0);										// write address
	}

	// PCODE GROMs are distinct from the rest of the system

//	debug_write("Read PCODE GROM (>%04X), >%04x, >%02x", x, GROMBase[PCODEGROMBASE].GRMADD, GROMBase[PCODEGROMBASE].grmdata);

	if (x&0x0002)
	{
		// address
		GROMBase[PCODEGROMBASE].grmaccess=2;
		z=(GROMBase[PCODEGROMBASE].GRMADD&0xff00)>>8;
		// read is destructive
		GROMBase[PCODEGROMBASE].GRMADD=(((GROMBase[PCODEGROMBASE].GRMADD&0xff)<<8)|(GROMBase[PCODEGROMBASE].GRMADD&0xff));		
		// TODO: Is the address incremented anyway? ie: if you keep reading, what do you get?
		return(z);
	}
	else
	{
		// data
		UpdateHeatGROM(GROMBase[PCODEGROMBASE].GRMADD);	// todo: maybe a separate P-Code color?

		GROMBase[PCODEGROMBASE].grmaccess=2;
		z=GROMBase[PCODEGROMBASE].grmdata;

		// update just this prefetch
		GROMBase[PCODEGROMBASE].grmdata=GROMBase[PCODEGROMBASE].GROM[GROMBase[PCODEGROMBASE].GRMADD];
		GROMBase[PCODEGROMBASE].GRMADD++;
		return(z);
	}
}

//////////////////////////////////////////////////////////////////
// Write a byte to P-Code GROM
//////////////////////////////////////////////////////////////////
void wpcodebyte(Word x, Byte c)
{
	if (x<0x5ffc) 
	{
		return;											// read address
	}

	// PCODE GROMs are distinct from the rest of the system
//	debug_write("Write PCODE GROM (>%04X), >%04x, >%02x, %d", x, GROMBase[PCODEGROMBASE].GRMADD, c, GROMBase[PCODEGROMBASE].grmaccess);

	if (x&0x0002)
	{
		GROMBase[PCODEGROMBASE].GRMADD=(GROMBase[PCODEGROMBASE].GRMADD<<8)|(c);			// write GROM address
		GROMBase[PCODEGROMBASE].grmaccess--;
		if (GROMBase[PCODEGROMBASE].grmaccess==0)
		{ 
			GROMBase[PCODEGROMBASE].grmaccess=2;										// prefetch emulation
			
			// update just this prefetch
			GROMBase[PCODEGROMBASE].grmdata=GROMBase[PCODEGROMBASE].GROM[GROMBase[PCODEGROMBASE].GRMADD];
			GROMBase[PCODEGROMBASE].GRMADD++;
		}
		// GROM writes do not affect the prefetches, and have the same
		// side effects as reads (they increment the address and perform a
		// new prefetch)
	}
	else
	{
		UpdateHeatGROM(GROMBase[PCODEGROMBASE].GRMADD);		// todo: another color for pCode?

		GROMBase[PCODEGROMBASE].grmaccess=2;

//		debug_write("Writing to PCODE GROM!!");	// not supported!

		// update just this prefetch
		GROMBase[PCODEGROMBASE].grmdata=GROMBase[PCODEGROMBASE].GROM[GROMBase[PCODEGROMBASE].GRMADD];
		GROMBase[PCODEGROMBASE].GRMADD++;
	}
}

void writePCode(Word addr, Byte data) {
	// don't respond on odd addresses (confirmed)
	switch (addr) {
//		case 0x5bfc:		// read grom data
//		case 0x5bfe:		// read grom address
//			break;

		case 0x5ffc:		// write grom data
		case 0x5ffe:		// write grom address
  			wpcodebyte(x,c);
	}
}

Byte readPCode(Word addr, bool rmw) {
	// don't respond on odd addresses (confirmed)
	switch (addr) {
		case 0x5bfc:		// read grom data
		case 0x5bfe:		// read grom address
			return(rpcodebyte(addr));

		case 0x5ffc:		// write data
		case 0x5ffe:		// write address
			return 0;
	}

    // return from the paged DSR bank
    // TODO: needs to maintain its own bank, when we get CRU into here 
    // TODO: need to bring the pCode DSR code into this file
	if (nDSRBank[nCurrentDSR]) {
		return DSR[nCurrentDSR][addr-0x2000];	// page 1: -0x4000 for base, +0x2000 for second page
	} else {
		return DSR[nCurrentDSR][addr-0x4000];	// page 0: -0x4000 for base address
	}
}

// called to deconfigure/free RAM/etc
void unconfigurePCode(readFunc *pReads, writeFunc *pWrites, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
}

// called to configure the hardware
// return false to indicate an error occurred
// pReads points to 64k of read functions, pWrites of write functions
unconfigFunc configurePCode(readFunc * /*pReads*/, writeFunc * /*pWrites*/, readFunc * /*gromReads*/, writeFunc * /*gromWrites*/) {
    return unconfigurePCode;
}
