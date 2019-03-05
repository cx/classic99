//
// (C) 2007-2014 Mike Brent aka Tursi aka HarmlessLion.com
// This software is provided AS-IS. No warranty
// express or implied is provided.
//
// This notice defines the entire license for this code.
// All rights not explicity granted here are reserved by the
// author.
//
// You may redistribute this software provided the original
// archive is UNCHANGED and a link back to my web page,
// http://harmlesslion.com, is provided as the author's site.
// It is acceptable to link directly to a subpage at harmlesslion.com
// provided that page offers a URL for that purpose
//
// Source code, if available, is provided for educational purposes
// only. You are welcome to read it, learn from it, mock
// it, and hack it up - for your own use only.
//
// Please contact me before distributing derived works or
// ports so that we may work out terms. I don't mind people
// using my code but it's been outright stolen before. In all
// cases the code must maintain credit to the original author(s).
//
// -COMMERCIAL USE- Contact me first. I didn't make
// any money off it - why should you? ;) If you just learned
// something from this, then go ahead. If you just pinched
// a routine or two, let me know, I'll probably just ask
// for credit. If you want to derive a commercial tool
// or use large portions, we need to talk. ;)
//
// If this, itself, is a derived work from someone else's code,
// then their original copyrights and licenses are left intact
// and in full force.
//
// http://harmlesslion.com - visit the web page for contact info
//
//*****************************************************
//* Classic 99 - TI Emulator for Win32				  *
//* by M.Brent                                        *
//*                                                   *
//* Thanks to many people - see documentation         *
//*****************************************************

// Scratchpad RAM is now at 0x8300
// any patches that want to access it directly (not through ROMWORD or RCPUBYTE)
// must take note of this or they will fail

#pragma warning (disable: 4113 4761 4101)

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0500

////////////////////////////////////////////
// Includes
////////////////////////////////////////////
#include <WinSock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <mmsystem.h>
#include <process.h>
#include <malloc.h>
#include <dsound.h>
#include <time.h>
#include <math.h>
#include <atlstr.h>

#include "..\resource.h"
#include "tiemul.h"
#include "hwtypes.h"
#include "cpu9900.h"
#include "..\SpeechDll\5220intf.h"
#include "..\addons\rs232_pio.h"
#include "..\keyboard\kb.h"
#include "..\keyboard\ti.h"
#include "..\addons\ams.h"
#include "..\disk\diskclass.h"
#include "..\disk\fiaddisk.h"
#include "..\disk\imagedisk.h"
#include "..\disk\TICCDisk.h"
#include "..\disk\cf7Disk.h"
#include "sound.h"
#include "..\debugger\bug99.h"
#include "..\addons\mpd.h"
#include "..\addons\ubergrom.h"
#include "..\debugger\dbghook.h"

extern void rampVolume(LPDIRECTSOUNDBUFFER ds, long newVol);       // to reduce up/down clicks

////////////////////////////////////////////
// Globals
// These don't all NEED to be globals, but I'm only cleaning up the code, 
// not re-writing it all from scratch.
////////////////////////////////////////////

// TODO HACK
#define USE_BIG_ARRAY
#ifdef USE_BIG_ARRAY
unsigned char *BIGARRAY;
unsigned int BIGARRAYADD;
unsigned int BIGARRAYSIZE;
#endif

// Win32 Stuff
HINSTANCE hInstance;						// global program instance
HINSTANCE hPrevInstance;					// prev instance (always null so far)
bool bWindowInitComplete = false;           // just a little sync so we ignore size changes till we're done
extern BOOL CALLBACK DebugBoxProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
extern void DebugUpdateThread(void*);
extern void UpdateMakeMenu(HWND hwnd, int enable);

// User interface
CString csLastDiskImage[MAX_MRU];
CString csLastDiskPath[MAX_MRU];
CString csLastUserCart[MAX_MRU];

// audio
extern int AudioSampleRate;				// in hz
extern unsigned int CalculatedAudioBufferSize;		// round audiosample rate up to a multiple of frame rate
extern CRITICAL_SECTION csAudioBuf;

// speech 
#define SPEECHUPDATECOUNT (max_cpf/5)
INT16 SpeechTmp[SPEECHRATE*2];				// two seconds worth of buffer
int nSpeechTmpPos=0;
double nDACLevel=0.0;						// DAC level percentage (from cassette port) - added into the audio buffer on update
HANDLE hSpeechBufferClearEvent=INVALID_HANDLE_VALUE;		// notification of speech buffer looping

HMODULE hSpeechDll;											// Handle to speech DLL
void (*SpeechInit)(Byte *pROM, int nRomLen, int BufLen,int SampleRate);	// Pointer to SpeechInit function
void (*SpeechStop)(void);									// Pointer to SpeechStop function
Byte (*SpeechRead)(void);									// Pointer to SpeechRead function
bool (*SpeechWrite)(Byte b, bool f);						// Pointer to SpeechWrite function
void (*SpeechProcess)(Byte *pBuf, int nLen);				// Pointer to SpeechProcess function
HANDLE hWakeupEvent=NULL;									// used to sleep the CPU when not busy
volatile signed long cycles_left=0;							// runs the CPU throttle
volatile unsigned long total_cycles=0;						// used for interrupts
unsigned long speech_cycles=0;								// used to sync speech
bool total_cycles_looped=false;
bool bDebugAfterStep=false;									// force debug after step
bool bStepOver=false;										// whether step over is on
int nStepCount=0;											// how many instructions to step before breakpoints work again (usually 1)
bool bScrambleMemory = false;								// whether to set RAM to random values on reset
bool bWarmBoot = false;										// whether to leave memory alone on reset
int HeatMapFadeSpeed = 25;									// how many pixels per access to fade - bigger = more CPU but faster fade
int installedJoysticks = 3;									// bitmask - both joysticks are installed

// debug
struct _break BreakPoints[MAX_BREAKPOINTS];
int nBreakPoints=0;
bool BreakOnIllegal = false;
bool BreakOnDiskCorrupt = false;
bool gDisableDebugKeys = false;
CRITICAL_SECTION debugCS;
char g_cmdLine[512];

// disk
extern bool bCorruptDSKRAM;
int filesTopOfVram = 0x3fff;
CString csCf7Bios = "";                     // not sure if I can include the CF7 BIOS, so not a top level feature yet
CString csCf7Disk = ".\\cf7Disk.img";
int nCf7DiskSize = 128*1024*1024;

// Must remain compatible with LARGE_INTEGER - just here
// to make QuadPart unsigned ;)
typedef union {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
    unsigned __int64 QuadPart;
} MY_LARGE_INTEGER;

// Memory
Byte CPUMemInited[65536];					// not going to support AMS yet -- might switch to bits, but need to sort out AMS memory usage (16MB vs 1MB?)
Byte VDPMemInited[128*1024];				// track VDP mem
bool g_bCheckUninit = false;				// track reads from uninitialized RAM

extern Byte staticCPU[0x10000];				// main memory
Byte *CPU2=NULL;				            // Cartridge space bank-switched (ROM >6000 space, 8k blocks, XB, 379, SuperSpace and MBX ROM), sized by xbmask
Byte ROMMAP[65536];							// Write-protect map of CPU space
Byte DumpMap[65536];						// map for data to dump to files
FILE *DumpFile[10];							// byte dump file 0-9
Byte CRU[4096];								// CRU space (todo: could be bits)
Byte SPEECH[65536];							// Speech Synth ROM
Byte DSR[16][16384];						// 16 CRU bases, up to 16k each (ROM >4000 space)
int  nDSRBank[16];							// Is the DSR bank switched?
struct GROMType GROMBase[17];				// support 16 GROM bases (there is room for 256 of them!), plus 1 for PCODE
int  nSystem=1;								// Which system do we default to?
int  nCartGroup=0;							// Which cart group?
int	 nCart=-1;								// Which cart is loaded (-1=none)
struct DISKS *pMagicDisk=NULL;				// which magic disk is loaded?
bool fKeyEverPressed=false;					// used to suppress warning when changing cartridges
int  nLoadedUserCarts[100]= { 0 };			// for each group
int  nLoadedUserGroups=0;					// how many groups
char UserGroupNames[100][32];				// name of each group
int nTotalUserCarts=0;						// total user carts loaded
int CRUTimerTicks=0;						// used for 9901 timer

readFunc readFunctions[64*1024];            // read functions for every address
writeFunc writeFunctions[64*1024];          // write functions for every address

unsigned char DummyROM[6]={
	0x83, 0x00,								// >0000	reset vector: workspace
	0x00, 0x04,								// >0002	reset vector: address
	0x10, 0xff								// >0004	JMP @>0004
};

int KEYS[2][8][8]= {  
{
// Keyboard - 99/4 - no PS/2 emulation :)
/* unused */	VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE,

/* 1 */			'N', 'H', 'U', '7', 'C', 'D', 'R', '4',
/* Joy 1 */		VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE,
/* 3 */			VK_OEM_PERIOD, 'K', 'O', '9', 'Z', 'A', 'W', '2',

/* Joy 2 */		VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE,
	
/* 5 */			'M', 'J', 'I', '8', 'X', 'S', 'E', '3',
/* 6 */			'B', 'G', 'Y', '6', 'V', 'F', 'T', '5',
/* 7 */			VK_RETURN, 'L', 'P', '0', VK_SHIFT, VK_SPACE, 'Q', '1'
},
{
// Keyboard - 99/4A
/* Joy 2 */		VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE,

/* 1 */			'M', 'J', 'U', '7', '4', 'F', 'R', 'V',					// MJU7 4FRV
/* 2 */			VK_OEM_2, VK_OEM_1, 'P', '0', '1', 'A', 'Q', 'Z',		// /;P0 1AQZ
/* 3 */			VK_OEM_PERIOD, 'L', 'O', '9', '2', 'S', 'W', 'X',		// .LO9 2SWX

/* Joy 1 */		VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE, VK_ESCAPE,
	
/* 5 */			VK_OEM_COMMA, 'K', 'I', '8', '3', 'D', 'E', 'C',		// ,KI8 3DEC
/* 6 */			'N', 'H', 'Y', '6', '5', 'G', 'T', 'B',					// NHY6 5GTB
/* 7 */			VK_OEM_PLUS, VK_SPACE, VK_RETURN, VK_ESCAPE, VK_MENU, VK_SHIFT, VK_CONTROL, VK_ESCAPE 
}																		// = rx fscx
};

char key[256];										// keyboard state buffer

// Win32 Joysticks
JOYINFOEX myJoy;
int fJoy;
int joy1mode, joy2mode;
int fJoystickActiveOnKeys;

// Audio
int latch_byte;										// latched byte
extern int max_volume;								// maximum volume as a percentage
LPDIRECTSOUND lpds;									// DirectSound handle
LPDIRECTSOUNDBUFFER soundbuf;						// sound chip audio buffer
LPDIRECTSOUNDBUFFER sidbuf;							// sid blaster audio buffer
LPDIRECTSOUNDBUFFER speechbuf;						// speech audio buffer
// Used to halt the CPU when writing to the synth too quicky
// This probably belongs in the speech emulation
bool CPUSpeechHalt=false;
Byte CPUSpeechHaltByte=0;

// disassembly view
struct history Disasm[20];							// history object

// video
extern int bEnable80Columns;						// 80 column hack
extern int bEnable128k;								// 128k hack
extern int bF18Enabled;								// F18A support
extern int bInterleaveGPU;							// simultaneous GPU (not really)

// Assorted
char qw[80];										// temp string
volatile int quitflag;								// quit flag
char lines[34][DEBUGLEN];							// debug lines
bool bDebugDirty;									// whether debug has changed
volatile int xbBank=0;								// Cartridge bank switch
volatile int bInvertedBanks=false;					// whether switching uses Jon's inverted 379
volatile int bUsesMBX=false;						// whether switching uses MBX style ROM
int xb = 0;											// Is bank-switched cartridge ROM loaded?
int grombanking = 0;								// Did we load multiple GROM bases?
int nCurrentDSR=-1;									// Which DSR Bank are we on?
unsigned int index1;								// General counter variable
int drawspeed=0;									// flag used in display updating
int max_cpf=DEFAULT_60HZ_CPF;						// Maximum cycles per frame (default)
int cfg_cpf=max_cpf;								// copy of same
int slowdown_keyboard = 1;							// slowdown keyboard autorepeat in the GROM code
int cpucount, cpuframes;							// CPU counters for timing
int timercount;										// Used to estimate runtime
int CtrlAltReset = 0;								// if true, require control+alt+equals
int gDontInvertCapsLock = 0;						// if true, caps lock is not inverted
const char *szDefaultWindowText="Classic99";		// used to set Window back to normal after a change

int timer9901;										// 9901 interrupt timer
int timer9901Read;                                  // the read-back register
int starttimer9901;									// and it's set time
int timer9901IntReq;								// And whether it is requesting an interrupt
int keyboard=KEY_994A_PS2;							// keyboard map (0=99/4, 1=99/4A, 2=99/4A PS/2 (see enum in .h))
int ps2keyboardok=1;								// whether to allow PS2 keyboards

int sams_enabled = 1;								// memory type (0 = disabled, 1 SAMS enabled)
int sams_size = 3;									// SAMS emulation memory size (0 = 128k, 1 = 256k, 2 = 512k, 3 = 1024k)

int retrace_count=0;								// count on the 60hz timer

int PauseInactive;									// what to do when the window is inactive
int SpeechEnabled;									// whether speech is enabled
volatile int CPUThrottle;							// Whether or not the CPU is throttled
volatile int SystemThrottle;						// Whether or not the VDP is throttled

time_t STARTTIME, ENDTIME;
volatile long ticks;

CPU9900 * volatile pCurrentCPU;	// todo: I'm not sure. the contents are volatile too...
CPU9900 *pCPU, *pGPU;

ATOM myClass;										// Window Class
HWND myWnd;											// Handle to windows
volatile HWND dbgWnd;								// Handle to windows
HDC myDC;											// Handle to Device Context
int fontX, fontY;									// Non-proportional font x and y size
DWORD g_dwMyStyle = WS_OVERLAPPEDWINDOW | WS_SIZEBOX | WS_VISIBLE;
int nVideoLeft = -1, nVideoTop = -1;
RECT gWindowRect;

char AVIFileName[256]="C:\\TI99AVI.AVI";			// AVI Filename

char *PasteString;									// Used for Edit->Paste
char *PasteIndex;
bool PasteStringHackBuffer=false;					// forces long inputs under BASIC/XB (may cause crashes)
int PasteCount;

unsigned long myThread;								// timer thread
CRITICAL_SECTION VideoCS;							// Video CS
CRITICAL_SECTION DebugCS;							// Debug CS
CRITICAL_SECTION TapeCS;							// Tape CS

extern const char *pCurrentHelpMsg;
extern int VDPDebug;
extern int TVScanLines;

#define INIFILE ".\\classic99.ini"

///////////////////////////////////
// Built-in Cart library
///////////////////////////////////

// ROMs to always load
struct IMG AlwaysLoad[] = {
    {	IDR_AMI99DSK,	0x1100, 0x01c0,	TYPE_DSR	, 0},
	{	IDR_TIDISK,		0x1100, 0x2000,	TYPE_DSR2	, 0},	// not paged on the real hardware, but this is how we fake it with all our features :)
//	{	IDR_RS232,		0x1300, 0x0900, TYPE_DSR	, 0},
	{	IDR_SPCHROM,	0x0000,	0x8000,	TYPE_SPEECH	, 0},
	{	IDR_PGROM,		0x0000, 0xF800, TYPE_PCODEG , 0},
};

// Extra files to support certain cartridges
// These files, when the list is loaded, can be loaded as if they were
// on the disk without the disk actually needing it
// Currently these can only be loaded program image files!
// They completely ignore the disk now so can override any disk (may be good or bad?)
struct DISKS Disk_EA[] = {
	{	"ASSM1",	IDR_ASSM1	},
	{	"ASSM2",	IDR_ASSM2	},
	{	"EDIT1",	IDR_EDIT1	},
	{	"",				0			},
};

struct DISKS Disk_SSA[] = {
	{	"ACER_C",	IDR_ACERC	},
	{	"ACER_P",	IDR_ACERP	},
	{	"SSD",		IDR_SSD		},
	{	"SSE",		IDR_SSE		},
	{	"",				0			},
};

struct DISKS Disk_Tunnels[] = {
	{	"PENNIES",	IDR_PENNIES	},
	{	"QUEST",	IDR_QUEST	},

	{	"",				0			},
};

// Actual cartridge definitions (broken into categories)
struct CARTS *Users=NULL;		// these are loaded dynamically

struct CARTS Systems[] = {
	{	
		"TI-99/4",	
		{	
			{	IDR_CON4R0,		0x0000, 0x2000,	TYPE_ROM	, -1},
			{	IDR_CON4G0,		0x0000, 0x2000,	TYPE_GROM	, -1},
			{	IDR_CON4G1,		0x2000,	0x2000,	TYPE_GROM	, -1},
			{	IDR_CON4G2,		0x4000,	0x2000,	TYPE_GROM	, -1},
		},
		NULL,
		NULL,
		0
	},

	{	
		"TI-99/4A",	
		{	
			{	IDR_994AGROM,	0x0000, 0x6000,	TYPE_GROM	, -1},
			{	IDR_994AROM,	0x0000,	0x2000,	TYPE_ROM	, -1},
		},
		NULL,
		NULL,
		0
	},

	{	
		"TI-99/4A V2.2",
		{
			{	IDR_CON22R0,	0x0000, 0x2000,	TYPE_ROM	, -1},
			{	IDR_CON22G0,	0x0000,	0x2000,	TYPE_GROM	, -1},
			{	IDR_CON22G1,	0x2000,	0x2000,	TYPE_GROM	, -1},
			{	IDR_CON22G2,	0x4000,	0x2000,	TYPE_GROM	, -1},
		},
		NULL,
		NULL,
		0
	},
};

struct CARTS Apps[] = {
#if 0
	// not working yet
	{	
		"AMS Test 2.0",
		{
			{	IDR_ROMS5,		0x6000,	0x0030,	TYPE_ROM	, 0},
			{	0,				0x0000,	0x0000,	TYPE_AMS	, 0},	// address ignored on AMS cards, 0xffff flags no load, activate card
		},
		NULL,
		"AMS Card will automatically be enabled.",
		0
	},
#endif

	{	
		"Demonstration",
		{
			{	IDR_DEMOG,		0x6000, 0x8000,	TYPE_GROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"Diagnostics",
		{	
			{	IDR_DIAGNOSG,	0x6000, 0x2000,	TYPE_GROM	, 0},
		},
		NULL,
		"The maintenance tests are intended for use with external hardware, and are not supported. They will hang the emulator. Use File->Reset to bring it back. The checkerboard test will fail so long as the disk system is attached.",
		0
	},
						
	{	
		"Editor/Assembler",
		{
			{IDR_TIEAG,		0x6000,	0x2000,	TYPE_GROM	, 0},
		},
		Disk_EA,
		"The Editor and Assembler files are built-in.",
		0
	},

	{	
		"EPSGMOD Example",
		{
			{IDR_EPSGMODG,	0x6000,	0x60C8,	TYPE_GROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"Extended BASIC",
		{
			{	IDR_TIEXTG,		0x6000,	0x8000,	TYPE_GROM	, 0},
			{	IDR_TIEXTC,		0x6000,	0x2000,	TYPE_ROM	, 0},
			{	IDR_TIEXTD,		0x6000,	0x2000,	TYPE_XB		, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"fbForth 2.0:11 by Lee Stewart",
		{	
			{	IDR_FBFORTH,	0x0000, 0x8000,	TYPE_379	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"Home Finance",
		{
			{	IDR_HOMEG,		0x6000, 0x4000,	TYPE_GROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"MegaMan2 Music",
		{
			{	IDR_TIPLAYERC,	0x6000, 0x2000,	TYPE_ROM	, 0},
			{	IDR_TIMUSIC,	0x6000,	0xA000,	TYPE_GROM	, 0},
			{	IDR_TIMUSID,	0x6000,	0xA000,	TYPE_GROM	, 1},
			{	IDR_TIMUSIE,	0x6000,	0xA000,	TYPE_GROM	, 2},
			{	IDR_TIMUSIF,	0x6000,	0x5A90,	TYPE_GROM	, 3},
			{	IDR_TIMM2PICP,	0x6000,	0x1800,	TYPE_GROM	, 15},
			{	IDR_TIMM2PICC,	0x8000,	0x1800,	TYPE_GROM	, 15},
			{	IDR_DUMMYG,		0x6000,	0x0040,	TYPE_GROM	, 9},
		},
		NULL,
		NULL,
		0
	},

	{	
		"Mini Memory",
		{	
			{	IDR_MINIMEMG,	0x6000, 0x2000,	TYPE_GROM	, 0},
			{	IDR_MINIMEMC,	0x6000,	0x1000,	TYPE_ROM	, 0},
			{	NULL,			0x7000,	0x1000,	TYPE_NVRAM	, 0, "minimemNV.bin"},
		},
		NULL,
		NULL,
		0
	},

	{	
		"P-Code Card",
		{	
			{	IDR_PCODEC,		0x1F00,	0x2000,	TYPE_DSR	, 0},
			{	IDR_PCODED,		0x1F00, 0x2000,	TYPE_DSR2	, 0},
		},
		NULL,		// TODO: include the P-Code diskettes in the archive - convert from PC99 to standard V9T9 and check sectors
		NULL,
		0
	},

	{	
		"RXB 2015E by Rich Gilbertson",
		{
			{	IDR_RXBG,		0x6000,	0xA000,	TYPE_GROM	, 0},
			{	IDR_RXBC,		0x6000,	0x2000,	TYPE_ROM	, 0},
			{	IDR_RXBD,		0x6000,	0x2000,	TYPE_XB		, 0},
		},
		Disk_EA,
		"Editor and Assembler files are built in. Not loadable by REA2012 due to GROM base. RXB may rarely crash after loading the editor or assembler (just reset)!",
		0
	},

#if 0
	// don't have permission for some of the pics, will add it when I get time to rebuild the file
	{	
		"Slideshow",	
		{	
			{	IDR_TISLIDE128C,0x0000, 0x20000,TYPE_379	, 0},
		},
		NULL,
		NULL,
		0
	},
#endif

	{	
		"Terminal Emulator 2",
		{
			{	IDR_TE2G,	0x6000, 0xA000,	TYPE_GROM	, 0},
			{	IDR_TE2C,	0x6000,	0x2000,	TYPE_ROM	, 0},
		},
		NULL,
		"Serial is not supported yet, speech may sound poor.",
		0
	},

	{	
		"TI Logo ][",
		{	
			{	IDR_LOGOG,		0x6000, 0x6000,	TYPE_GROM	, 0},
			{	IDR_LOGOC,		0x6000,	0x2000,	TYPE_ROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"TI Workshop (379)",
		{	
			{	IDR_TIWORKSHOP,	0x0000, 0x10000,TYPE_379	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"TurboForth 1.2.1 by Mark Wills",
		{	
			{	IDR_TURBOFORTHC,	0x6000, 0x2000,TYPE_ROM	, 0},
			{	IDR_TURBOFORTHD,	0x6000, 0x2000,TYPE_XB	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"XB2.7 Suite",
		{	
			{	IDR_XB27GROM,		0x0000, 0x1E000,	TYPE_UBER_GROM		, 0},
			{	IDR_XB27ROM,		0x0000, 0x80000,	TYPE_378			, 0},
			{	IDR_XB27EEPROM,		0x0000, 0x1000,		TYPE_UBER_EEPROM	, 0},
		},
		NULL,
		NULL,
		0
	},

};

struct CARTS Games[] = {
	{	
		"Alpiner",	
		{	
			{	IDR_ALPINERG,	0x6000, 0x8000,	TYPE_GROM	, 0},
			{	IDR_ALPINERC,	0x6000,	0x2000,	TYPE_ROM	, 0},
		},
		NULL,
		NULL,
		0
	},
	
	{	
		"A-Maze-Ing",
		{	
			{	IDR_AMAZEG,		0x6000, 0x2000,	TYPE_GROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"BlackJack&&Poker",
		{
			{IDR_BLACKJACK,	0x6000, 0x2000,	TYPE_GROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"Car Wars",	
		{	
			{	IDR_CARWARS,	0x6000, 0x2000,	TYPE_GROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"Chisholm Trail",
		{
			{	IDR_CHISHOLMG,	0x6000, 0x2000,	TYPE_GROM	, 0},
			{	IDR_CHISHOLMC,	0x6000,	0x2000,	TYPE_ROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"Football",
		{	
			{	IDR_FOOTBALLG,	0x6000, 0x4000,	TYPE_GROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"Hustle",	
		{	
			{	IDR_HUSTLEG,	0x6000, 0x2000,	TYPE_GROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"Hunt the Wumpus",
		{
			{	IDR_WUMPUSG,	0x6000, 0x2000,	TYPE_GROM	, 0},
		},
		NULL,
		NULL,
		0
	},
 
	{	
		"Mind Challengers",
		{
			{	IDR_MINDG,		0x6000, 0x2000,	TYPE_GROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"Munch Man",
		{	
			{	IDR_MUNCHMNG,	0x6000, 0x2000,	TYPE_GROM	, 0},
			{	IDR_MUNCHMNC,	0x6000,	0x2000,	TYPE_ROM	, 0},
		},
		NULL,
		NULL,
		0
	},
	
	{	
		"Parsec",	
		{	
			{	IDR_PARSECG,	0x6000, 0x6000,	TYPE_GROM	, 0},
			{	IDR_PARSECC,	0x6000,	0x2000,	TYPE_ROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"Super Space Acer",
		{
			{	IDR_SSALOAD,	0x6000, 0x0030,	TYPE_ROM	, 0},
			{	IDR_DEMQ,		0x2000,	0x0706,	TYPE_RAM	, 0},
			{	IDR_SSARAM,		0xA000,	0x5A28,	TYPE_RAM	, 0},
		},
		Disk_SSA,
		NULL,
		0
	},

	{	
		"TI Invaders",
		{	
			{	IDR_TIINVADG,	0x6000, 0x8000,	TYPE_GROM	, 0},
			{	IDR_TIINVADC,	0x6000,	0x2000,	TYPE_ROM	, 0},
		},
		NULL,
		NULL,
		0
	},
	
#if 0
	// not working yet
	{	
		"TI-Nopoly",
		{	
			{	IDR_ROMS4,		0x6000,	0x0030,	TYPE_ROM	, 0},
			{	IDR_ROMS1,		0x0003, 0x0df2,	TYPE_AMS	, 0},	// both words used for size on AMS cards
			{	IDR_ROMS2,		0x2000,	0x00BA,	TYPE_RAM	, 0},
			{	IDR_ROMS6,		0x0000,	0x0005,	TYPE_KEYS	, 0},
		},
		NULL,
		"AMS Card will automatically be enabled.",
		0
	},
#endif

	{	
		"Tombstone City",
		{
			{	IDR_TOMBCITG,	0x6000, 0x2000,	TYPE_GROM	, 0},
			{	IDR_TOMBCITC,	0x6000,	0x2000,	TYPE_ROM	, 0},
		},
		NULL,
		NULL,
		0
	},

	{	
		"Tunnels of Doom",
		{
			{	IDR_TUNDOOMG,	0x6000,	0xA000,	TYPE_GROM	, 0},
		},
		Disk_Tunnels,
		"Select DSK1, and PENNIES for introductory quest, or QUEST for a full quest.",
		0
	},

	{	
		"Video Chess",
		{	
			{	IDR_CHESSG,		0x6000, 0x8000,	TYPE_GROM	, 0},
			{	IDR_CHESSC,		0x6000,	0x2000,	TYPE_ROM	, 0},
		},
		NULL,
		NULL,
		0
	},
};

// breakpoint helper 
bool CheckRange(int nBreak, int x) {
	// check bank first (assumes ranges are only for addresses, not data)
	if (BreakPoints[nBreak].Bank != -1) {
		if ((x>=0x6000) && (x<=0x7FFF)) {
			if (xbBank != BreakPoints[nBreak].Bank) {
				// bank required and not the right bank
				return false;
			}
		}
	}

	if (BreakPoints[nBreak].B) {
		// this is a range
		if ((x >= BreakPoints[nBreak].A) && (x <= BreakPoints[nBreak].B)) {
			return true;
		}
	} else {
		// not a range
		if (x == BreakPoints[nBreak].A) {
			return true;
		}
	}
	return false;
}

// Configuration access
void ReadConfig() {
	int idx,idx2,idx3;
	bool bFilePresent=true;

	// Check if the file is even present - if it's not we need to fake the disk config
	FILE *fp=fopen(INIFILE, "r");
	if (NULL == fp) {
		// no such file
		debug_write("No configuration file - setting defaults");
		bFilePresent=false;
	} else {
		fclose(fp);
	}

	// Volume percentage
	max_volume =			GetPrivateProfileInt("audio",	"max_volume",	max_volume,					INIFILE);

	// SID blaster
	if (NULL != SetSidEnable) {
		SetSidEnable(		GetPrivateProfileInt("audio",	"sid_blaster",	0,							INIFILE) != 0	);
	}

	// audio rate
	AudioSampleRate =		GetPrivateProfileInt("audio",	"samplerate",	AudioSampleRate,			INIFILE);

	// load the new style config
	EnterCriticalSection(&csDriveType);

	if (bFilePresent) {
		HMENU hMenu = GetMenu(myWnd);
		if (NULL != hMenu) {
			hMenu = GetSubMenu(hMenu, 4);	// disk menu
		}

		for (int idx=0; idx < MAX_DRIVES-RESERVED_DRIVES; idx++) {
			CString cs;
			
			if (NULL != pDriveType[idx]) {
				delete pDriveType[idx];
				pDriveType[idx]=NULL;
			}

			cs.Format("Disk%d", idx);

			int nType = GetPrivateProfileInt(cs, "Type", DISK_NONE, INIFILE);
			if (nType != DISK_NONE) {
				switch (nType) {
					case DISK_FIAD:
						pDriveType[idx] = new FiadDisk;
						break;

					case DISK_SECTOR:
						pDriveType[idx] = new ImageDisk;
						break;

					case DISK_TICC:
						pDriveType[idx] = new TICCDisk;
						break;
				}

				if (NULL != pDriveType[idx]) {
					// get the path
					char buf[MAX_PATH];
					GetPrivateProfileString(cs, "Path", ".", buf, MAX_PATH, INIFILE);
					pDriveType[idx]->SetPath(buf);

					if (hMenu != NULL) {
						HMENU hSub = GetSubMenu(hMenu, idx);
						if (hSub != NULL) {
							ModifyMenu(hSub, 0, MF_BYPOSITION | MF_STRING, ID_DISK_DSK0_SETDSK0+idx, buf);
						}
					}
					
					// note that all values should use 0 for default, since a 
					// 0 from the config will not be relayed to the class
					for (idx2=0; idx2 < DISK_OPT_MAXIMUM; idx2++) {
						int nTmp = GetPrivateProfileInt(cs, pszOptionNames[idx2], -1, INIFILE);
						if (nTmp != -1) {
							pDriveType[idx]->SetOption(idx2, nTmp);
						}
					}
				}
			}
		}
	} else {
		// There's no configuration file - this sets the default drive layout
		for (int idx=0; idx < MAX_DRIVES-RESERVED_DRIVES; idx++) {
			CString cs;
			HMENU hMenu = GetMenu(myWnd);
			if (NULL != hMenu) {
				hMenu = GetSubMenu(hMenu, 4);	// disk menu
			}
			
			if (NULL != pDriveType[idx]) {
				delete pDriveType[idx];
				pDriveType[idx]=NULL;
			}

			if ((idx>0)&&(idx<4)) {		// 1-3
				CString csTmp;
				// the main defaults for FIADDisk are fine, just set type and path
				pDriveType[idx] = new FiadDisk;
				csTmp.Format(".\\DSK%d\\", idx);
				pDriveType[idx]->SetPath(csTmp);
				if (hMenu != NULL) {
					HMENU hSub = GetSubMenu(hMenu, idx);
					if (hSub != NULL) {
						ModifyMenu(hSub, 0, MF_BYPOSITION | MF_STRING, ID_DISK_DSK0_SETDSK0+idx, csTmp.GetBuffer());
					}
				}
			}
		}
	}
	LeaveCriticalSection(&csDriveType);
	// the menu may have changed!
	DrawMenuBar(myWnd);

    // see if we're going to do anything with the CF7 - if it's set, we'll not use the disk DSR
    {
        char buf[1024];
    	GetPrivateProfileString("CF7", "BIOS", "", buf, sizeof(buf), INIFILE);
        if (buf[0] != '\0') {
            csCf7Bios = buf;
        }
    	GetPrivateProfileString("CF7", "Disk", "", buf, sizeof(buf), INIFILE);
        if (buf[0] != '\0') {
            csCf7Disk = buf;
        }
        nCf7DiskSize = GetPrivateProfileInt("CF7", "Size", nCf7DiskSize, INIFILE);
    }

	// Filename used to write recorded video
	GetPrivateProfileString("emulation", "AVIFilename", AVIFileName, AVIFileName, 256, INIFILE);
	// CPU Throttling? CPU_OVERDRIVE, CPU_NORMAL, CPU_MAXIMUM
	CPUThrottle=	GetPrivateProfileInt("emulation",	"cputhrottle",			CPUThrottle,	INIFILE);
	// VDP Throttling? VDP_CPUSYNC, VDP_REALTIME
	SystemThrottle=	GetPrivateProfileInt("emulation",	"systemthrottle",		SystemThrottle,	INIFILE);
	// Proper CPU throttle (cycles per frame) - ipf is deprecated
	max_cpf=		GetPrivateProfileInt("emulation",	"maxcpf",				max_cpf,		INIFILE);
	cfg_cpf = max_cpf;
	// Pause emulator when window inactive: 0-no, 1-yes
	PauseInactive=	GetPrivateProfileInt("emulation",	"pauseinactive",		PauseInactive,	INIFILE);
	// Disable speech if desired
	SpeechEnabled=  GetPrivateProfileInt("emulation",   "speechenabled",         SpeechEnabled,  INIFILE);
	// require additional control key to reset (QUIT)
	CtrlAltReset=	GetPrivateProfileInt("emulation",	"ctrlaltreset",			CtrlAltReset,	INIFILE);
	// override the inverted caps lock
	gDontInvertCapsLock = !GetPrivateProfileInt("emulation","invertcaps",	!gDontInvertCapsLock, INIFILE);
	// Get system type: 0-99/4, 1-99/4A, 2-99/4Av2.2
	nSystem=		GetPrivateProfileInt("emulation",	"system",				nSystem,		INIFILE);
	// Read flag for slowing keyboard repeat: 0-no, 1-yes
	slowdown_keyboard=GetPrivateProfileInt("emulation",	"slowdown_keyboard",	slowdown_keyboard, INIFILE);
	// Check whether to use the ps/2 keyboard (normally yes for 99/4A)
	if (nSystem == 0) {
		keyboard=KEY_994;		// 99/4
	} else {
		ps2keyboardok=GetPrivateProfileInt("emulation", "ps2keyboard", 1, INIFILE);
		if (ps2keyboardok) {
			keyboard=KEY_994A_PS2;	// 99/4A with ps/2
		} else {
			keyboard=KEY_994A;		// 99/4A without ps/2
		}
	}
	// SAMS emulation
	sams_enabled = GetPrivateProfileInt("emulation", "sams_enabled", sams_enabled, INIFILE);
	// Read flag for SAMS memory size if selected
	sams_size = GetPrivateProfileInt("emulation", "sams_size", sams_size, INIFILE);

	// Joystick active: 0 - off, 1 on
	fJoy=		GetPrivateProfileInt("joysticks", "active",		fJoy,		INIFILE);
	// 0-keyboard, 1-PC joystick 1, 2-PC joystick 2
	joy1mode=	GetPrivateProfileInt("joysticks", "joy1mode",	joy1mode,	INIFILE);
	joy2mode=	GetPrivateProfileInt("joysticks", "joy2mode",	joy2mode,	INIFILE);
	fJoystickActiveOnKeys = 0;		// just reset this

	// Cartridge group loaded (0-apps, 1-games, 2-user)
	nCartGroup=	GetPrivateProfileInt("roms",	"cartgroup",	nCartGroup,	INIFILE);
	// Cartridge index (depends on group)
	nCart=		GetPrivateProfileInt("roms",	"cartidx",		nCart,		INIFILE);
	// User cartridges
	memset(nLoadedUserCarts, 0, sizeof(nLoadedUserCarts));
	nLoadedUserGroups=0;
	// up to 100 groups, each with up to 100 carts. The last group is always "usercart%d" (and we remove it if it's empty)
	idx2=0;
	for (idx=0; idx<100; idx++) {
		char buf[256], buf2[256];
		sprintf(buf, "Group%d", idx);
		GetPrivateProfileString("CartGroups", buf, "", UserGroupNames[idx2], sizeof(UserGroupNames[idx2]), INIFILE);
		if (strlen(UserGroupNames[idx2]) > 0) idx2++;
	}
	// now sneak in usercart, in case the user had it configured... but only if there is room!
	if (idx2 < 100) {
		strcpy(UserGroupNames[idx2], "UserCart");
		idx2++;
	}
	// save the count
	nLoadedUserGroups=idx2;
	nTotalUserCarts = 1;	// there's always one to start with, and we leave it blank
	// now run through all the groups and scan for carts to load to the menu
	for (int cart=0; cart<nLoadedUserGroups; cart++) {
		idx2=0;

		for (idx=0; idx<100; idx++) {
			char buf[256], buf2[256];

            // TODO: check for memory leaks on failed realloc
			// it's not the most efficient to keep reallocing, but it will be fine in this limited use case
			Users=(CARTS*)realloc(Users, (nTotalUserCarts+1) * sizeof(Users[0]));
            if (NULL == Users) {
                fail("Unable to allocate user cart memory!");
            }
			memset(&Users[nTotalUserCarts], 0, sizeof(CARTS));

			sprintf(buf, "%s%d", UserGroupNames[cart], idx);
			GetPrivateProfileString(buf, "name", "", Users[nTotalUserCarts].szName, sizeof(Users[nTotalUserCarts].szName), INIFILE);
			if (strlen(Users[nTotalUserCarts].szName) > 0) {
				Users[nTotalUserCarts].pDisk=NULL;
				GetPrivateProfileString(buf, "message", "", buf2, 256, INIFILE);
				if (strlen(buf2) > 0) {
					Users[nTotalUserCarts].szMessage=_strdup(buf2);		// this memory will leak!
				} else {
					Users[nTotalUserCarts].szMessage=NULL;
				}
				for (idx3=0; idx3<MAXROMSPERCART; idx3++) {
					char buf3[1024];

					sprintf(buf2, "ROM%d", idx3);
					// line is formatted, except filename which finishes the line
					// T[x]|AAAA|LLLL|filename
					// [x] is the optional bank number from 0-F
					Users[nTotalUserCarts].Img[idx3].dwImg=NULL;
					Users[nTotalUserCarts].Img[idx3].nBank=0;
					GetPrivateProfileString(buf, buf2, "", buf3, 1024, INIFILE);
					if (strlen(buf3) > 0) {
						int strpos=0;
						if (3 != sscanf(buf3, "%c|%x|%x|%n", 
							&Users[nTotalUserCarts].Img[idx3].nType,
							&Users[nTotalUserCarts].Img[idx3].nLoadAddr,
							&Users[nTotalUserCarts].Img[idx3].nLength,
							&strpos)) {
								if (4 != sscanf(buf3, "%c%x|%x|%x|%n", 
									&Users[nTotalUserCarts].Img[idx3].nType,
									&Users[nTotalUserCarts].Img[idx3].nBank,
									&Users[nTotalUserCarts].Img[idx3].nLoadAddr,
									&Users[nTotalUserCarts].Img[idx3].nLength,
									&strpos)) {
										sprintf(buf3, "INI File error reading %s in %s", buf2, buf);
										MessageBox(myWnd, buf3, "Classic99 Error", MB_OK);
										goto skiprestofuser;
								}
						}
						// copy the full string (have to do it this way to include spaces)
						strcpy(Users[nTotalUserCarts].Img[idx3].szFileName, &buf3[strpos]);
						// this doesn't read correctly? Sometimes it does??
						Users[nTotalUserCarts].Img[idx3].nType=buf3[0];
					}
				}
				Users[nTotalUserCarts].nUserMenu = nTotalUserCarts+ID_USER_0;
				++idx2;
				++nTotalUserCarts;
				if (nTotalUserCarts+ID_USER_0 >= ID_SYSTEM_0) break;	// inner loop break
                if (nTotalUserCarts >= MAXUSERCARTS) break;                 // system max
			}
			if (nTotalUserCarts+ID_USER_0 >= ID_SYSTEM_0) break;	// mid loop break
            if (nTotalUserCarts >= MAXUSERCARTS) break;                 // system max
		}
        debug_write("Loaded %d user carts total.", nTotalUserCarts);
		nLoadedUserCarts[cart]=idx2;
		if (idx2 == 0) {
			// there were no carts in this one, so just remove it from the list
			debug_write("Cartridge Group '%s' empty, dropping from list.", UserGroupNames[cart]);
			if (cart < 99) {
				memcpy(UserGroupNames[cart], UserGroupNames[cart+1], sizeof(UserGroupNames[cart]));
			} 
			cart--;		// it will be incremented and we'll be right back where we are
			nLoadedUserGroups--;
		}
		if (nTotalUserCarts+ID_USER_0 >= ID_SYSTEM_0) break;	// outer loop break
        if (nTotalUserCarts >= MAXUSERCARTS) break;                 // system max (do we need this?)
	}
	if (nTotalUserCarts+ID_USER_0 >= ID_SYSTEM_0) {
		debug_write("User cartridge count exceeded available units of %d (tell Tursi!)", ID_SYSTEM_0 - ID_USER_0);
	}
	if (nTotalUserCarts >= 1000) {
		debug_write("Exceeded maximum user cartridge count of %d (impressive!)", MAXUSERCARTS);
	}

skiprestofuser:
	// video filter mode
	FilterMode=		GetPrivateProfileInt("video",	"FilterMode",		FilterMode,		INIFILE);
	// essentially frameskip
	drawspeed=		GetPrivateProfileInt("video",	"frameskip",		drawspeed,		INIFILE);
	// graphics mode used for full screen direct X (see SetupDirectDraw() in tivdp.cpp)
	FullScreenMode=	GetPrivateProfileInt("video",	"fullscreenmode",	FullScreenMode, INIFILE);
	// heat map fade speed
	HeatMapFadeSpeed=GetPrivateProfileInt("video",	"heatmapfadespeed",	HeatMapFadeSpeed, INIFILE);
	// set interrupt rate - 50/60
	hzRate=			GetPrivateProfileInt("video",	"hzRate",			hzRate,			INIFILE);
	if ((hzRate != HZ50) && (hzRate != HZ60)) {
		// upgrade code
		if (hzRate == 50) hzRate = HZ50;
		else if (hzRate == 60) hzRate = HZ60;
		else hzRate=HZ60;
	}
	// Whether to enable the F18A support
	bF18Enabled=GetPrivateProfileInt("video",	"EnableF18A",		bF18Enabled, INIFILE);
	// Whether to allow a hacky 80 column mode
	bEnable80Columns=GetPrivateProfileInt("video",	"Enable80Col",		bEnable80Columns, INIFILE);
	// whether to allow an even hackier 128k mode (and will only be valid when 80 columsn is up for now)
	bEnable128k=GetPrivateProfileInt("video",	"Enable128k",		bEnable128k, INIFILE);
	// whether to interleave the GPU execution
	bInterleaveGPU = GetPrivateProfileInt("video",	"InterleaveGPU",	bInterleaveGPU, INIFILE);
	// whether to force correct aspect ratio
	MaintainAspect=	GetPrivateProfileInt("video",	"MaintainAspect",	MaintainAspect, INIFILE);
	// 0-none, 1-DIB, 2-DX, 3-DX Full
	StretchMode=	GetPrivateProfileInt("video",	"StretchMode",		StretchMode,	INIFILE);
	// 5 sprite per line flicker
	bUse5SpriteLimit = GetPrivateProfileInt("video","Flicker",			bUse5SpriteLimit,INIFILE);
	// default screen scale size
	nDefaultScreenScale = GetPrivateProfileInt("video","ScreenScale",	nDefaultScreenScale,INIFILE);
	// -1 means custom
	if ((nDefaultScreenScale!=-1) && ((nDefaultScreenScale < 1) || (nDefaultScreenScale > 4))) nDefaultScreenScale=1;
	nXSize = GetPrivateProfileInt("video", "ScreenX", nXSize, INIFILE);
	if (nXSize < 64) nXSize=64;
	nYSize = GetPrivateProfileInt("video", "ScreenY", nYSize, INIFILE);
	if (nYSize < 64) nYSize=64;

	// get screen position
	nVideoLeft = GetPrivateProfileInt("video",		"topX",				-1,					INIFILE);
	nVideoTop = GetPrivateProfileInt("video",		"topY",				-1,					INIFILE);

	// debug
	bScrambleMemory = GetPrivateProfileInt("debug","ScrambleRam",	bScrambleMemory, INIFILE) ? true : false;
	bCorruptDSKRAM =  GetPrivateProfileInt("debug","CorruptDSKRAM",	bCorruptDSKRAM, INIFILE) ? true : false;

	// TV stuff
	TVScanLines=	GetPrivateProfileInt("tvfilter","scanlines",		TVScanLines,	INIFILE);
	double thue, tsat, tcont, tbright, tsharp, tmp;
	tmp=			GetPrivateProfileInt("tvfilter","hue",				100,			INIFILE);
	thue=(tmp-100)/100.0;
	tmp=			GetPrivateProfileInt("tvfilter","saturation",		100,			INIFILE);
	tsat=(tmp-100)/100.0;
	tmp=			GetPrivateProfileInt("tvfilter","contrast",			100,			INIFILE);
	tcont=(tmp-100)/100.0;
	tmp=			GetPrivateProfileInt("tvfilter","brightness",		100,			INIFILE);
	tbright=(tmp-100)/100.0;
	tmp=			GetPrivateProfileInt("tvfilter","sharpness",		100,			INIFILE);
	tsharp=(tmp-100)/100.0;
	SetTVValues(thue, tsat, tcont, tbright, tsharp);

    // MRUs
    for (int idx=1; idx<=MAX_MRU; ++idx) {
        char buf[1024];
        char str[80];
        sprintf(str, "MRU%d", idx);
        GetPrivateProfileString("LastDiskMRU", str, "", buf, sizeof(buf), INIFILE);
        csLastDiskImage[idx-1] = buf;
    }

    for (int idx=1; idx<=MAX_MRU; ++idx) {
        char buf[1024];
        char str[80];
        sprintf(str, "MRU%d", idx);
        GetPrivateProfileString("LastPathMRU", str, "", buf, sizeof(buf), INIFILE);
        csLastDiskPath[idx-1] = buf;
    }
     
    for (int idx=1; idx<=MAX_MRU; ++idx) {
        char buf[1024];
        char str[80];
        sprintf(str, "MRU%d", idx);
        GetPrivateProfileString("LastCartMRU", str, "", buf, sizeof(buf), INIFILE);
        csLastUserCart[idx-1] = buf;
    }
}

// Wrapper function - not available in Win32?
void WritePrivateProfileInt(LPCTSTR lpApp, LPCTSTR lpKey, int nVal, LPCTSTR lpFile) {
	char buf[256];

	sprintf(buf, "%d", nVal);
	WritePrivateProfileString(lpApp, lpKey, buf, lpFile);
}

void SaveConfig() {
	int idx;

	WritePrivateProfileInt(		"audio",		"max_volume",			max_volume,					INIFILE);
	WritePrivateProfileInt(		"audio",		"samplerate",			AudioSampleRate,			INIFILE);
	if (NULL != GetSidEnable) {
		WritePrivateProfileInt(	"audio",		"sid_blaster",			GetSidEnable(),				INIFILE);
	}

	// write the new data
	EnterCriticalSection(&csDriveType);

	for (int idx=0; idx < MAX_DRIVES-RESERVED_DRIVES; idx++) {
		CString cs;

		cs.Format("Disk%d", idx);

		if (NULL == pDriveType[idx]) {
			WritePrivateProfileInt(cs, "Type", DISK_NONE, INIFILE);
			continue;
		}

		WritePrivateProfileInt(cs, "Type", pDriveType[idx]->GetDiskType(), INIFILE);
		WritePrivateProfileString(cs, "Path", pDriveType[idx]->GetPath(), INIFILE);

		for (int idx2=0; idx2 < DISK_OPT_MAXIMUM; idx2++) {
			int nVal;
			if (pDriveType[idx]->GetOption(idx2, nVal)) {
				WritePrivateProfileInt(cs, pszOptionNames[idx2], nVal, INIFILE);
			}
		}
	}
	LeaveCriticalSection(&csDriveType);

    WritePrivateProfileString("CF7", "BIOS", csCf7Bios, INIFILE);
    WritePrivateProfileString("CF7", "Disk", csCf7Disk, INIFILE);
    WritePrivateProfileInt("CF7", "Size", nCf7DiskSize, INIFILE);

	WritePrivateProfileString(	"emulation",	"AVIFilename",			AVIFileName,				INIFILE);
	WritePrivateProfileInt(		"emulation",	"cputhrottle",			CPUThrottle,				INIFILE);
	WritePrivateProfileInt(		"emulation",	"systemthrottle",		SystemThrottle,				INIFILE);
	if (0 != max_cpf) {
		WritePrivateProfileInt(	"emulation",	"maxcpf",				max_cpf,					INIFILE);
	}
	WritePrivateProfileInt(		"emulation",	"pauseinactive",		PauseInactive,				INIFILE);
	WritePrivateProfileInt(		"emulation",	"ctrlaltreset",			CtrlAltReset,				INIFILE);
	WritePrivateProfileInt(		"emulation",	"invertcaps",			!gDontInvertCapsLock,		INIFILE);
	WritePrivateProfileInt(     "emulation",    "speechenabled",        SpeechEnabled,              INIFILE);
	WritePrivateProfileInt(		"emulation",	"system",				nSystem,					INIFILE);
	WritePrivateProfileInt(		"emulation",	"slowdown_keyboard",	slowdown_keyboard,			INIFILE);
	WritePrivateProfileInt(		"emulation",	"ps2keyboard",			ps2keyboardok,				INIFILE);
	WritePrivateProfileInt(		"emulation",	"sams_enabled",			sams_enabled,				INIFILE);
	WritePrivateProfileInt(		"emulation",	"sams_size",			sams_size,					INIFILE);

	WritePrivateProfileInt(		"joysticks",	"active",				fJoy,						INIFILE);
	WritePrivateProfileInt(		"joysticks",	"joy1mode",				joy1mode,					INIFILE);
	WritePrivateProfileInt(		"joysticks",	"joy2mode",				joy2mode,					INIFILE);

	WritePrivateProfileInt(		"roms",			"cartgroup",			nCartGroup,					INIFILE);
	WritePrivateProfileInt(		"roms",			"cartidx",				nCart,						INIFILE);
	
	WritePrivateProfileInt(		"video",		"FilterMode",			FilterMode,					INIFILE);
	WritePrivateProfileInt(		"video",		"frameskip",			drawspeed,					INIFILE);
	WritePrivateProfileInt(		"video",		"fullscreenmode",		FullScreenMode,				INIFILE);
	WritePrivateProfileInt(		"video",		"heatmapfadespeed",		HeatMapFadeSpeed,			INIFILE);

	WritePrivateProfileInt(		"video",		"hzRate",				hzRate,						INIFILE);
	WritePrivateProfileInt(		"video",		"MaintainAspect",		MaintainAspect,				INIFILE);
	WritePrivateProfileInt(		"video",		"EnableF18A",			bF18Enabled,				INIFILE);
	WritePrivateProfileInt(		"video",		"Enable80Col",			bEnable80Columns,			INIFILE);
	WritePrivateProfileInt(		"video",		"Enable128k",			bEnable128k,			    INIFILE);
	WritePrivateProfileInt(		"video",		"InterleaveGPU",		bInterleaveGPU,				INIFILE);

	WritePrivateProfileInt(		"video",		"StretchMode",			StretchMode,				INIFILE);
	WritePrivateProfileInt(		"video",		"Flicker",				bUse5SpriteLimit,			INIFILE);
	WritePrivateProfileInt(		"video",		"ScreenScale",			nDefaultScreenScale,		INIFILE);
	WritePrivateProfileInt(		"video",		"ScreenX",				nXSize,						INIFILE);
	WritePrivateProfileInt(		"video",		"ScreenY",				nYSize,						INIFILE);

	WritePrivateProfileInt(		"video",		"topX",					gWindowRect.left,			INIFILE);
	WritePrivateProfileInt(		"video",		"topY",					gWindowRect.top,			INIFILE);

	// debug
	WritePrivateProfileInt(		"debug",		"ScrambleRam",			bScrambleMemory,			INIFILE);
	WritePrivateProfileInt(		"debug",		"CorruptDSKRAM",		bCorruptDSKRAM,				INIFILE);

	// TV stuff
	double thue, tsat, tcont, tbright, tsharp;
	int tmp;
	GetTVValues(&thue, &tsat, &tcont, &tbright, &tsharp);

	tmp=(int)((thue+1.0)*100.0);
	WritePrivateProfileInt(		"tvfilter",		"hue",					tmp,						INIFILE);
	tmp=(int)((tsat+1.0)*100.0);
	WritePrivateProfileInt(		"tvfilter",		"saturation",			tmp,						INIFILE);
	tmp=(int)((tcont+1.0)*100.0);
	WritePrivateProfileInt(		"tvfilter",		"contrast",				tmp,						INIFILE);
	tmp=(int)((tbright+1.0)*100.0);
	WritePrivateProfileInt(		"tvfilter",		"brightness",			tmp,						INIFILE);
	tmp=(int)((tsharp+1.0)*100.0);
	WritePrivateProfileInt(		"tvfilter",		"sharpness",			tmp,						INIFILE);
	WritePrivateProfileInt(		"tvfilter",		"scanlines",			TVScanLines,				INIFILE);

    // MRUs
    for (int idx=1; idx<=MAX_MRU; ++idx) {
        char str[80];
        sprintf(str, "MRU%d", idx);
        WritePrivateProfileString("LastDiskMRU", str, csLastDiskImage[idx-1], INIFILE);
    }

    for (int idx=1; idx<=MAX_MRU; ++idx) {
        char str[80];
        sprintf(str, "MRU%d", idx);
        WritePrivateProfileString("LastPathMRU", str, csLastDiskPath[idx-1], INIFILE);
    }
     
    for (int idx=1; idx<=MAX_MRU; ++idx) {
        char str[80];
        sprintf(str, "MRU%d", idx);
        WritePrivateProfileString("LastCartMRU", str, csLastUserCart[idx-1], INIFILE);
    }

}

void CloseDumpFiles() {
	for (int idx=0; idx<65536; idx++) {
		if (DumpMap[idx]) {
			if ((DumpMap[idx]>0)&&(DumpMap[idx]<10)) {
				if (DumpFile[DumpMap[idx]]) {
					debug_write("Closing dump file %d\n", DumpMap[idx]);
					fclose(DumpFile[DumpMap[idx]]);
					DumpFile[DumpMap[idx]] = NULL;
				}
			}
			DumpMap[idx] = 0;
		}
	}
}
void ReloadDumpFiles() {
	// scan all breakpoints and open any un-open files
	for (int idx=0; idx<MAX_BREAKPOINTS; idx++) {
		if (BreakPoints[idx].Type == BREAK_DISK_LOG) {
			if (NULL == DumpFile[BreakPoints[idx].Data]) {
				char buf[128];
				sprintf(buf, "dump%d.bin", BreakPoints[idx].Data);
				debug_write("Opening dump file %d as %s\n", BreakPoints[idx].Data, buf);
				DumpFile[BreakPoints[idx].Data] = fopen(buf, "wb");
			}
		}
	}
}

// rewrite the user last used list of the user carts
void UpdateUserCartMRU() {
	HMENU hMenu=GetMenu(myWnd);   // root menu
	if (hMenu) {
		hMenu=GetSubMenu(hMenu, 3);     // cartridge menu
		if (hMenu) {
			hMenu=GetSubMenu(hMenu, 2);     // user menu
			if (hMenu) {
                hMenu=GetSubMenu(hMenu, 1);     // recent menu
                if (hMenu) {
                    // we're all set now...
                    // just a dumb blind wipe of MAX_MRU items...
                    for (int idx=0; idx<MAX_MRU; ++idx) {
                        if (!DeleteMenu(hMenu, 0, MF_BYPOSITION)) break;
                    }
                    // now add the MRU list in
                    for (int idx=0; idx<MAX_MRU; ++idx) {
                        if (csLastUserCart[idx].GetLength() == 0) break;
    					AppendMenu(hMenu, MF_STRING, ID_USERCART_MRU+idx, csLastUserCart[idx]);
                    }
				}
			}
		}
	}
}

// reset the emulator
void doSystemReset() {
	memset(CRU, 1, 4096);					// reset 9901
	CRU[0]=0;	// timer control
	CRU[1]=0;	// peripheral interrupt mask
	CRU[2]=0;	// VDP interrupt mask
	CRU[3]=0;	// timer interrupt mask??
//  CRU[12-14]  // keyboard column select
//  CRU[15]     // Alpha lock 
//  CRU[24]     // audio gate (leave high)
	CRU[25]=0;	// mag tape out - needed for Robotron to work!
	CRU[27]=0;	// mag tape in (maybe all these zeros means 0 should be the default??)
	timer9901=0;
    timer9901Read = 0;
	timer9901IntReq=0;
	starttimer9901=0;
	wrword(0x83c4,0);						// Console bug work around, make sure no user int is active
	init_kb();								// Reset keyboard emulation
	if (NULL != InitSid) {
		InitSid();							// reset the SID chip
		if (NULL != SetSidBanked) {		
			SetSidBanked(false);			// switch it out for now
		}
	}
	resetDAC();
	readroms();								// reload the real ROMs
	if (NULL != pCurrentHelpMsg) {
		szDefaultWindowText="Classic99 - See Help->Known Issues for this cart";
		SetWindowText(myWnd, szDefaultWindowText);
	} else {
		szDefaultWindowText="Classic99";
		SetWindowText(myWnd, szDefaultWindowText);
	}
	pCPU->reset();
	pGPU->reset();
	pCurrentCPU = pCPU;
	bF18AActive = 0;
	for (int idx=0; idx<=PCODEGROMBASE; idx++) {
		GROMBase[idx].grmaccess=2;			// no GROM accesses yet
	}
	nCurrentDSR=-1;
	memset(nDSRBank, 0, sizeof(nDSRBank));
	doLoadInt=false;						// no pending LOAD
	vdpReset();								// TODO: should move these vars into the reset function
	vdpaccess=0;							// No VDP address writes yet 
	vdpwroteaddress=0;						// timer after a VDP address write to allow time to fetch
	vdpscanline=0;
	vdpprefetch=0;
	vdpprefetchuninited = true;
	VDPREG[0]=0;
	VDPREG[1]=0;							// VDP registers 0/1 cleared on reset per datasheet
	end_of_frame=0;							// No end of frame yet
	CPUSpeechHalt=false;					// not halted for speech reasons
	CPUSpeechHaltByte=0;					// byte pending for the speech hardware
	cpucount=0;
	cpuframes=0;
	fKeyEverPressed=false;					// No key pressed yet (to disable the warning on cart change)
	memset(CPUMemInited, 0, sizeof(CPUMemInited));	// no CPU mem written to yet
	memset(VDPMemInited, 0, sizeof(VDPMemInited));	// or VDP
	bWarmBoot = false;						// if it was a warm boot, it's done now
	// set both joysticks as active
	installedJoysticks = 0x03;
	// but don't reset g_bCheckUninit
}

///////////////////////////////////
// Main
// Startup and shutdown system
///////////////////////////////////
int WINAPI WinMain( HINSTANCE hInst, HINSTANCE hInPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	int idx;
	int err;
	char temp[255];
	WNDCLASS aclass;
	TEXTMETRIC myMetric;
	RECT myrect, myrect2;

	// Get the CS initialization done early
	InitializeCriticalSection(&VideoCS);
	InitializeCriticalSection(&DebugCS);
	InitializeCriticalSection(&csDriveType);
	InitializeCriticalSection(&csAudioBuf);
    InitializeCriticalSection(&TapeCS);

	hInstance = hInst;
	hPrevInstance=hInPrevInstance;

	// Null the pointers
	myClass=0;
	myWnd=NULL;		// Classic99 Window
	dbgWnd=NULL;	// Debug Window
	lpds=NULL;
	soundbuf=NULL;
	sidbuf=NULL;
	PasteString=NULL;
	PasteIndex=NULL;
	PasteStringHackBuffer=false;
	PasteCount=-1;
	ZeroMemory(nLoadedUserCarts, sizeof(nLoadedUserCarts));
	nLoadedUserGroups=0;
	memset(BreakPoints, 0, sizeof(BreakPoints));
	nBreakPoints=0;
	BreakOnIllegal=false;
	BreakOnDiskCorrupt=false;
	for (idx=0; idx<10; idx++) {
		DumpFile[idx]=NULL;
	}

	// set the working folder to the classic99.exe path
	{
		char path1[1024];
		int bytes = GetModuleFileName(NULL, path1, 1024);
		if (bytes) {
			SetCurrentDirectory(path1);
			debug_write("Set working folder to %s", path1);
		} else {
			debug_write("Failed to get working folder.");
		}
	}

	// Also do the Winsock init (non-fatal if fails)
	{
		WORD wVersionRequested;
		WSADATA wsaData;
		int err;
  
		/* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
		wVersionRequested = MAKEWORD(2, 2);
  
		err = WSAStartup(wVersionRequested, &wsaData);
		if (err != 0) {
			/* Tell the user that we could not find a usable */
			/* Winsock DLL.                                  */
			debug_write("Net init failed with error: %d", err);
		}
	}

	// build the CPU super early - lots of init functions write to its memory
	debug_write("Building CPU");
	pCPU = new CPU9900();							// does NOT reset
	pGPU = new GPUF18A();
	pCurrentCPU = pCPU;
	hWakeupEvent=CreateEvent(NULL, FALSE, FALSE, NULL);
	InterlockedExchange((LONG*)&cycles_left, max_cpf);

	// Get the default np font dimensions with a dummy dc
	myDC=CreateCompatibleDC(NULL);
	SelectObject(myDC, GetStockObject(ANSI_FIXED_FONT));
	if (GetTextMetrics(myDC, &myMetric)) {
		fontX=myMetric.tmMaxCharWidth;
		fontY=myMetric.tmHeight;
	} else {
		fontX=20;
		fontY=20;
	}
	DeleteDC(myDC);

	framedata=(unsigned int*)malloc((512+16)*(192+16)*4);	// This is where we draw everything - 8 pixel border - extra room left for 80 column mode
	framedata2=(unsigned int*)malloc((256+16)*4*(192+16)*4*4);// used for the filters - 16 pixel border on SAI and 8 horizontal on TV (x2), HQ4x is the largest

    if ((framedata==NULL)||(framedata2==NULL)) {
        fail("Unable to allocate framebuffers");
    }

	// create and register a class and open a window
	if (NULL == hPrevInstance)
	{
		aclass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
		aclass.lpfnWndProc = myproc;
		aclass.cbClsExtra = 0;
		aclass.cbWndExtra = 0;
		aclass.hInstance = hInstance;
		aclass.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
		aclass.hCursor = LoadCursor(NULL, IDC_ARROW);
		aclass.hbrBackground = NULL;
		aclass.lpszMenuName = MAKEINTRESOURCE(IDR_MENU1);
		aclass.lpszClassName = "TIWndClass";
		myClass = RegisterClass(&aclass);
		if (0 == myClass)
		{	
			err=GetLastError();
			sprintf(temp, "Can't create class: 0x%x", err);
			fail(temp);
		}

		// Info windows
		aclass.lpszClassName="Classic99Info";
		aclass.hbrBackground=NULL;
		aclass.lpszMenuName=NULL;
		myClass=RegisterClass(&aclass);
		if (0 == myClass) {
			debug_write("Couldn't register debug window class: 0x%x", GetLastError());
		}
	}

	myWnd = CreateWindow("TIWndClass", "Classic99", g_dwMyStyle, CW_USEDEFAULT, CW_USEDEFAULT, 536, 446, NULL, NULL, hInstance, NULL);
	if (NULL == myWnd)
	{	
		err=GetLastError();
		sprintf(temp, "Can't open window: %x", err);
		fail(temp);
	}
	ShowWindow(myWnd, SW_HIDE);
	UpdateWindow(myWnd);
	SetActiveWindow(myWnd);

	// start the debug updater thread
	if (-1 == _beginthread(DebugUpdateThread, 0, NULL)) {
		debug_write("Failed to start debug update thread.");
	} else {
		debug_write("Debug update thread started.");
	}

	// Fill in the menus
	HMENU hMenu;
	// Systems
	hMenu=GetMenu(myWnd);
	if (hMenu) {
		hMenu=GetSubMenu(hMenu, 2);
		if (hMenu) {
			for (idx=0; idx<sizeof(Systems)/sizeof(struct CARTS); idx++) {
				AppendMenu(hMenu, MF_STRING, ID_SYSTEM_0+idx, Systems[idx].szName);
			}
			DeleteMenu(hMenu, 0, MF_BYPOSITION);	// remove temp separator
		}
	}
	// Apps
	hMenu=GetMenu(myWnd);
	if (hMenu) {
		hMenu=GetSubMenu(hMenu, 3);
		if (hMenu) {
			hMenu=GetSubMenu(hMenu, 0);
			if (hMenu) {
				for (idx=0; idx<sizeof(Apps)/sizeof(struct CARTS); idx++) {
					AppendMenu(hMenu, MF_STRING, ID_APP_0+idx, Apps[idx].szName);
				}
				DeleteMenu(hMenu, 0, MF_BYPOSITION);	// remove temp separator
			}
		}
	}
	// Games
	hMenu=GetMenu(myWnd);
	if (hMenu) {
		hMenu=GetSubMenu(hMenu, 3);
		if (hMenu) {
			hMenu=GetSubMenu(hMenu, 1);
			if (hMenu) {
				for (idx=0; idx<sizeof(Games)/sizeof(struct CARTS); idx++) {
					AppendMenu(hMenu, MF_STRING, ID_GAME_0+idx, Games[idx].szName);
				}
				DeleteMenu(hMenu, 0, MF_BYPOSITION);	// remove temp separator
			}
		}
	}

	// create the default user0 cart (used for 'open')
	Users=(CARTS*)malloc(sizeof(CARTS));
    if (NULL == Users) {
        fail("Failed to allocate user cart memory default");
    }
	ZeroMemory(Users, sizeof(CARTS));

	for (idx=0; idx<=PCODEGROMBASE; idx++) {
		GROMBase[idx].GRMADD=0;
		for (int i2=0; i2<8; i2++) {
			GROMBase[idx].bWritable[i2]=false;
		}
		GROMBase[idx].grmaccess=2;
		GROMBase[idx].grmdata=0;
        GROMBase[idx].LastRead=0;   // only idx==0 is used though
        GROMBase[idx].LastBase=0;
	}
		
	quitflag=0;			// no quit yet

	// clear debugging strings
	memset(lines, 0, sizeof(lines));
	memset(Disasm, 0, sizeof(Disasm));
	bDebugDirty=true;

	// Print some initial debug
	debug_write("---");
	debug_write("Classic99 version %s (C)2002-2017 M.Brent", VERSION);
	debug_write("ROM files included under license from Texas Instruments");

	// copy out the command line
	memset(g_cmdLine, 0, sizeof(g_cmdLine));
	if (NULL != lpCmdLine) {
		strncpy(g_cmdLine, lpCmdLine, sizeof(g_cmdLine));
		g_cmdLine[sizeof(g_cmdLine)-1]='\0';
		debug_write("Got command line: %s", g_cmdLine);
	}
	 
	// Set default values for config (alphabetized here)
	strcpy(AVIFileName, "C:\\Classic99.AVI");	// default movie filename
	nCartGroup=0;				// Cartridge group (0-apps, 1-games, 2-user)
	nCart=-1;					// loaded cartridge (-1 is none)
	CPUThrottle=CPU_NORMAL;		// throttle the CPU
	SystemThrottle=VDP_CPUSYNC;	// throttle the VDP
	drawspeed=0;				// no frameskip
	FilterMode=2;				// super 2xSAI
	nDefaultScreenScale=1;		// 1x by default
	nXSize = 256+16;			// default size, but not used while screenscale is set
	nYSize = 192+16;
	FullScreenMode=6;			// full screen at 640x480x16
	fJoy=1;						// enable joysticks
	joy1mode=0;					// keyboard
	joy2mode=1;					// joystick 1
	fJoystickActiveOnKeys=0;	// not reading joystick in the last 3 seconds or so (180 frames)
	hzRate=HZ60;				// 60 hz
	MaintainAspect=1;			// Keep aspect ratio
	bEnable80Columns=1;			// allow the 80 column hack
	bEnable128k=0;				// disable the 128k mode
	max_cpf=DEFAULT_60HZ_CPF;	// max cycles per frame
	cfg_cpf=max_cpf;
	PauseInactive=0;			// don't pause when window inactive
	SpeechEnabled=1;			// speech is decent now
	Recording=0;				// not recording AVI
	slowdown_keyboard=1;		// slow down keyboard repeat when read via BASIC
	StretchMode=2;				// dx
	bUse5SpriteLimit=1;			// enable flicker by default
	TVScanLines=1;				// on by default
	sams_enabled=1;				// off by default
	sams_size=3;				// 1MB by default when on (no reason not to use a large card)
	nSystem=1;					// TI-99/4A
	max_volume=80;				// percentage of maximum volume to use
	doLoadInt=false;			// no pending LOAD
	pCPU->enableDebug=1;		// whether breakpoints affect CPU
	pGPU->enableDebug=1;		// whether breakpoints affect GPU

	// initialize debugger links
	InitBug99();
	initDbgHook();

	// init disk DSR system
	InitDiskDSR();

	// Load the Audio code (must be before config)
	PrepareSID();
	if (NULL != SetSidEnable) {
		SetSidEnable(false);	// by default, off
	}

	// Read configuration - uses above settings as default!
	ReadConfig();

	// position the window if needed
	if ((nVideoLeft != -1) || (nVideoTop != -1)) {
		RECT check;
		check.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
		check.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
		check.right = check.left + GetSystemMetrics(SM_CXVIRTUALSCREEN) - 256;
		check.bottom = check.top + GetSystemMetrics(SM_CYVIRTUALSCREEN) - 192;
		// if it looks onscreen more or less, then allow it
		if ((nVideoLeft >= check.left) && (nVideoLeft <= check.right) && (nVideoTop >= check.top) && (nVideoTop <= check.bottom)) {
			SetWindowPos(myWnd, HWND_TOP, nVideoLeft, nVideoTop, 0, 0, SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOZORDER);
		}
	}
	ShowWindow(myWnd, nCmdShow);

	// Update user menu - this will be a function later
	hMenu=GetMenu(myWnd);   // root menu
	if (hMenu) {
		hMenu=GetSubMenu(hMenu, 3);     // cartridge menu
		if (hMenu) {
			hMenu=GetSubMenu(hMenu, 2);     // user menu
			if (hMenu) {
				// User ROMs are a bit different, since we have to read them
				// from the configuration - now nested two deep!
				int nCartIdx = 1;		// skip cart 0, it's used by the open menu
				for (int cart = 0; cart < nLoadedUserGroups; cart++) {
					HMENU hRef = hMenu;
					// as a bit of backwards compatibility, if it's called 'usercart' then store it in the original location
					if (strcmp(UserGroupNames[cart], "UserCart") != 0) {
						// it's different, so create a submenu for it
						hRef = CreateMenu();
						AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hRef, UserGroupNames[cart]);
					}
					for (idx=0; idx<nLoadedUserCarts[cart]; idx++) {
						AppendMenu(hRef, MF_STRING, Users[nCartIdx].nUserMenu, Users[nCartIdx].szName);
						nCartIdx++;
					}
				}
			}
		}
	}
    UpdateUserCartMRU();
	DrawMenuBar(myWnd);
	
	// set temp stuff
	cfg_cpf=max_cpf;

	// Load a dummy CPU ROM for the emu to spin on till we load something real
	WriteMemoryBlock(0x0000, DummyROM, 6);

	// start the video processor
	debug_write("Starting Video");
	startvdp();

	// wait for the video thread to initialize so we can resize the window :)
	Sleep(500);

	if (nDefaultScreenScale != -1) {
		SendMessage(myWnd, WM_COMMAND, ID_CHANGESIZE_1X+nDefaultScreenScale-1, 1);
	} else {
		SetWindowPos(myWnd, HWND_TOP, nVideoLeft, nVideoTop, nXSize, nYSize, SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOZORDER);
	}

	// Set menu-based settings (lParam 1 means it's coming from here, not the user)
	// Only some messages care about that param, though
	SendMessage(myWnd, WM_COMMAND, ID_SYSTEM_0+nSystem, 1);
	SendMessage(myWnd, WM_COMMAND, ID_OPTIONS_CPUTHROTTLING, 1);
	SendMessage(myWnd, WM_COMMAND, ID_DISK_CORRUPTDSKRAM, 1);
	SendMessage(myWnd, WM_COMMAND, ID_VIDEO_MAINTAINASPECT, 1);
	SendMessage(myWnd, WM_COMMAND, ID_VIDEO_FILTERMODE_NONE+FilterMode, 1);
	SendMessage(myWnd, WM_COMMAND, ID_VIDEO_ENABLEF18A, 1);
	SendMessage(myWnd, WM_COMMAND, ID_VIDEO_INTERLEAVEGPU, 1);
	SendMessage(myWnd, WM_COMMAND, ID_VIDEO_ENABLE80COLUMNHACK, 1);
	SendMessage(myWnd, WM_COMMAND, ID_VIDEO_ENABLE128KHACK, 1);

	if ((StretchMode>=0)&&(StretchMode<=2)) {
		SendMessage(myWnd, WM_COMMAND, ID_VIDEO_STRETCHMODE_NONE+StretchMode, 1);
	} else {
		if (StretchMode==3) {
			SendMessage(myWnd, WM_COMMAND, ID_VIDEO_STRETCHMODE_DXFULL_320X240X8+FullScreenMode-1, 1);
		}
	}
	SendMessage(myWnd, WM_COMMAND, ID_VIDEO_50HZ, 1);
	SendMessage(myWnd, WM_COMMAND, ID_OPTIONS_PAUSEINACTIVE, 1);
	if (SpeechEnabled) SendMessage(myWnd, WM_COMMAND, ID_OPTIONS_SPEECHENABLED, 1);
	if (CtrlAltReset) SendMessage(myWnd, WM_COMMAND, ID_OPTIONS_CTRL_RESET, 1);
	if (!gDontInvertCapsLock) SendMessage(myWnd, WM_COMMAND, ID_OPTIONS_INVERTCAPSLOCK, 1);
	SendMessage(myWnd, WM_COMMAND, ID_VIDEO_FLICKER, 1);
	
	if (nCart != -1) {
		switch (nCartGroup) {
		case 0:
			SendMessage(myWnd, WM_COMMAND, ID_APP_0+nCart, 1);
			break;

		case 1:
			SendMessage(myWnd, WM_COMMAND, ID_GAME_0+nCart, 1);
			break;

		case 2:
			SendMessage(myWnd, WM_COMMAND, ID_USER_0+nCart, 1);
			break;
		}
	} else {
		// no cart, we still need to send a reset to load up the system
		SendMessage(myWnd, WM_COMMAND, ID_FILE_RESET, 1);
	}

    // Init the system
    doSystemReset();

	// start sound
	debug_write("Starting Sound");
	startsound();
	
	// Init disk
	debug_write("Starting Disk");

	// prepare the emulation...
	timercount=0;

	// set up 60hz timer
	myThread=_beginthread(TimerThread, 0, NULL);
	if (myThread != -1) {
		debug_write("Timer thread began...");
		// try to raise the priority of the thread
		if (!SetThreadPriority((HANDLE)myThread, THREAD_PRIORITY_ABOVE_NORMAL)) {
			debug_write("Failed to update thread priority.");
		}
	} else {
		debug_write("Timer thread failed.");
	}
	// set up speech buffer restart
	myThread=_beginthread(SpeechBufThread, 0, NULL);
	if (myThread != -1) {
		debug_write("Speech Buffer thread began...");
		if (!SetThreadPriority((HANDLE)myThread, THREAD_PRIORITY_ABOVE_NORMAL)) {
			debug_write("Failed to update thread priority.");
		}
	} else {
		debug_write("Speech Buffer thread failed.");
	}

	Sleep(100);			// time for threads to start

	// start up CPU handler
	myThread=_beginthread(emulti, 0, NULL);
	if (myThread != -1) {
		debug_write("CPU thread began...");
	} else {
		debug_write("CPU thread failed.");
		// that's fatal, otherwise nothing will work
		quitflag=1;
	}

	// window management start - returns when it's time to exit
	debug_write("Starting Window management");
	SetFocus(myWnd);
	WindowThread();

    // quiet down the audio
    MuteAudio();
	// save out our config
	SaveConfig();
	// save any previous NVRAM
	saveroms();

	// Fail is the full exit
	debug_write("Shutting down");
	fail("Normal Termination");
	CloseHandle(hWakeupEvent);

	ShutdownMemorySystem();
	CloseDumpFiles();

	// shutdown Winsock
	WSACleanup();

	// good bye
	return 0;
}


//////////////////////////////////////////////////////
// start up the sound system
//////////////////////////////////////////////////////
void GenerateToneBuffer() {
	unsigned int idx2;
	UCHAR c;
	UCHAR *ptr1, *ptr2;
	unsigned long len1, len2, len;
	DSBUFFERDESC dsbd;
	WAVEFORMATEX pcmwf;

	EnterCriticalSection(&csAudioBuf);

	// if we already have one, get rid of it
	if (NULL != soundbuf) {
		rampVolume(soundbuf, DSBVOLUME_MIN);
		Sleep(1);
		soundbuf->Stop();
		soundbuf->Release();
		soundbuf=NULL;
	}

	// calculate new buffer size - 1 second of sample rate, rounded up to a multiple of hzRate (fps)
	CalculatedAudioBufferSize=AudioSampleRate;
	if (CalculatedAudioBufferSize%(hzRate) > 0) {
		CalculatedAudioBufferSize=((CalculatedAudioBufferSize/(hzRate))+1)*(hzRate);
	}
	CalculatedAudioBufferSize*=2;		// now upscale from samples to bytes
	debug_write("Sample rate: %dhz, Buffer size: %d bytes", AudioSampleRate, CalculatedAudioBufferSize);

	// Here's the format of the audio buffer, 16 bit signed today
	ZeroMemory(&pcmwf, sizeof(pcmwf));
	pcmwf.wFormatTag = WAVE_FORMAT_PCM;		// wave file
	pcmwf.nChannels=1;						// 1 channel (mono)
	pcmwf.nSamplesPerSec=AudioSampleRate;	// 22khz
	pcmwf.nBlockAlign=2;					// 2 bytes per sample * 1 channel
	pcmwf.nAvgBytesPerSec=pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;
	pcmwf.wBitsPerSample=16;				// 16 bit samples
	pcmwf.cbSize=0;							// always zero (extra data size, not struct size)

	ZeroMemory(&dsbd, sizeof(dsbd));
	dsbd.dwSize=sizeof(dsbd);
	dsbd.dwFlags=DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
//	dsbd.dwBufferBytes=AUDIO_BUFFER_SIZE;	// the sample is AUDIO_BUFFER_SIZE bytes long
	dsbd.dwBufferBytes=CalculatedAudioBufferSize;	// the sample is CalculatedAudioBufferSize bytes long
	dsbd.lpwfxFormat=&pcmwf;
	dsbd.guid3DAlgorithm=GUID_NULL;

	if (FAILED(lpds->CreateSoundBuffer(&dsbd, &soundbuf, NULL)))
	{
		debug_write("Failed to create sound buffer");
		LeaveCriticalSection(&csAudioBuf);
		return;
	}
	
	if (SUCCEEDED(soundbuf->Lock(0, CalculatedAudioBufferSize, (void**)&ptr1, &len1, (void**)&ptr2, &len2, DSBLOCK_ENTIREBUFFER)))
	{
		// since we haven't started the sound, hopefully the second pointer is null
		if (len2 != 0) {
			MessageBox(myWnd, "Failed to lock tone buffer", "Classic99 Error", MB_OK);
		}

		// just make sure it's all zeroed out
		memset(ptr1, 0, len1);
		
		// and unlock
		soundbuf->Unlock(ptr1, len1, ptr2, len2);
	}

	// mute for now - caller will set the volume
	soundbuf->SetVolume(DSBVOLUME_MIN);

	if (FAILED(soundbuf->Play(0, 0, DSBPLAY_LOOPING))) {
		debug_write("Voice DID NOT START");
	}

	LeaveCriticalSection(&csAudioBuf);
}

void GenerateSIDBuffer() {
	unsigned int idx2;
	UCHAR c;
	UCHAR *ptr1, *ptr2;
	unsigned long len1, len2, len;
	DSBUFFERDESC dsbd;
	WAVEFORMATEX pcmwf;

	EnterCriticalSection(&csAudioBuf);

	// if we already have one, get rid of it
	if (NULL != sidbuf) {
		rampVolume(sidbuf, DSBVOLUME_MIN);
		Sleep(1);
		sidbuf->Stop();
		sidbuf->Release();
		sidbuf=NULL;
	}

	// calculate new buffer size - 1 second of sample rate, rounded up to a multiple of hzRate (fps)
	CalculatedAudioBufferSize=AudioSampleRate;
	if (CalculatedAudioBufferSize%(hzRate) > 0) {
		CalculatedAudioBufferSize=((CalculatedAudioBufferSize/(hzRate)+1)*(hzRate));
	}
	CalculatedAudioBufferSize*=2;		// now upscale from samples to bytes

	// Here's the format of the audio buffer, 16 bit signed today
	ZeroMemory(&pcmwf, sizeof(pcmwf));
	pcmwf.wFormatTag = WAVE_FORMAT_PCM;		// wave file
	pcmwf.nChannels=1;						// 1 channel (mono)
	pcmwf.nSamplesPerSec=AudioSampleRate;	// 22khz
	pcmwf.nBlockAlign=2;					// 2 bytes per sample * 1 channel
	pcmwf.nAvgBytesPerSec=pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;
	pcmwf.wBitsPerSample=16;				// 16 bit samples
	pcmwf.cbSize=0;							// always zero (extra data size, not struct size)

	ZeroMemory(&dsbd, sizeof(dsbd));
	dsbd.dwSize=sizeof(dsbd);
	dsbd.dwFlags=DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
//	dsbd.dwBufferBytes=AUDIO_BUFFER_SIZE;	// the sample is AUDIO_BUFFER_SIZE bytes long
	dsbd.dwBufferBytes=CalculatedAudioBufferSize;	// the sample is CalculatedAudioBufferSize bytes long
	dsbd.lpwfxFormat=&pcmwf;
	dsbd.guid3DAlgorithm=GUID_NULL;

	if (FAILED(lpds->CreateSoundBuffer(&dsbd, &sidbuf, NULL)))
	{
		debug_write("Failed to create SID buffer");
		LeaveCriticalSection(&csAudioBuf);
		return;
	}
	
	if (SUCCEEDED(sidbuf->Lock(0, CalculatedAudioBufferSize, (void**)&ptr1, &len1, (void**)&ptr2, &len2, DSBLOCK_ENTIREBUFFER)))
	{
		// since we haven't started the sound, hopefully the second pointer is null
		if (len2 != 0) {
			MessageBox(myWnd, "Failed to lock SID buffer", "Classic99 Error", MB_OK);
		}

		// just make sure it's all zeroed out
		memset(ptr1, 0, len1);
		
		// and unlock
		sidbuf->Unlock(ptr1, len1, ptr2, len2);
	}

	// mute for now - caller will set the volume
	sidbuf->SetVolume(DSBVOLUME_MIN);

	if (FAILED(sidbuf->Play(0, 0, DSBPLAY_LOOPING))) {
		debug_write("SID DID NOT START");
	}

	LeaveCriticalSection(&csAudioBuf);
}

void startsound()
{ /* start up the sound files */

	DSBUFFERDESC dsbd;
	WAVEFORMATEX pcmwf;
	unsigned int idx, idx2;
	UCHAR *ptr1, *ptr2;
	unsigned long len1, len2, len;
	char buf[80];
	latch_byte=0;

	if (FAILED(DirectSoundCreate(NULL, &lpds, NULL)))
	{
		lpds=NULL;		// no sound
		return;
	}
	
	if (FAILED(lpds->SetCooperativeLevel(myWnd, DSSCL_NORMAL)))	// normal created a 22khz, 8 bit stereo DirectSound system
	{
		lpds->Release();
		lpds=NULL;
		return;
	}

	sound_init(AudioSampleRate);

	GenerateToneBuffer();
	GenerateSIDBuffer();

	// load the Speech DLL
	hSpeechDll=LoadLibrary("SpeechDll.dll");
	if (NULL == hSpeechDll) {
		debug_write("Failed to load speech library.");
	} else {
		SpeechInit=(void (*)(Byte*,int,int,int))GetProcAddress(hSpeechDll, "SpeechInit");
		SpeechStop=(void (*)(void))GetProcAddress(hSpeechDll, "SpeechStop");
		SpeechRead=(Byte (*)(void))GetProcAddress(hSpeechDll, "SpeechRead");
		SpeechWrite=(bool (*)(Byte, bool))GetProcAddress(hSpeechDll, "SpeechWrite");
		SpeechProcess=(void (*)(Byte*,int))GetProcAddress(hSpeechDll, "SpeechProcess");
	}

	// Zero the temporary buffers
	memset(SpeechTmp, 0, sizeof(SpeechTmp));
	nSpeechTmpPos=0;

	// Now load up the speech system
	/* start audio stream - SPEECHBUFFER buffer, 16 bit, 8khz, max vol, center */
	ZeroMemory(&pcmwf, sizeof(pcmwf));
	pcmwf.wFormatTag = WAVE_FORMAT_PCM;		// wave file
	pcmwf.nChannels=1;						// 1 channel (mono)
	pcmwf.nSamplesPerSec=SPEECHRATE;		// Should be 8khz
	pcmwf.nBlockAlign=2;					// 2 bytes per sample * 1 channel
	pcmwf.nAvgBytesPerSec=pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;
	pcmwf.wBitsPerSample=16;				// 16 bit samples
	pcmwf.cbSize=0;							// always zero;

	ZeroMemory(&dsbd, sizeof(dsbd));
	dsbd.dwSize=sizeof(dsbd);
	dsbd.dwFlags=DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
	dsbd.dwBufferBytes=SPEECHBUFFER;
	dsbd.lpwfxFormat=&pcmwf;

	hSpeechBufferClearEvent=CreateEvent(NULL, FALSE, FALSE, NULL);

	if (FAILED(lpds->CreateSoundBuffer(&dsbd, &speechbuf, NULL))) {
		debug_write("Failed to create speech sound buffer!");
	} else {
		if (SUCCEEDED(speechbuf->Lock(0, SPEECHBUFFER*pcmwf.nBlockAlign, (void**)&ptr1, &len1, (void**)&ptr2, &len2, DSBLOCK_ENTIREBUFFER))) {
			// since we haven't started the sound, hopefully the second pointer is nil
			if (len2 != 0) {
				debug_write("Failed to lock speech buffer");
			}

			// signed 16-bit - zero the buffer
			memset(ptr1, 0, len1);

			speechbuf->Unlock(ptr1, len1, ptr2, len2);
		}

		LPDIRECTSOUNDNOTIFY8 lpDsNotify;
		if (SUCCEEDED(speechbuf->QueryInterface(IID_IDirectSoundNotify8,(LPVOID*)&lpDsNotify))) {
			DSBPOSITIONNOTIFY notify;
			notify.dwOffset=DSBPN_OFFSETSTOP;
			notify.hEventNotify=hSpeechBufferClearEvent;
			if (FAILED(lpDsNotify->SetNotificationPositions(1, &notify))) {
				debug_write("Failed to set notification positions.");
			}
			lpDsNotify->Release();
		} else {
			debug_write("Failed to get DS8 interface for speech.");
		}

		speechbuf->SetVolume(DSBVOLUME_MIN);

		if (FAILED(speechbuf->Play(0, 0, 0))) {
			debug_write("Speech DID NOT START");
		}

		if (SpeechInit) SpeechInit(SPEECH, 0x8000, SPEECHBUFFER, SPEECHRATE);
	}

	SetSoundVolumes();
}

//////////////////////////////////////////////////////////
// Start up the video system 
//////////////////////////////////////////////////////////
void startvdp()
{ 
	// call VDP Startup
	hVideoThread=NULL;
	hVideoThread=_beginthread((void (__cdecl *)(void*))VDPmain, 0, NULL);
	if (hVideoThread != -1)
		debug_write("Video Thread began...");
	else
		debug_write("Video Thread failed.");

	Sleep(100);

	// first retrace
	retrace_count=0;
}

//////////////////////////////////////////////////////////
// Non-fatal recoverable (?) error
//////////////////////////////////////////////////////////
void warn(char *x)
{ 
	// Warn will for now just dump a message into the log
	// eventually it should pop up a window and ask about
	// continuing

	// note: we assume -2 for the PC, but it could be something else!
	debug_write("%s at address >%04X, Bank >%04X, DSR >%04X", x, pCurrentCPU->GetPC()-2, (xb<<8)|(xbBank), nCurrentDSR&0xffff);
}

//////////////////////////////////////////////////////////
// Fatal error - clean up and exit
// Note that normal exit is a fatal error ;)
//////////////////////////////////////////////////////////
void fail(char *x)
{ 
	// fatal error
	char buffer[1024];
	char buf2[256];
	int idx;

	// just in case it's not set yet
	quitflag=1;

	// add to the log - not useful now, but maybe in the future when it writes to disk
	debug_write(x);

	timeEndPeriod(1);

	sprintf(buffer,"\n%s\n",x);
	sprintf(buf2,"PC-%.4X  WP-%.4X  ST-%.4X\nGROM-%.4X VDP-%.4X\n",pCurrentCPU->GetPC(),pCurrentCPU->GetWP(),pCurrentCPU->GetST(),GROMBase[0].GRMADD,VDPADD);
	strcat(buffer,buf2);
	sprintf(buf2,"Run Duration  : %d seconds\n",timercount/hzRate);
	strcat(buffer,buf2);
	sprintf(buf2,"Operation time: %d instructions processed.\n",cpucount);
	strcat(buffer,buf2);
	sprintf(buf2,"Operation time: %lu cycles processed.\n",total_cycles);
	strcat(buffer,buf2);
	sprintf(buf2,"Display frames: %d video frames displayed.\n",cpuframes);
	strcat(buffer,buf2);

	if (timercount<hzRate) timercount=hzRate;	// avoid divide by zero
	
	sprintf(buf2,"Average speed : %d instructions per second.\n",cpucount/(timercount/hzRate));
	strcat(buffer,buf2);
	sprintf(buf2,"Average speed : %lu cycles per second.\n",total_cycles/(timercount/hzRate));
	strcat(buffer,buf2);
	sprintf(buf2,"Frameskip     : %d\n",drawspeed);
	strcat(buffer,buf2);

	// the messagebox fails during a normal exit in WIN32.. why is that?
	OutputDebugString(buffer);
	MessageBox(myWnd, buffer, "Classic99 Exit", MB_OK);

	Sleep(600);			// give the threads a little time to shut down

	if (Recording) {
		CloseAVI();
	}

	if (SpeechStop) SpeechStop();

    if (speechbuf) {
		speechbuf->Stop();
		speechbuf->Release();
		speechbuf=NULL;
	}

	if (soundbuf)
	{
		soundbuf->Stop();
		soundbuf->Release();
		soundbuf=NULL;
	}

	if (sidbuf)
	{
		sidbuf->Stop();
		sidbuf->Release();
		sidbuf=NULL;
	}

	if (lpds) {
		lpds->Release();
	}

	if (Users) {
		free(Users);
	}

	if (myWnd) {
		DestroyWindow(myWnd);
	}
	
	if (myClass) {
		UnregisterClass("TIWndClass", hInstance);
	}

	if (framedata) free(framedata);
	if (framedata2) free(framedata2);

	if (hSpeechDll) {
		FreeLibrary(hSpeechDll);
		hSpeechDll=NULL;
	}

	exit(0);
}

/////////////////////////////////////////////////////////
// Return a Word from CPU memory
/////////////////////////////////////////////////////////
Word romword(Word x, bool rmw)
{ 
    x&=0xfffe;		// drop LSB

	// This reads the LSB first. This is the correct order (verified)
    // although this line of code is not really guaranteed to do that...
	return((rcpubyte(x,rmw)<<8)+rcpubyte(x+1,rmw));
}

/////////////////////////////////////////////////////////
// Write a Word y to CPU memory x
/////////////////////////////////////////////////////////
void wrword(Word x, Word y)
{ 
	Word nTmp;

	x&=0xfffe;		// drop LSB

	// now write the new data, LSB first
	wcpubyte(x+1,(Byte)(y&0xff));
	wcpubyte(x,(Byte)(y>>8));

	// check breakpoints against what was written to where
	for (int idx=0; idx<nBreakPoints; idx++) {
		switch (BreakPoints[idx].Type) {
			case BREAK_EQUALS_WORD:
				if (CheckRange(idx, x)) {
					if ((y&BreakPoints[idx].Mask) == BreakPoints[idx].Data) {		// value matches
						TriggerBreakPoint();
					}
				}
				break;

			case BREAK_EQUALS_REGISTER:
				nTmp=pCurrentCPU->GetWP()+(BreakPoints[idx].A*2);
				if ((nTmp == x) && ((y&BreakPoints[idx].Mask) == BreakPoints[idx].Data)) {
					TriggerBreakPoint();
				}
				break;
		}
	}
}

/////////////////////////////////////////////////////////
// Window message loop
/////////////////////////////////////////////////////////
void WindowThread() {
	MSG msg;
	HACCEL hAccels;		// keyboard accelerator table for the debug window
	char buf[128];
	static FILE *fp=NULL;
	int cnt, idx, wid;

	hAccels = LoadAccelerators(NULL, MAKEINTRESOURCE(DebugAccel));
    bWindowInitComplete = true;     // we're finally up and running

	while (!quitflag) {
		// check for messages
		if (0 == GetMessage(&msg, NULL, 0, 0)) {
			quitflag=1;
			break;
		} else {
			if (msg.message == WM_QUIT) {
				// shouldn't happen, since GetMessage should return 0
				quitflag=1;
			} 
			
			if (IsWindow(dbgWnd)) {
				if (TranslateAccelerator(dbgWnd, hAccels, &msg)) {
					// processed (must be before IsDialogMessage)
					continue;
				}
				if (IsDialogMessage(dbgWnd, &msg)) {
					// processed
					continue;
				}
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);		// this will push it to the Window procedure
		}
	}
}

/////////////////////////////////////////////////////////
// Main loop for Emulation
/////////////////////////////////////////////////////////
void __cdecl emulti(void *)
{
	quitflag=0;							// Don't quit

	while (!quitflag)
	{ 
		if ((PauseInactive)&&(myWnd != GetForegroundWindow())&&(dbgWnd != GetForegroundWindow())) {
			// we're supposed to pause when inactive, and we are not active
			// So, don't execute an instruction, and sleep a bit to relieve CPU
			// also clear the current timeslice so the machine doesn't run crazy fast when
			// we come back
			Sleep(100);
			InterlockedExchange((LONG*)&cycles_left, 0);
		} else {
			// execute one opcode
			do1();

			// GPU 
			if (bInterleaveGPU) {
				// todo: this is a hack for interleaving F18GPU with the 9900 - and it works, but.. not correct at all.
				if (pGPU->GetIdle() == 0) {
					pCurrentCPU = pGPU;
					for (int nCnt = 0; nCnt < 20; nCnt++) {	// /instructions/ per 9900 instruction - approximation!!
						do1();
						if (pGPU->GetIdle()) {
							break;
						}
						// handle step
						if (cycles_left <= 1) {
							break;
						}
					}
					pCurrentCPU = pCPU;
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////
// Read and process the load files
//////////////////////////////////////////////////////////
void LoadOneImg(struct IMG *pImg, char *szFork) {
	FILE *fp;
	char *pData;
	HRSRC hRsrc;
	HGLOBAL hGlob;
	unsigned char *DiskFile=NULL;	// ROM plus 6 byte kracker header max
	char *pszFrom="resource";
	char szFilename[MAX_PATH+3]="";		// extra for parenthesis and space

	if ((NULL == pImg) || (pImg->nType == TYPE_NONE) || (pImg->nType == TYPE_UNSET)) return;

	pData=NULL;

	int nLen=pImg->nLength;

	if (pImg->nType == TYPE_AUTO) {
		// figure out what it should be by filename, and flag the length to be filled in
		char *pPos = strrchr(pImg->szFileName, '.');
		if ((NULL == pPos) || (pPos == pImg->szFileName)) {
            // there's no extension at all. Since the advent of the FinalROM, this has become
            // a defacto standard of non-inverted ROM cartridge, so it's time to get with the times!
			debug_write("No extension for filename '%s', assuming non-inverted ROM.", pImg->szFileName);
			nLen = -1;		// flag to fill in after loading
			pImg->nType = TYPE_378;
			pImg->nLoadAddr = 0x0000;
		} else {
			nLen = -1;		// flag to fill in after loading
			pImg->nLoadAddr = 0x6000;		// default except for 379 and NVRAM, fixed below
			pPos--;
			switch (*pPos) {
				case 'C':
					pImg->nType = TYPE_ROM;
					break;

				case 'D':
					pImg->nType = TYPE_XB;
					break;

				case 'G':
					pImg->nType = TYPE_GROM;
					break;

				case '3':
				case '9':
					pImg->nType = TYPE_379;
					pImg->nLoadAddr = 0x0000;
					break;

				case '8':
					pImg->nType = TYPE_378;
					pImg->nLoadAddr = 0x0000;
					break;

				case 'N':
					pImg->nType = TYPE_NVRAM;
					pImg->nLoadAddr = 0x7000;		// assuming minimem, if not you need an INI
					pImg->nLength = nLen = 0x1000;	// in case it's not loaded
					break;

				default:
					debug_write("AUTO type not supported for filename '%s' (unrecognized type)", pImg->szFileName);
					return;
			}
		}
		// not auto anymore! (this may cause small issues with ROMs that change size, AUTO should not be used for development)
	}

	if ((TYPE_KEYS != pImg->nType) && (TYPE_OTHER != pImg->nType)) {
		if (NULL != pImg->dwImg) {
			hRsrc=FindResource(NULL, MAKEINTRESOURCE(pImg->dwImg), szFork);
			if (hRsrc) {
				int nRealLen=SizeofResource(NULL, hRsrc);
				if (nLen > nRealLen) nLen=nRealLen;

				hGlob=LoadResource(NULL, hRsrc);
				if (NULL != hGlob) {
					pData=(char*)LockResource(hGlob);
				}
			}
		} else {
			// It's a disk file. Worse, it may be a disk file with a header. 
			// But we may be able to determine that.
			if (strlen(pImg->szFileName) == 0) {
				return;
			}
			fp=fopen(pImg->szFileName, "rb");
			if (NULL == fp) {
				debug_write("Failed to load '%s', error %d", pImg->szFileName, errno);
				return;
			}
			pszFrom="disk";
			int nRealLen=0;
            
            // get filesize
            fseek(fp, 0, SEEK_END);
            int fSize = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            if (NULL != DiskFile) free(DiskFile);
            if (fSize > MAX_BANKSWITCH_SIZE+6) {
                fSize = MAX_BANKSWITCH_SIZE+6;
            }
            DiskFile=(unsigned char*)malloc(fSize);
            if (NULL == DiskFile) {
                debug_write("Can't allocate memory for read of '%s'!", pImg->szFileName);
                return;
            }

			while ((!feof(fp)) && (nRealLen < fSize)) {
				int nTmp = fread(&DiskFile[nRealLen], 1, fSize-nRealLen, fp);
				if (nTmp == 0) {
					debug_write("Failed to read entire file - too large or disk error. Max size = %dk!", MAX_BANKSWITCH_SIZE/1024);
					break;
				}
				nRealLen+=nTmp;
			}
			fclose(fp);

			// don't check if it is a 379 or MBX type file - this is for GRAMKracker files
			if ((nRealLen > 6) && ((pImg->nType == TYPE_ROM)||(pImg->nType == TYPE_XB)||(pImg->nType == TYPE_GROM))) {
				// Check for 6 byte header - our simple check is if
				// byte 0 is 0x00 or 0xff, and bytes 4/5 contain the
				// load address, then strip the first six bytes
				if ((DiskFile[0]==0x00) || (DiskFile[0]==0xff)) {	// a flag byte?
					if (DiskFile[4]*256+DiskFile[5] == pImg->nLoadAddr) {
						debug_write("Removing header from %s", pImg->szFileName);
						nRealLen-=6;
						memmove(DiskFile, &DiskFile[6], nRealLen);
					} 
				}
			} else {
				// regardless of the filetype, check for PC99 naming PHMxxxx.GRM, and remove its header
				if ((nRealLen > 6) && (strstr(pImg->szFileName,"PHM")) && (strstr(pImg->szFileName,".GRM"))) {
					if ((DiskFile[0]==0x00) || (DiskFile[0]==0xff)) {	// a flag byte?
						debug_write("PC99 filename? Removing header from %s", pImg->szFileName);
						nRealLen-=6;
						memmove(DiskFile, &DiskFile[6], nRealLen);
					}
				}
			}

			if (nLen < 1) {
				// fill in the loaded length
				pImg->nLength = nRealLen;
				nLen = nRealLen;
			} else if (nLen != nRealLen) {
				debug_write("Warning: size mismatch on %s - expected >%04x bytes but found >%04x", pImg->szFileName, pImg->nLength, nRealLen);
			}
			if (nLen > nRealLen) nLen=nRealLen;
			pData=(char*)DiskFile;
			sprintf(szFilename, "(%s) ", pImg->szFileName);
		}
	}

    // Don't return without freeing DiskFile

	if ((pImg->nType == TYPE_KEYS) || (pImg->nType == TYPE_OTHER) || (NULL != pData)) {
		// finally ;)
		debug_write("Loading file %sfrom %s: Type %c, Bank %d, Address 0x%04X, Length 0x%04X", szFilename,  pszFrom, pImg->nType, pImg->nBank, pImg->nLoadAddr, nLen);

		switch (pImg->nType) {
			case TYPE_GROM:
				if (pImg->nLoadAddr+nLen > 65536) {
					debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
					nLen=65536-pImg->nLoadAddr;
				}
				if (pImg->nBank > 0) {
					grombanking=1;
				}
				if (pImg->nBank == -1) {
					// copy to all banks
					for (int idx=0; idx<PCODEGROMBASE; idx++) {
						memcpy(&GROMBase[idx].GROM[pImg->nLoadAddr], pData, nLen);
					}
				} else {
					if ((pImg->nBank < PCODEGROMBASE) && (pImg->nBank >= 0)) {
						memcpy(&GROMBase[pImg->nBank].GROM[pImg->nLoadAddr], pData, nLen);
					} else {
						debug_write("Not loading to unsupported GROM bank %d", pImg->nBank);
					}
				}
				break;

			case TYPE_ROM:
				if ((pImg->nLoadAddr >= 0x6000) && (pImg->nLoadAddr <= 0x7fff)) {
					// cart ROM, load into the paged data
					if (pImg->nLoadAddr+nLen > 0x8000) {
						debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
						nLen=0x8000-pImg->nLoadAddr;
					}
                    // this is unlikely, but better safe than crashy
                    if (NULL == CPU2) {
                        CPU2=(Byte*)malloc(8192);
                        if (NULL == CPU2) {
                            // this will probably crash... maybe fail?
                            debug_write("Failed to allocate base RAM for cartridge, aborting load.");
                            break;
                        }
                        xb=0;
                        xbBank=0;
                    }
					// load into the first bank of paged memory (8k max)
					memcpy(&CPU2[pImg->nLoadAddr-0x6000], pData, nLen);
					// also load to main memory incase we aren't paging
					WriteMemoryBlock(pImg->nLoadAddr, pData, nLen);
					// and set up the ROM map
					memset(&ROMMAP[pImg->nLoadAddr], 1, nLen);
				} else {
					if (pImg->nLoadAddr < 0x6000) {
						if (pImg->nLoadAddr < 0x4000) {
							// non-DSR override
							if (pImg->nLoadAddr+nLen > 0x4000) {
								debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
								nLen=0x4000-pImg->nLoadAddr;
							}
						} else {
							// DSR override
							if (pImg->nLoadAddr+nLen > 0x6000) {
								debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
								nLen=0x6000-pImg->nLoadAddr;
							}
						}
					} else if (pImg->nLoadAddr+nLen > 0x10000) {
						debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
						nLen=65536-pImg->nLoadAddr;
					}
					WriteMemoryBlock(pImg->nLoadAddr, pData, nLen);
					memset(&ROMMAP[pImg->nLoadAddr], 1, nLen);
				}
				break;

			case TYPE_NVRAM:
				// for now, this is for cartridge space only, and only if not banked
				if ((pImg->nLoadAddr >= 0x6000) && (pImg->nLoadAddr <= 0x7fff)) {
					if (xb > 0) {
						debug_write("NVRAM only supported for non-paged carts, not loading.");
					} else {
						// cart ROM, load into the non-paged data
						if (pImg->nLoadAddr+nLen > 0x8000) {
							debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
							nLen=0x8000-pImg->nLoadAddr;
							// becase we save this back out, fix up the size internally
							pImg->nLength = nLen;
						}
                        // this is unlikely, but better safe than crashy
                        if (NULL == CPU2) {
                            CPU2=(Byte*)malloc(8192);
                            if (NULL == CPU2) {
                                debug_write("Failed to allocate base NVRAM for cartridge, aborting load.");
                                break;
                            }
                            xb=0;
                            xbBank=0;
                        }
						// load into the first bank of paged memory
						memcpy(&CPU2[pImg->nLoadAddr-0x6000], pData, nLen);
						// also load to main memory incase we aren't paging
						WriteMemoryBlock(pImg->nLoadAddr, pData, nLen);
						// it's RAM, so no memory map
					}
				} else {
					debug_write("NVRAM currently only supported for cartridge space, not loading to >%04X", pImg->nLoadAddr);
				}
				break;

			case TYPE_SPEECH:
				if (pImg->nLoadAddr+nLen > 65536) {
					debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
					nLen=65536-pImg->nLoadAddr;
				}
				memcpy(&SPEECH[pImg->nLoadAddr], pData, nLen);
				break;

			case TYPE_XB:
				if (pImg->nLoadAddr-0x6000+nLen > 8192) {
					debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
					nLen=8192-(pImg->nLoadAddr-0x6000);
				}
                // make sure we have enough space
                if ((NULL == CPU2)||(xb<1)) {
                    Byte *CNew = (Byte*)realloc(CPU2, 8192*2);
                    if (NULL == CNew) {
                        debug_write("Failed to reallocate RAM for XB cartridge, aborting load.");
                        break;
                    } else {
                        CPU2 = CNew;
                    }
                }
				// load it into the second bank of switched memory (2k in)
				memcpy(&CPU2[pImg->nLoadAddr-0x4000], pData, nLen);
				xb=1;		// one xb bank loaded
				xbBank=1;	// not guaranteed on real console
				bInvertedBanks=false;
				bUsesMBX = false;
				break;

			case TYPE_378:
			case TYPE_379:
			case TYPE_MBX:
            {
				// Non-inverted or Inverted XB style, but more than one bank! Up to 32MB mapped 8k at a time
				// not certain the intended maximum for MBX, but it has a slightly different layout
				// We still use the same loader here though.
				if (pImg->nLoadAddr+nLen > MAX_BANKSWITCH_SIZE) {
					debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
					nLen=MAX_BANKSWITCH_SIZE-pImg->nLoadAddr;
				}
				// update ROM map if in bank 0
				if (pImg->nLoadAddr < 0x2000) {
					memset(&ROMMAP[pImg->nLoadAddr+0x6000], 1, min(nLen, 0x2000-pImg->nLoadAddr));
				}

                int oldXB = xb;
				xb=(pImg->nLoadAddr+nLen+8191)/8192;	// round up, this many banks are loaded
				// now we need to make it a power of 2 for masking
				switch (xb) {
					case 0:			// should be impossible (result 0)
					case 1:			// 1 bank, no switching (result 0)
						xb=0;
						break;
					
					case 2:			// 2 banks uses 1 bit (result 1)
						xb=1;
						break;

					case 3:
					case 4:			// 2-4 banks uses 2 bits (result 3)
						xb=3;
						break;

					case 5:
					case 6:
					case 7:
					case 8:			// 5-8 banks uses 3 bits (result 7)
						xb=7;
						break;

					case 9:
					case 10:
					case 11:
					case 12:
					case 13:
					case 14:
					case 15:		
					case 16:		// 9-16 banks uses 4 bits (result 15);
						xb=15;
						break;

					default:
						// the ranges are getting kind of large for switch..case at this point
						if (xb<=32) {	// 17-32 banks uses 5 bits (result 31)
							xb=31;
						} else if (xb<=64) {	// 33-64 banks uses 6 bits (result 63)
							xb=63;
						} else if (xb<=128) {	// 65-128 banks are 7 bits (result 127)
							xb=127;
						} else if (xb<=256) {	// 129-256 banks are 8 bits (result 255)
							xb=255;
						} else if (xb<=512) {	// 257-512 banks are 9 bits (result 511)
							xb=511;
						} else if (xb<=1024) {	// 513-1024 banks are 10 bits (result 1023)
							xb=1023;
						} else if (xb<=2048) {	// 1025-2048 banks are 11 bits (result 2047)
							xb=2047;
						} else if (xb<=4096) {  // 2049-4096 banks are 12 bits (result 4095)
                            // maximum size for address latching
                            xb=4095;
                        } else if (xb<=8192) {  // 4097-8192 banks are 13 bits (result 8191)
							debug_write("Enable gigacart 64MB");
                            xb=8191;
                        } else if (xb<=16384) {  // 8193-16384 banks are 14 bits (result 16383)
							xb=16383;
							debug_write("Enable gigacart 128MB with 256 bytes GROM");
                        } else if (xb<=32768) {  // 16385-32768 banks are 15 bits (result 32767)
							debug_write("Enable gigacart 256MB");
                            xb=32767;
                        } else {  // 32769-65536 banks are 16 bits (result 65535)
							debug_write("Enable gigacart 512MB");
                            xb=65535;
						}
						break;
				}

                // make sure we have enough space
                if ((NULL == CPU2)||(oldXB<xb)) {
                    Byte *CNew=(Byte*)realloc(CPU2, 8192*(xb+1));
                    if (NULL == CNew) {
                        debug_write("Failed to reallocate RAM for cartridge, aborting load.");
                        xb=0;
                        xbBank=0;
                        break;
                    } else {
                        CPU2 = CNew;
                    }
                }
				memcpy(&CPU2[pImg->nLoadAddr], pData, nLen);
                xbBank=xb;	// not guaranteed on real console
				//xbBank=rand()%xb;		// TODO: make an option

                if (xb == 16383) {
                    // copy the GROM data into the GROM space
                    // copy to all banks - only 256 bytes, repeated over and over
					for (int idx=0; idx<PCODEGROMBASE; idx++) {
                        for (int adr=0x8000; adr<0xa000; adr+=256) {
                            // last 256 bytes of range
    						memcpy(&GROMBase[idx].GROM[adr], &CPU2[128*1024*1024-256], 256);
                        }
					}
                }
               
				// TYPE_378 is non-inverted, non MBX
				bInvertedBanks = false;
				bUsesMBX = false;
				if (pImg->nType == TYPE_379) {
					bInvertedBanks=true;
				} else if (pImg->nType == TYPE_MBX) {
					bUsesMBX=true;
				}
				debug_write("Loaded %d bytes, %sinverted, %sbank mask 0x%X", nLen, bInvertedBanks?"":"non-", bUsesMBX?"MBX, ":"", xb);
            }
				break;

			case TYPE_RAM:
				if (pImg->nLoadAddr+nLen > 65536) {
					debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
					nLen=65536-pImg->nLoadAddr;
				}
				//memcpy(&CPU[pImg->nLoadAddr], pData, nLen);
				WriteMemoryBlock(pImg->nLoadAddr, pData, nLen);
				break;

			case TYPE_VDP:
				if (pImg->nLoadAddr+nLen > 16384) {
					debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
					nLen=16384-pImg->nLoadAddr;
				}
				memcpy(&VDP[pImg->nLoadAddr], pData, nLen);
				break;

			case TYPE_DSR:	// always loads at >4000, the load address is the CRU base
				if (nLen > 8192) {
					debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
					nLen=8192;
				}
				// TODO: throw a debug warning if the address is not a valid CRU base
				memcpy(&DSR[(pImg->nLoadAddr>>8)&0x0f][0], pData, nLen);
				break;

			case TYPE_DSR2:	// always loads at >4000, the load address is the CRU base
				if (nLen > 8192) {
					debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
					nLen=8192;
				}
				// TODO: throw a debug warning if the address is not a valid CRU base
				memcpy(&DSR[(pImg->nLoadAddr>>8)&0x0f][0x2000], pData, nLen);
				break;

			case TYPE_PCODEG:
				if (pImg->nLoadAddr+nLen > 65536) {
					debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
					nLen=65536-pImg->nLoadAddr;
				}
				memcpy(&GROMBase[PCODEGROMBASE].GROM[pImg->nLoadAddr], pData, nLen);
				break;

			case TYPE_AMS:
				// Force the AMS card to on (reset later will init it)
				sams_enabled=1;
				sams_size=3;
				// decode the RLE encoded data into the AMS memory block
				// We use both words as the size value to get a 32-bit size
				RestoreAMS((unsigned char*)pData, (pImg->nLoadAddr<<16)|nLen);
				break;

			case TYPE_KEYS:
				// szFilename is the key presses but \n needs to be replaced with enter
				if (NULL != PasteString) {
					free(PasteString);
					PasteStringHackBuffer=false;
				}
				char *p;
				while (p = strstr(pImg->szFileName, "\\n")) {
					*p='\n';
					memmove(p+1, p+2, strlen(p+1));
				}
				PasteString=(char*)malloc(strlen(pImg->szFileName)+1);
                if (NULL != PasteString) {
    				strcpy(PasteString, pImg->szFileName);
	    			PasteCount=-1;		// give it time to come up!
		    		PasteIndex=PasteString;
                } else {
                    debug_write("Paste string failed to allocate memory");
                }
				break;

			case TYPE_MPD:
				// My own hacky MPD GROM hack - up to 128k with special internal banking rules
				if (pImg->nLoadAddr+nLen > 144*1024) {
					debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
					nLen=144*1024-pImg->nLoadAddr;
				}
				memcpy(&mpdGrom[pImg->nLoadAddr], pData, nLen);
				bMpdActive=true;
				debug_write("Loaded %d bytes", nLen);
				debug_write("WARNING: MPD ACTIVE. 99/4A only! Console GROMs overridden!");
				break;

			case TYPE_UBER_GROM:
				// my hacky little Uber GROM simulation
				if (pImg->nLoadAddr+nLen > 120*1024) {
					debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
					nLen=120*1024-pImg->nLoadAddr;
				}
				memcpy(&UberGROM[pImg->nLoadAddr], pData, nLen);
				bUberGROMActive=true;
				grombanking=1;
				debug_write("Loaded %d bytes", nLen);
				debug_write("WARNING: Uber-GROM ACTIVE");
				break;

			case TYPE_UBER_EEPROM:
				if (pImg->nLoadAddr+nLen > 4*1024) {
					debug_write("%s overwrites memory block - truncating.", pImg->szFileName);
					nLen=4*1024-pImg->nLoadAddr;
				}
				memcpy(&UberEEPROM[pImg->nLoadAddr], pData, nLen);
				debug_write("Loaded %d bytes", nLen);
				break;

			case TYPE_OTHER:
				// loads from another Classic99 ROM group - most useful for loading
				// built-in ROMs. LoadAddr is the group, and nLength is the index.
				{
					struct CARTS *pBank = NULL;
					switch (pImg->nLoadAddr) {
						case 0:	pBank=Apps; break;
						case 1: pBank=Games; break;
						case 2: pBank=Users; break;
					}
					if (NULL == pBank) {
						debug_write("Invalid bank index %d in 'Other' cart group", pImg->nLoadAddr);
					} else {
						if (pImg->nLength >= MAXROMSPERCART) {
							debug_write("Invalid cart index %d in 'Other' cart group", pImg->nLength);
						} else {
							pCurrentHelpMsg=pBank[pImg->nLength].szMessage;
							for (int idx=0; idx<MAXROMSPERCART; idx++) {
								LoadOneImg(&pBank[pImg->nLength].Img[idx], "ROMS");
							}
							pMagicDisk=pBank[pImg->nLength].pDisk;
						}
					}
				}
				break;

			default:
				break;
		}
	}
	
	// WIN32 does not require (or even permit!) us to unlock and release these objects
    // But we do need to free DiskFile if set
    if (NULL != DiskFile) {
        free(DiskFile);
    }
}

void readroms() { 
	int idx;

    // reset the hardware read/write pointers to RAM
    for (idx = 0; idx < 64*1024; ++idx) {
        readFunctions[idx] = readZero;
        writeFunctions[idx] = writeZero;
    }

#ifdef USE_BIG_ARRAY
	// TODO HACK
	{
		FILE *fp;
		if (strlen(g_cmdLine) > 0) {
			char *p=g_cmdLine;
			// remove quotes
			while (*p) {
				if (*p == '\"') {
					strcpy(p, p+1);
				} else {
					p++;
				}
			}
			debug_write("Reading hack %s...", g_cmdLine);
			fp = fopen(g_cmdLine, "rb");
			if (NULL == fp) {
				debug_write("Failed to open file, code %d", errno);
				BIGARRAYSIZE = 0;
			} else {
				fseek(fp, 0, SEEK_END);
				int x = ftell(fp);
				fseek(fp, 0, SEEK_SET);
				BIGARRAY = (unsigned char*)malloc(x);
				if (BIGARRAY == NULL) {
					debug_write("Failed to allocate memory for BIG ARRARY HACK");
					BIGARRAYSIZE = 0;
				} else {
					BIGARRAYSIZE = fread(BIGARRAY, 1, x, fp);
					debug_write("Read %d bytes", BIGARRAYSIZE);

                    debug_write("Copying hack to AMS...");
                    PreloadAMS(BIGARRAY, BIGARRAYSIZE);
				}
				fclose(fp);
			}
		} else {
			BIGARRAYSIZE = 0;
		}
		BIGARRAYADD=0;
	}
	// end hack
#endif

	// save any previous NVRAM
	saveroms();

	// process the dump list and remove any disk files
	CloseDumpFiles();

	// process the breakpoint list and re-open any listed files
	ReloadDumpFiles();

    // make sure there's at least a little memory at CPU2, since we are building on very old code
    // the xb mask must now always be associated with CPU2
    if (NULL == CPU2) {
        CPU2=(Byte*)malloc(8192);
        if (NULL == CPU2) {
            fail("Failed to allocate initial RAM for cartridge");
        }
        xb=0;
        xbBank=0;
    }

	// now memory
	memset(ROMMAP, 0, 65536);	// this is not RAM
	if (!bWarmBoot) {
		memrnd(CPU2, 8192*(xb+1));
		memrnd(VDP, 16384);
	}
	memset(DSR, 0, 16*16384);	// not normally RAM
	memset(key, 0, 256);		// keyboard

	pMagicDisk=NULL;
	pCurrentHelpMsg=NULL;
	bMpdActive = false;			// no MPD unless we load it!
	bUberGROMActive = false;	// same with UberGROM

	for (idx=0; idx<=PCODEGROMBASE; idx++) {
		// not normally RAM
		memset(GROMBase[idx].GROM, 0, sizeof(GROMBase[idx].GROM));
	}

	// Disable the PCode card (at >1F00)
	DSR[0xF][0]=0;
	xb=0;									// no XB bank loaded
	grombanking=0;							// not using grom banking
	nCurrentDSR=-1;							// no DSR paged in
	memset(nDSRBank, 0, sizeof(nDSRBank));	// not on second page of DSR

	// load the always load files
	for (idx=0; idx<sizeof(AlwaysLoad)/sizeof(IMG); idx++) {
		LoadOneImg(&AlwaysLoad[idx], "ROMS");
	}

    // reconfigure for CF7 if necessary
    if (csCf7Bios.GetLength() > 0) {
        FILE *fp = fopen(csCf7Bios, "rb");
        if (NULL != fp) {
//            DSR[1][0] = 0x00;       // disable Classic99 DSR - not positive how important that is...
            debug_write("Replacing DSK1-3 with CF7 emulation at CRU >1000.");
            fread(DSR[0], 1, 8192, fp);
            fclose(fp);
            // make sure DSK 1-3 is a cf7Disk object, which does nothing
            for (int idx = 1; idx < 4; ++idx) {
                if (NULL != pDriveType[idx]) {
                    free(pDriveType[idx]);
                }
                pDriveType[idx] = new Cf7Disk;
            }
        } else {
            debug_write("Failed to read CF7 BIOS '%s'", csCf7Bios.GetString());
        }
    }

	// load the appropriate system ROMs - each system is a cart structure that contains MAXROMSPERCART ROMs
	for (idx=0; idx<MAXROMSPERCART; idx++) {
		LoadOneImg(&Systems[nSystem].Img[idx], "ROMS");
	}

	// if there is a cart plugged in, load that too
	if (nCart != -1) {
		struct CARTS *pBank=NULL;

 		switch (nCartGroup) {
		case 0:	
			if (nCart < 100) {
				pBank=Apps; 
			}
			break;
		case 1: 
			if (nCart < 100) {
				pBank=Games; 
			}
			break;
		case 2: 
			if (nCart < nTotalUserCarts) {
				pBank=Users; 
			}
			break;
		}

		if (pBank) {
			pCurrentHelpMsg=pBank[nCart].szMessage;
			for (idx=0; idx<MAXROMSPERCART; idx++) {
				LoadOneImg(&pBank[nCart].Img[idx], "ROMS");
			}
			pMagicDisk=pBank[nCart].pDisk;
		}
	}

	// check for a ROM open request on the command line
	// we then nuke the command line so it doesn't get stuck
	// in a loop re-processing it on every reset (which is part
	// of the cart load)
	{
		FILE *fp;
		if (strlen(g_cmdLine) > 0) {
			char *p=g_cmdLine;
			// remove quotes
			while (*p) {
				if (*p == '\"') {
					strcpy(p, p+1);
				} else {
					p++;
				}
			}
			// note that since quotes are nuked (even earlier than
			// here with the big file hack), we can't currently
			// support multiple commands, so "-rom" is the only
			// valid input, and the rest is a filename
			if (0 == strncmp(g_cmdLine, "-rom", 4)) {
				p = g_cmdLine+5;
				PostMessage(myWnd, WM_COMMAND, ID_CART_USER_OPEN, (LPARAM)p);
				// don't process it again
				g_cmdLine[0] = '\0';
			}
		}
	}
}

void saveroms()
{
	// if there is a cart plugged in, see if there is any NVRAM to save
	if (nCart != -1) {
		struct CARTS *pBank=NULL;

 		switch (nCartGroup) {
		case 0:	
			if (nCart < 100) {
				pBank=Apps; 
			}
			break;
		case 1: 
			if (nCart < 100) {
				pBank=Games; 
			}
			break;
		case 2: 
			if (nCart < nTotalUserCarts) {
				pBank=Users; 
			}
			break;
		}

		if ((pBank)&&(xb==0)&&(nvRamUpdated)) {
			for (int idx=0; idx<MAXROMSPERCART; idx++) {
				if (pBank[nCart].Img[idx].nType == TYPE_NVRAM) {
					// presumably all the data is correct, length and so on
					FILE *fp = fopen(pBank[nCart].Img[idx].szFileName, "wb");
					if (NULL == fp) {
						debug_write("Failed to write NVRAM file '%s', code %d", pBank[nCart].Img[idx].szFileName, errno);
					} else {
						char buf[8192];
						debug_write("Saving NVRAM file '%s', addr >%04X, len >%04X", pBank[nCart].Img[idx].szFileName, pBank[nCart].Img[idx].nLoadAddr, pBank[nCart].Img[idx].nLength);
						ReadMemoryBlock(pBank[nCart].Img[idx].nLoadAddr, buf, min(sizeof(buf), pBank[nCart].Img[idx].nLength));
						fwrite(buf, 1, pBank[nCart].Img[idx].nLength, fp);
						fclose(fp);
					}
				}
			}
		}
	}

	// reset the value
	nvRamUpdated = false;
}

//////////////////////////////////////////////////////////
// Interpret a single instruction
// Note: op_X doesn't come here anymore as this function
// is too busy for the recursive call.
//////////////////////////////////////////////////////////
void do1()
{
	// used for emulating idle and halts (!READY) better
	bool nopFrame = false;

	// handle end of frame processing (some emulator, some hardware)
	if (end_of_frame)
	{
		pCurrentCPU->ResetCycleCount();

		// put this before setting the draw event to reduce conflict, though it makes it a frame behind
		if (Recording) {
			WriteFrame();
		}

		int nNumFrames = retrace_count / (drawspeed+1);	// get count so we can update counters (ignore remainder)
		if (fJoystickActiveOnKeys > 0) {
			fJoystickActiveOnKeys -= nNumFrames;
			if (fJoystickActiveOnKeys < 0) {
				fJoystickActiveOnKeys = 0;
			}
		}
		cpuframes+=nNumFrames;
		retrace_count-=nNumFrames*(drawspeed+1);
		timercount+=nNumFrames*(drawspeed+1);

		end_of_frame=0;								// No matter what, this tick is passed!
	}

	if (pCurrentCPU == pCPU) {
		// Somewhat better 9901 timing - getting close! (Actually this looks pretty good now)
		// The 9901 timer decrements every 64 periods, with the TI having a 3MHz clock speed
		// Thus, it decrements 46875 times per second. If CRU bit 3 is on, we trigger an
		// interrupt when it expires. 
		if (timer9901>0) {
			int nTimerCnt=CRUTimerTicks>>6;		// /64
			if (nTimerCnt) {
				timer9901-=nTimerCnt;
				CRUTimerTicks-=(nTimerCnt<<6);	// *64

				if (timer9901 < 1) {
					timer9901=starttimer9901+timer9901;
// 					debug_write("9901 timer expired, int requested");
					timer9901IntReq=1;	
				}
			}
		}

		// Check if the VDP or CRU wants an interrupt (99/4A has only level 1 interrupts)
		// When we have peripheral card interrupts, they are masked on CRU[1]
		if ((((VDPINT)&&(CRU[2]))||((timer9901IntReq)&&(CRU[3]))) && ((pCurrentCPU->GetST()&0x000f) >= 1) && (!skip_interrupt)) {
//			if (cycles_left >= 22) {					// speed throttling
				pCurrentCPU->TriggerInterrupt(0x0004);
//			}
            // the if cycles_left doesn't work because if we don't take it now, we'll
            // execute other instructions (at least one more!) instead of stopping...
            // so we'll just take it and suffer later.
		}

		// If an idle is set
		if ((pCurrentCPU->GetIdle())||(pCurrentCPU->GetHalt())) {
			nopFrame = true;
		}
	}

    // some shortcut keys that are always active...
    // these all require control to be active
    // launch debug dialog (with control)
	if (key[VK_HOME]) 
	{
		if (GetAsyncKeyState(VK_CONTROL)&0x8000) {
		    if (NULL == dbgWnd) {
			    PostMessage(myWnd, WM_COMMAND, ID_EDIT_DEBUGGER, 0);
			    // the dialog focus switch may cause a loss of the up event, so just fake it now
			    decode(0xe0);	// extended key
			    decode(0xf0);
			    decode(VK_HOME);
		    }
    		key[VK_HOME]=0;
        }
	}

	// edit->paste (with control)
	if (key[VK_F1]) {
    	if (GetAsyncKeyState(VK_CONTROL)&0x8000) {
            PostMessage(myWnd, WM_COMMAND, ID_EDITPASTE, 0);
            key[VK_F1] = 0;
        }
    }

    // copy screen to clipboard (with control)
	if (key[VK_F2]) {
        // we'll try to use the screen offset byte - 0x83d3
        // we'll explicitly check for only 0x60, otherwise 0
    	if (GetAsyncKeyState(VK_CONTROL)&0x8000) {
            PostMessage(myWnd, WM_COMMAND, ID_EDIT_COPYSCREEN, 0);
            key[VK_F2]=0;
        }
	}

	// Control keys - active only with the debug view open in PS/2 mode
	// nopFrame must be set before now!
	if ((!gDisableDebugKeys) && (NULL != dbgWnd) && (!nopFrame)) {
		// breakpoint handling
		if ((max_cpf > 0) && (nStepCount == 0)) {
			// used for timing
			static unsigned long nFirst=0;
			static unsigned long nMax=0, nMin=0xffffffff;
			static int nCount=0;
			static unsigned long nTotal=0;
			Word PC = pCurrentCPU->GetPC();

			for (int idx=0; idx<nBreakPoints; idx++) {
				switch (BreakPoints[idx].Type) {
					case BREAK_PC:
						if (CheckRange(idx, PC)) {
							TriggerBreakPoint();
						}
						break;

					// timing instead of breakpoints
					case BREAK_RUN_TIMER:
						if ((BreakPoints[idx].Bank != -1) && (xbBank != BreakPoints[idx].Bank)) {
							break;
						}
						if (PC == BreakPoints[idx].A) {
							nFirst=total_cycles;
						} else if (PC == BreakPoints[idx].B) {
							if (nFirst!=0) {
								if (total_cycles<nFirst) {
									debug_write("Counter Wrapped, no statistics");
								} else {
									unsigned long nTime=total_cycles-nFirst;
									if (nTime > 0) {
										if (nTime>nMax) nMax=nTime;
										if (nTime<nMin) nMin=nTime;
										nCount++;
										nTotal+=nTime;
										if (nTotal<nTime) {
											nTotal=0;
											nCount=0;
										} else {
											debug_write("Timer: %u CPU cycles - Min: %u  Max: %u  Average(%u): %u", nTime, nMin, nMax, nCount, nTotal/nCount);
										}
									}
								}
								nFirst=0;
							}
						}
						break;
				}
			}

			// check for the step over
			if (bStepOver) {
				// if return address wasn't set by the last instruction, just stop
				// otherwise, we have to wait for it. The address +2 covers the case
				// of a subroutine that takes a single data operand as arguments, so modifies
				// the return address.
				if ((pCurrentCPU->GetReturnAddress() == 0) || (PC == pCurrentCPU->GetReturnAddress()) || (PC == pCurrentCPU->GetReturnAddress()+2)) {
					bStepOver=false;
					TriggerBreakPoint();
				}
			}
		}

		// pause/play
		if (key[VK_F1]) {
            // in the case where the current CPU can not break but the other can, just
            // hold onto this key...
            if (!pCurrentCPU->enableDebug) {
                if ((pCPU->enableDebug)||(pGPU->enableDebug)) {
                    // just wait for the context switch
                    return;
                } else {
                    // no debugging, so discard the key
                    debug_write("Neither processor is enabled to breakpoint!");
        			key[VK_F1]=0;
                    return;
                }
            }

			if (0 == max_cpf) {
				// already paused, restore
				DoPlay();
			} else {
				// running normal, so pause
				DoPause();
			}
			key[VK_F1]=0;
		}
		
		// step
		if (key[VK_F2]) {
			DoStep();
			key[VK_F2]=0;
		}

		// step over
		if (key[VK_F3]) {
			DoStepOver();
			key[VK_F3]=0;
		}

		// do automatic screenshot (not filtered), or set CPU Normal (ctrl)
		if (key[VK_F5]) {
			key[VK_F5]=0;

			if (GetAsyncKeyState(VK_CONTROL)&0x8000) {
				SendMessage(myWnd, WM_COMMAND, ID_CPUTHROTTLING_NORMAL, 0);
			} else {
				SaveScreenshot(true, false);
			}
		}

		// auto screenshot, filtered, or CPU Overdrive (ctrl)
		if (key[VK_F6]) {
			key[VK_F6]=0;

			if (GetAsyncKeyState(VK_CONTROL)&0x8000) {
				SendMessage(myWnd, WM_COMMAND, ID_CPUTHROTTLING_CPUOVERDRIVE, 0);
			} else {
				SaveScreenshot(true, true);
			}
		}

		// toggle sprites / system maximum (ctrl)
		if (key[VK_F7]) {
			key[VK_F7]=0;

			if (GetAsyncKeyState(VK_CONTROL)&0x8000) {
				SendMessage(myWnd, WM_COMMAND, ID_CPUTHROTTLING_SYSTEMMAXIMUM, 0);
			} else {
				SendMessage(myWnd, WM_COMMAND, ID_LAYERS_DISABLESPRITES, 0);
			}
		}

		// toggle background / cpu slow (ctrl)
		if (key[VK_F8]) {
			key[VK_F8]=0;

			if (GetAsyncKeyState(VK_CONTROL)&0x8000) {
				SendMessage(myWnd, WM_COMMAND, ID_CPUTHROTTLING_CPUSLOW, 0);
			} else {
				SendMessage(myWnd, WM_COMMAND, ID_LAYERS_DISABLEBACKGROUND, 0);
			}
		}

		// tell VDP to draw character set
		if (key[VK_F9]) {
			VDPDebug=!VDPDebug;
			key[VK_F9]=0;
			redraw_needed=REDRAW_LINES;
		}

		// dump main memory
		if (key[VK_F10]) {
			key[VK_F10]=0;
			DoMemoryDump();
		}

		// toggle turbo
		if (key[VK_F11]) {
			key[VK_F11]=0;

			if (CPUThrottle==CPU_MAXIMUM) {
				// running in fast forward, return to normal
				DoPlay();
			} else {
				DoFastForward();
			}
		}									
		// LOAD interrupt or RESET (ctrl)
		if (key[VK_F12]) {
			key[VK_F12]=0;
			
			if (GetAsyncKeyState(VK_CONTROL)&0x8000) {
				SendMessage(myWnd, WM_COMMAND, ID_FILE_RESET, 0);
			} else {
				DoLoadInterrupt();
			}
		}
	}

	bool bOperate = true;
	if (cycles_left <= 0) bOperate=false;
	// force run of the other CPU if we're stepping
	if ((cycles_left == 1) && (!pCurrentCPU->enableDebug)) bOperate=true;

	if (bOperate) {
		cpucount++;					// increment instruction counter (TODO: just remove, pointless today)
 
		////////////////// System Patches - not active for GPU

		if (pCurrentCPU == pCPU) {
			// Keyboard hacks - requires original 99/4A ROMS!
			// Now we can check for all three systems - we look for the
			// address where KSCAN stores the character at >8375
			// longer buffer hack is enabled ONLY for system 1 (TI-99/4A)
			if ( ((nSystem == 0) && (pCurrentCPU->GetPC()==0x356)) ||
			 ((nSystem == 1) && (pCurrentCPU->GetPC()==0x478)) ||
			 ((nSystem == 2) && (pCurrentCPU->GetPC()==0x478)) ) {
				if (NULL != PasteString) {
					static int nOldSpeed = -1;

					if (nOldSpeed == -1) {
						// set overdrive during pasting, then go back to normal
						nOldSpeed = CPUThrottle;
						SendMessage(myWnd, WM_COMMAND, ID_CPUTHROTTLING_CPUOVERDRIVE, 0);
					}

					if (PasteString==PasteIndex) SetWindowText(myWnd, "Classic99 - Pasting (ESC Cancels)");	

					if (key[VK_ESCAPE]) {
						// the rest of the cleanup will happen below
						*PasteIndex='\0';
					}

					if ((rcpubyte(0x8374)==0)||(rcpubyte(0x8374)==5)) {		// Check for pastestring - note keyboard is still active
						if (*PasteIndex) {
							if (*PasteIndex==10) {
								// CRLF to CR, LF to CR
								if ((PasteIndex==PasteString)||(*(PasteIndex-1)!=13)) {
									*PasteIndex=13;
								}
							}

							if (PasteCount==0) {
								if ((*PasteIndex>31)||(*PasteIndex==13)) {
									if (nSystem == 0) {
										// TI-99/4A code is different - it expects to get the character
										// from GROM, so we need to hack 8375 after it's written
										Word WP = pCurrentCPU->GetWP();
										wcpubyte(0x8375, toupper(*PasteIndex));
										wcpubyte(WP, rcpubyte(0x837c) | 0x20);	/* R0 must contain the status byte */
									} else {
										if ((PasteStringHackBuffer)&&(nSystem==1)) {	// for normal TI-99/4A only - need to verify for 2.2
											wcpubyte(0x835F, 0x5d);		// max length for BASIC continuously set - infinite string length! Use with care!
										}
										Word WP = pCurrentCPU->GetWP();
										wcpubyte(WP, *PasteIndex);					/* set R0 (byte) with keycode */
										wcpubyte(WP+12, rcpubyte(0x837c) | 0x20);	/* R6 must contain the status byte (it gets overwritten) */
									}
								}
								if (PasteCount<1) {
									wcpubyte(0x837c, rcpubyte(0x837c)|0x20);
								}
								PasteCount++;
								PasteIndex++;
							} else {
								// no response - key up!
								PasteCount=0;
							}
						}
					
						if ('\0' == *PasteIndex) {
							PasteIndex=NULL;
							free(PasteString);
							PasteString=NULL;
							PasteStringHackBuffer=false;
							SetWindowText(myWnd, szDefaultWindowText);

							switch (nOldSpeed) {
								default:
								case CPU_NORMAL:
									SendMessage(myWnd, WM_COMMAND, ID_CPUTHROTTLING_NORMAL, 0);
									break;
								case CPU_OVERDRIVE:
									SendMessage(myWnd, WM_COMMAND, ID_CPUTHROTTLING_CPUOVERDRIVE, 0);
									break;
								case CPU_MAXIMUM:
									SendMessage(myWnd, WM_COMMAND, ID_CPUTHROTTLING_SYSTEMMAXIMUM, 0);
									break;
							}
							nOldSpeed = -1;
						}
					}
				}
			}

			// intercept patches for Classic99 DSK1 emulation
			// These patches handle the custom DSR:
			// >4800 - DSK or clipboard file access
			// >4810 - DSK powerup
			// >4820->482C - SBRLNK - Special subprograms
			// NOTE: Classic99 DSR must not use 0x5FF0-0x5FFF - used for TI disk controller hardware
			if ((nCurrentDSR == 1) && (nDSRBank[1] == 0) && (pCurrentCPU->GetPC() >= 0x4800) && (pCurrentCPU->GetPC() <= 0x5FEF)) {
				Word WP = pCurrentCPU->GetWP();
				bool bRet = HandleDisk();
				// the disk system may have switched in the TI disk controller, in which case we
				// will actually execute code instead of faking in. So in that case, don't return!
				if (nDSRBank[1] == 0) {
					if (bRet) {
						// if all goes well, increment address by 2
						// Note the powerup routine won't do that :)
						// return address in R11.. TI is a bit silly
						// and if we don't increment by 2, it's considered
						// an error condition
						wrword(WP+22, romword(WP+22)+2);
					}
					pCurrentCPU->SetPC(romword(WP+22));
				}
				return;
			}
			// turn off TI disk DSR if no longer active
			if ((nDSRBank[1] > 0) && ((nCurrentDSR != 1)||(pCurrentCPU->GetPC() < 0x4000))) nDSRBank[1] = 0;
			// check for sector access hook
			if ((nCurrentDSR == 1) && (nDSRBank[1] > 0) && (pCurrentCPU->GetPC() == 0x40e8)) {
				HandleTICCSector();
			}
		}

		if ((!nopFrame) && ((!bStepOver) || (nStepCount))) {
			if (pCurrentCPU->enableDebug) {
				// Update the disassembly trace
				memmove(&Disasm[0], &Disasm[1], 19*sizeof(Disasm[0]));	// TODO: really should be a ring buffer
				Disasm[19].pc=pCurrentCPU->GetPC();
				if (pCurrentCPU == pGPU) {
					Disasm[19].bank = -1;
				} else {
					Disasm[19].bank = xbBank;
				}
			}
			// will fill in cycles below
		}

#if 0
		// disasm running log
		static FILE *fLog=NULL;
		if ((NULL == fLog)&&(pCurrentCPU == pCPU)&&(pCurrentCPU->GetPC() == 0x601c)) {
			fLog=fopen("c:\\new\\RUNTRACE.TXT", "w");
		}
		if (NULL != fLog) {
			char buf[1024];
			sprintf(buf, "%04X ", pCurrentCPU->GetPC());
			Dasm9900(&buf[5], pCurrentCPU->GetPC(), xbBank);
			fprintf(fLog, "(%d) %s\n", xbBank, buf);

			if ((pCurrentCPU == pCPU)&&(pCurrentCPU->GetPC() == 0xffff)) {
				fclose(fLog);
				ExitProcess(0);
			}
		}
#endif

		// TODO: is this true? Is the LOAD interrupt disabled when the READY line is blocked?
		if ((pCurrentCPU == pCPU) && (!nopFrame)) {
			// this is a bad pattern, this repeated pCurrentCPU == pCPU, we can clean this up a lot
			if ((doLoadInt)&&(!skip_interrupt)) {
				pCurrentCPU->TriggerInterrupt(0xfffc);
				doLoadInt=false;
				// load interrupt also releases IDLE
				pCurrentCPU->StopIdle();
			}
		}

		Word in = pCurrentCPU->ExecuteOpcode(nopFrame);
		if (pCurrentCPU == pCPU) {
            updateTape(pCurrentCPU->GetCycleCount());
			updateDACBuffer(pCurrentCPU->GetCycleCount());
			// an instruction has executed, interrupts are again enabled
			skip_interrupt=0;
			// update VDP too
			updateVDP(pCurrentCPU->GetCycleCount());

			// and check for VDP address race
			if (vdpwroteaddress > 0) {
				vdpwroteaddress -= pCurrentCPU->GetCycleCount();
				if (vdpwroteaddress < 0) vdpwroteaddress=0;
			}

			// some debug help for DSR overruns
			// TODO: this is probably not quite right (AMS?), but should work for now
			int top = (staticCPU[0x8370] << 8) + staticCPU[0x8371];
			if (top != filesTopOfVram) {
				if ((pCurrentCPU->GetPC() < 0x4000) || (pCurrentCPU->GetPC() > 0x5FFF)) {
					// top of VRAM changed, not in a DSR, so write a warning
					// else this is in a DSR, so we'll assume it's legit
					debug_write("(Non-DSR) Top of VRAM pointer at >8370 changed to >%04X by PC >%04X", top, pCurrentCPU->GetPC());
				}
				updateCallFiles(top);
			}
		}

		if ((!nopFrame) && ((!bStepOver) || (nStepCount))) {
			if (pCurrentCPU->enableDebug) {
				Disasm[19].cycles = pCurrentCPU->GetCycleCount();
			}
		}

		if ((!nopFrame) && (nStepCount > 0)) {
			nStepCount--;
		}

		if (pCurrentCPU == pCPU) {
			// Slow down autorepeat using a timer - requires 99/4A GROM
			// We check if the opcode was "MOVB 2,*3+", (PC >025e, but we don't assume that) 
			// the 99/4A keyboard is on, and the GROM Address
			// has been incremented to the next instruction (IE: we just incremented the
			// repeat counter). If so, we only allow the increment at a much slower rate
			// based on the interrupt timer (for real time slowdown).
			// This doesn't work in XB!
			if ((CPUThrottle!=CPU_NORMAL) && (slowdown_keyboard) && (in == 0xdcc2) && ((keyboard==KEY_994A)||(keyboard==KEY_994A_PS2)) && (GROMBase[0].GRMADD == 0x2a62)) {
				if ((ticks%10) != 0) {
					WriteMemoryByte(0x830D, ReadMemoryByte(0x830D) - 1, false);
				}
			}
			// but this one does (note it will trigger for ANY bank-switched cartridge that uses this code at this address...)
			if ((CPUThrottle!=CPU_NORMAL) && (slowdown_keyboard) && (in == 0xdcc2) && ((keyboard==KEY_994A)||(keyboard==KEY_994A_PS2)) && (GROMBase[0].GRMADD == 0x6AB6) && (xb)) {
				if ((ticks%10) != 0) {
					WriteMemoryByte(0x8300, ReadMemoryByte(0x8300) - 1, false);
				}
			}
		}

		if (pCurrentCPU == pCPU) {
			int nLocalCycleCount = pCurrentCPU->GetCycleCount();
			InterlockedExchangeAdd((LONG*)&cycles_left, -nLocalCycleCount);
			unsigned long old=total_cycles;
			InterlockedExchangeAdd((LONG*)&total_cycles, nLocalCycleCount);
			if ((old&0x80000000)&&(!(total_cycles&0x80000000))) {
				total_cycles_looped=true;
				speech_cycles=total_cycles;
			} else {
				// check speech
				if ((max_cpf > 0) && (SPEECHUPDATECOUNT > 0)) {
					if (total_cycles - speech_cycles >= (unsigned)SPEECHUPDATECOUNT) {
						while (total_cycles - speech_cycles >= (unsigned)SPEECHUPDATECOUNT) {
							static int nCnt=0;
							int nSamples=26;
							// should only be once
							// now we're expecting 1/300th of a second, which at 8khz
							// is 26.6 samples
							nCnt++;
							if (nCnt > 2) {	// handle 2/3
								nCnt=0;
							} else {
								nSamples++;
							} 
							SpeechUpdate(nSamples);
							speech_cycles+=SPEECHUPDATECOUNT;
						}
					}
				}
			}

			CRUTimerTicks+=nLocalCycleCount;

			// see if we can resolve a halt condition
			int bits = pCurrentCPU->GetHalt();
			if (bits) {
				// speech is blocking
				if (bits & (1<<HALT_SPEECH)) {
					// this will automatically unlock it if it is accepted
					// 0x9400 is the speech base address
					wspeechbyte(0x9400, CPUSpeechHaltByte);
				}

				// that's all we have
			}
		}

		pCurrentCPU->ResetCycleCount();

		if ((!nopFrame) && (bDebugAfterStep)) {
			bDebugAfterStep=false;
			draw_debug();
			redraw_needed = REDRAW_LINES;
		}
	} else {
		// Go to sleep till it's time to work again, timeout wait after 50ms
		// (so if we're very slow or the event dies, we keep running anyway)
		if (bDebugAfterStep) {
			bDebugAfterStep=false;
			draw_debug();
			redraw_needed = REDRAW_LINES;
		}

		WaitForSingleObject(hWakeupEvent, 50);
	}
}

// "legally" update call files from the DSR
void updateCallFiles(int newTop) {
	filesTopOfVram = newTop;
}

// verify the top of VRAM pointer so we can warn about disk problems
// this is called when a DSR is executed
void verifyCallFiles() {
	int nTop = (staticCPU[0x8370]<<8) | staticCPU[0x8371];

	if (nTop > 0x3be3) {
		// there's not enough memory for even 1 disk buffer, so never mind
		return;
	}

	// validate the header for disk buffers - P-Code Card crashes with Stack Overflow without these
	// user programs can have this issue too, of course, if they use the TI CC
	if ((VDP[nTop+1] != 0xaa) ||	// valid header
		(VDP[nTop+2] != 0x3f) ||	// top of RAM, MSB (not validating LSB since CF7 can change it)**
		(VDP[nTop+4] != 0x11) ||	// CRU of disk controller (TODO: we assume >1100 today)
		(VDP[nTop+5] > 9)) {		// number of files, we support 0-9
									// ** - actually the previous value of highest free address from 8370
			char buf[256];
			sprintf(buf, "Invalid file buffer header at >%04X. Bytes >%02X,>%02X,>%02X,>%02X,>%02X",
				nTop, VDP[nTop+1], VDP[nTop+2], VDP[nTop+3], VDP[nTop+4], VDP[nTop+5]);
			debug_write(buf);
			if (BreakOnDiskCorrupt) {
				TriggerBreakPoint();
				MessageBox(myWnd, "Disk access is about to crash - the emulator has been paused. Check debug log for details.", "Classic99 Error", MB_OK);
			}
	}
}

// Zero handlers for unconfigured addresses
Byte readZero(Word /*addr*/, bool /*rmw*/) {
    return 0;
}
void writeZero(Word /*addr*/, Byte /*data*/) {
    // do nothing
}

//////////////////////////////////////////////////////
// Read a single byte from CPU memory
//////////////////////////////////////////////////////
Byte rcpubyte(Word x,bool rmw) {
	// TI CPU memory map
	// >0000 - >1fff  Console ROM
	// >2000 - >3fff  Low bank RAM
	// >4000 - >5fff  DSR ROMs
	// >6000 - >7fff  Cartridge expansion
	// >8000 - >9fff  Memory-mapped devices & CPU scratchpad
	// >a000 - >ffff  High bank RAM
	// 
	// All is fine and dandy, except the memory-mapped devices, and the
	// fact that writing to the cartridge port with XB/379 in place causes
	// a bank switch. In this emulator, that will only happen if 'xb'
	// is greater than 0. A custom DSR will be written to be loaded which
	// will appear to the "TI" to support all valid devices, and it will
	// be loaded into the DSR space.

    // TODO HACK FOR PCODE RELOCATE
//    if ((x<0x6000)&&(x>0x3fff)&&(nCurrentDSR==-1)) {
//        TriggerBreakPoint();
//    }

	// no matter what kind of access, update the heat map
	UpdateHeatmap(x);

	if (!rmw) {
		// Check for read or access breakpoints
		for (int idx=0; idx<nBreakPoints; idx++) {
			switch (BreakPoints[idx].Type) {
				case BREAK_ACCESS:
				case BREAK_READ:
					if (CheckRange(idx, x)) {
						TriggerBreakPoint();
					}
					break;
			}
		}

		// the cycle timing for read-before-write is dealt with in the opcodes
        // the only zero-wait-state memory is >0000 through >1FFF, and >8000 through >83FF
        // handling the wait state generator here rather than in the device handlers
		if ((x & 0x01) == 0) {					// this is a wait state (we cancel it below for ROM and scratchpad)
            if ((x > 0x2000) && ((x < 0x8000) || (x >= 0x8400))) {
    			pCurrentCPU->AddCycleCount(4);		// we can't do half of a wait, so just do it for the even addresses. This should
	    											// be right now that the CPU emulation does all Word accesses
            }
		}
	}

    // pass the read to the handler - it's never allowed to be NULL
    return readFunctions[x](x, rmw);
}

//////////////////////////////////////////////////////////
// Write a byte to CPU memory
//////////////////////////////////////////////////////////
void wcpubyte(Word x, Byte c)
{
	// no matter what kind of access, update the heat map
	UpdateHeatmap(x);

	// Check for write or access breakpoints
	for (int idx=0; idx<nBreakPoints; idx++) {
		switch (BreakPoints[idx].Type) {
			case BREAK_ACCESS:
			case BREAK_WRITE:
				if (CheckRange(idx, x)) {
					TriggerBreakPoint();
				}
				break;

			case BREAK_DISK_LOG:
				if (CheckRange(idx, x)) {
					if ((BreakPoints[idx].Data>0)&&(BreakPoints[idx].Data<10)) {
						if (NULL != DumpFile[BreakPoints[idx].Data]) {
							fputc(c, DumpFile[BreakPoints[idx].Data]);
						}
					}
				}
				break;
		}
	}

	// the cycle timing for read-before-write is dealt with in the opcodes
    // the only zero-wait-state memory is >0000 through >1FFF, and >8000 through >83FF
    // handling the wait state generator here rather than in the device handlers
	if ((x & 0x01) == 0) {					// this is a wait state (we cancel it below for ROM and scratchpad)
        if ((x > 0x2000) && ((x < 0x8000) || (x >= 0x8400))) {
    		pCurrentCPU->AddCycleCount(4);		// we can't do half of a wait, so just do it for the even addresses. This should
	    										// be right now that the CPU emulation does all Word accesses
        }
	}

    // pass the write to the handler - it's never allowed to be NULL
    writeFunctions[x](x);

	// check breakpoints against what was written to where
	for (int idx=0; idx<nBreakPoints; idx++) {
		switch (BreakPoints[idx].Type) {
			case BREAK_EQUALS_BYTE:
				if ((CheckRange(idx, x)) && ((c&BreakPoints[idx].Mask) == BreakPoints[idx].Data)) {
					TriggerBreakPoint();
				}
				break;
		}
	}
}

//////////////////////////////////////////////////////////////////
// Write a byte to GROM
//////////////////////////////////////////////////////////////////
void WriteValidGrom(int nBase, Word x, Byte c) {
//	if (nBase > 0) {
//		debug_write("Write GROM base %d(>%04X), >%04x, >%02x, %d", nBase, x, GROMBase[0].GRMADD, c, GROMBase[0].grmaccess);
//	}

	// Note that UberGROM access time (in the pre-release version) was 15 cycles (14.6), but it
	// does not apply as long as other GROMs are in the system (and they have to be due to lack
	// of address counter.) So this is still valid.

	if (x&0x0002)
	{
		GROMBase[0].GRMADD=(GROMBase[0].GRMADD<<8)|(c);						// write GROM address
		GROMBase[0].grmaccess--;
		if (GROMBase[0].grmaccess==0)
		{ 
			GROMBase[0].grmaccess=2;										// prefetch emulation

            // second GROM address write adds about 21 cycles (verified)
    		pCurrentCPU->AddCycleCount(21);
			
			// update MPD so it can reset if needed
			if (bMpdActive) {
				MpdHookNewAddress(GROMBase[0].GRMADD);
			}

			// update all bases prefetch
			for (int idx=0; idx<PCODEGROMBASE; idx++) {
				GROMBase[idx].grmdata=GROMBase[idx].GROM[GROMBase[0].GRMADD];
			}

            // TODO: This is not correct emulation for the gigacart, which ACTUALLY maintains
            // an 8-bit address latch and a 1 bit select (for GROM >8000)
            // But it's enough to let me test some theories...
   		    GROMBase[0].GRMADD++;
		} else {
            // first GROM address write adds about 15 cycles (verified)
    		pCurrentCPU->AddCycleCount(15);
        }

		// GROM writes do not affect the prefetches, and have the same
		// side effects as reads (they increment the address and perform a
		// new prefetch)
	}
	else
	{
		UpdateHeatGROM(GROMBase[0].GRMADD);

		// Check for breakpoints
		for (int idx=0; idx<nBreakPoints; idx++) {
			switch (BreakPoints[idx].Type) {
				case BREAK_WRITEGROM:
					if (CheckRange(idx, GROMBase[0].GRMADD-1)) {
						TriggerBreakPoint();
					}
					break;
			}
		}

		GROMBase[0].grmaccess=2;

		// MPD overrides the GRAM switch below
		if (bMpdActive) {
			MpdHookGROMWrite(GROMBase[0].GRMADD-1, c);
		}
		if ((bUberGROMActive) && ((Word)(GROMBase[0].GRMADD-1) >= 0x6000)) {
			UberGromWrite(GROMBase[0].GRMADD-1, nBase, c);
		}
 
		// Since all GRAM devices were hacks, they apparently didn't handle prefetch the same
		// way as I expected. Because of prefetch, the write address goes to the GROM address
		// minus one. Well, they were hacks in hardware, I'll just do a hack here.
		int nRealAddress = (GROMBase[0].GRMADD-1)&0xffff;
		if (GROMBase[0].bWritable[(nRealAddress&0xE000)>>13]) {
			// Allow it! The user is crazy! :)
			GROMBase[nBase].GROM[nRealAddress]=c;
		}
		// update all bases prefetch
		for (int idx=0; idx<PCODEGROMBASE; idx++) {
			GROMBase[idx].grmdata=GROMBase[idx].GROM[GROMBase[0].GRMADD];
		}
        // TODO: This is not correct emulation for the gigacart, which ACTUALLY maintains
        // an 8-bit address latch and a 1 bit select (for GROM >8000)
        // But it's enough to let me test some theories...
   		GROMBase[0].GRMADD++;

        // GROM data writes add about 22 cycles (verified)
   		pCurrentCPU->AddCycleCount(22);
	}
}


void wgrmbyte(Word x, Byte c)
{
	int nBank;

	if (x<0x9c00) 
	{
		return;											// read address
	}

	if (grombanking) {
		nBank=(x&0x3ff)>>2;								// maximum possible range to >9BFF - not all supported here though
		if (nBank >= PCODEGROMBASE) {
			debug_write("Invalid GROM base 0x%04X write", x);
			return;
		}
	} else {
		nBank=0;
	}

	return WriteValidGrom(nBank, x, c);
}

//////////////////////////////////////////////////////////////////
// Set bank for SuperSpace CRU method (currently leans on 379 code)
//////////////////////////////////////////////////////////////////
void SetSuperBank() {
	// NOTE: only 8 banks supported here (64k)
	// Does not work with all CRU-based carts (different paging schemes?)
	// TODO: May be because some (Red Baron) write to ROM, and I don't disable
	// the ROM-based banking here, which I should.

	// TODO: according to MAME, the Superspace pages at these addresses,
	// but the comment blocks are pretty confusing. If I could get some
	// superspace code, then maybe I could /try/ it...

	// What SHOULD this do if multiple CRU bits were set?
	// Right now we take the lowest one.
	
	if (CRU[0x0401]) {
		// is this not also true if all zeros written?
		xbBank=0;	
	} else if (CRU[0x0403]) {
		xbBank=1;
	} else if (CRU[0x0405]) {
		xbBank=2;
	} else if (CRU[0x0407]) {
		xbBank=3;
	} else if (CRU[0x0409]) {
		xbBank=4;
	} else if (CRU[0x040b]) {
		xbBank=5;
	} else if (CRU[0x040d]) {
		xbBank=6;
	} else if (CRU[0x040f]) {
		xbBank=7;
	}
	xbBank&=xb;

	// debug helper for me
//	TriggerBreakPoint();
}

//////////////////////////////////////////////////////////////////
// Write a bit to CRU
//////////////////////////////////////////////////////////////////
void wcru(Word ad, int bt)
{
	if (ad>=0x800) {
		// DSR space
		if (NULL != SetSidBanked) {
			SetSidBanked(false);	// SID is disabled no matter the write
		}
		ad<<=1;		// put back into familiar space. A bit wasteful, but devices aren't high performance
		if (bt) {
			// bit 0 enables the DSR rom, so we'll check that first
			if ((ad&0xff) == 0) {
				int nTmp = (ad>>8)&0xf;
				if ((nCurrentDSR != -1) && (nTmp != nCurrentDSR)) {
					debug_write("WARNING! DSR Conflict between >1%X00 and >1%X00 at PC >%04X", nCurrentDSR, nTmp, pCurrentCPU->GetPC());
				}
				nCurrentDSR=nTmp;
//				debug_write("Enabling DSR at >%04x", ad);
				// there may also be device-dependent behaviour! Don't exit.
			}
			switch (ad&0xff00) {
			case 0x1300:	// RS232/PIO card
				WriteRS232CRU((ad&0xff)>>1, bt);
				break;

			case 0x1e00:	// AMS Memory card
				// the CRU bit is taken only from A12-A14, and only 0 and 1 are valid
				// 0000 0000 0000 1110
				{
					int nCRUBit = (ad&0x000e)>>1;
					switch (nCRUBit) {
						case 0: 
							EnableMapperRegisters(true);
							break;
						case 1:
							SetMemoryMapperMode(Map);
							break;
						// nothing else is wired up!
					}
				}
				break;

			case 0x1f00:	// pCode card
				if ((ad&0xff) == 0x80) {
					// bank switch
					debug_write("Switching P-Code to bank 2");
					nDSRBank[0xf]=1;
				}
				break;
			}
		} else {
			// bit 0 enables the DSR rom, so we'll check that first
			if ((ad&0xff) == 0) {
				if (((ad>>8)&0xf) == nCurrentDSR) {
					nCurrentDSR=-1;
//					debug_write("Disabling DSR at >%04x", ad);
					// may be device-dependent behaviour, don't exit
				}
			}
			// else, it's device dependent
			switch (ad&0xff00) {
			case 0x1300:	// RS232/PIO card
				WriteRS232CRU((ad&0xff)>>1, bt);
				break;

			case 0x1e00:	// memory card
				// the CRU bit is taken only from A12-A14, and only 0 and 1 are valid
				// 0000 0000 0000 1110
				{
					int nCRUBit = (ad&0x000e)>>1;
					switch (nCRUBit) {
						case 0: 
							EnableMapperRegisters(false);
							break;
						case 1:
							SetMemoryMapperMode(Passthrough);
							break;
						// nothing else is wired up!
					}
				}
				break;

			case 0x1f00:	// pCode card
				if ((ad&0xff) == 0x80) {
					// bank switch
					debug_write("Switching P-Code to bank 1");
					nDSRBank[0xf]=0;
				}
				break;
			}
		}
		return;
	} else {
		if (NULL != SetSidBanked) {
			SetSidBanked(true);		// SID is enabled no matter the write
		}

		ad=(ad&0x0fff);										// get actual CRU line

//		debug_write("Write CRU 0x%x with %d", ad, bt);

        // cassette bits:
        // 22 - CS1 control (1 = motor on)
        // 23 - CS2 control (1 = motor on)
        // 24 - audio gate  (1 = silence)
        // 25 - audio out (shared)
        // 26 - audio in  (CS1 only)
        // most of these we just store and let other code deal with them

		if (bt) {											// write the data
			if ((ad>0)&&(ad<16)&&(CRU[0]==1)) {
				if (ad == 15) {
					// writing 1 has no effect
				} else {
					// writing to CRU 9901 timer
                    // V9T9 doesn't update starttimer9901 until release from clock mode (double-buffered), but it probably doesn't matter to most apps
                    int oldtimer = starttimer9901;
					Word mask=0x01<<(ad-1);
					starttimer9901|=mask;
                    if (oldtimer != starttimer9901) {
//                      debug_write("9901 timer now set to %d", starttimer9901);
                        // per adam doc, we start decrementing now (non-zero value)
                        timer9901=starttimer9901;
                    }
				}
			} else {
				CRU[ad]=1;
				switch (ad) {
                    case 0:
                        // timer mode (update readback reg)
                        // Adam doc says that the read register also gets a copy every decrement,
                        // (ie: it just stops updating), but since we can't read it otherwise anyway,
                        // this should have the same effect.
                        timer9901Read = timer9901;
                        break;

					case 3:
						// timer interrupt bit
						timer9901IntReq=0;
//                        debug_write("9901 timer interrupt off");
						break;

                    case 18:
                    case 19:
                    case 20:
                        // keyboard column select
//                        debug_write("Keyboard column now: %d", (CRU[0x14]==0 ? 1 : 0) | (CRU[0x13]==0 ? 2 : 0) | (CRU[0x12]==0 ? 4 : 0));
                        break;

                    case 22:
                        // CS1 motor on
                        setTapeMotor(true);
                        break;

                    case 23:
                        // CS2 motor on
                        debug_write("CS2 is not supported.");
                        break;
					
					case 0x040f:
						// super-space cart piggybacked on 379 code for now
						SetSuperBank();
						break;
				}
			}
		} else {
			if ((ad>=0)&&(ad<16)&&(CRU[0]==1)) {
				if (ad == 15) {
					// writing 0 is a soft reset of the 9901 - it resets
					// all I/O pins to pure input, but does not affect the timer
					// it only has this effect in timer mode, but is not a timer function.
				} else if (ad == 0) {
					// Turning off timer mode - start timer (but don't reset it, as proven by camelForth)
                    // Not sure this matters to this emulation, but Adam doc notes that the 9901 will exit
                    // clock mode if it even sees "a 1 on select line S0", even when chip select is not active.
                    // this may be the bit I mention below that Thierry noted as well.
					CRU[ad]=0;
                    if (timer9901 != starttimer9901) {
                        // TODO: in this initial case, would the interrupt flag be set?
                        // BUT: this change breaks tape. To read tape, I /must/ set timer9901=starttimer9901
                        // So who's right? How does the damn timer work?
                        // V9T9 (DOS) DOES this reset every time it exits clock mode
                        timer9901=starttimer9901;
                    }
//					timer9901=starttimer9901;
//                  timer9901Read=timer9901;    // V9T9 DOS updates timer9901Read ONLY when setting to clock mode (bit set is 1), so this is never right here
					CRUTimerTicks=0;
//					debug_write("Starting 9901 timer at %d ticks", timer9901);
				} else {
					// writing to CRU 9901 timer
                    // V9T9 doesn't update starttimer9901 until release from clock mode (double-buffered), but it probably doesn't matter to most apps
					Word mask=0x01<<(ad-1);
                    int oldtimer = starttimer9901;
					starttimer9901&=~mask;
                    if (oldtimer != starttimer9901) {
//                      debug_write("9901 timer now set to %d", starttimer9901);
                        // per adam doc, restart decrementing on any non-zero write to the timer register (this was zero)
                    }
				}
			} else {
				CRU[ad]=0;
				switch (ad) {
					case 3:
						// timer interrupt bit
						timer9901IntReq=0;
//                        debug_write("9901 timer request cleared");
						break;

                    case 18:
                    case 19:
                    case 20:
                        // keyboard column select
//                        debug_write("Keyboard column now: %d", (CRU[0x14]==0 ? 1 : 0) | (CRU[0x13]==0 ? 2 : 0) | (CRU[0x12]==0 ? 4 : 0));
                        break;

                    case 22:
                        // CS1 motor off
                        setTapeMotor(false);
                        break;
					
					case 0x040f:
						// super-space cart piggybacked on 379 code for now
						SetSuperBank();
						break;
				}
			}
		}
		if ((ad > 15) && (ad < 31) && (CRU[0] == 1)) {
			// exit timer mode
			wcru(0,0);
		}
		// There's another potential case for automatic exit of timer mode
		// if a value from 16-31 appears on A10-A15 (remember A15 is LSB),
		// Thierry Nouspikel says that the 9901 will see this and exit 
		// timer mode as well, even though it's not a CRU operation.
		// That would mean any address access to >xx1x, >xx5x, >xx9x, >xxdx
		// should trigger it. Maybe we can test this on a real machine
		// sometime? TODO.
	}
}

//////////////////////////////////////////////////////////////////
// Read a bit from CRU
//////////////////////////////////////////////////////////////////
int CheckJoysticks(Word ad, int col) {
	int joyX, joyY, joyFire, joykey;
	int joy1col, joy2col;
	int ret=1;

	// Read external hardware
	joyX=0;
	joyY=0;
	joyFire=0;

	if (nSystem != 0) {
		// 99/4A
		joy1col=4;
		joy2col=0;
		joykey=1;
	} else {
		// 99/4
		joy1col=2;
		joy2col=4;
		joykey=0;
	}

	if ((col == joy1col) || (col == joy2col))				// reading joystick
	{	
		// TODO: This still reads the joystick many times for a single scan, but it's better ;)
		if (fJoy) {
			int device;

			device=-1;

			if (col==joy2col) {
				switch (joy2mode) {
					case 1: device=JOYSTICKID1; break;
					case 2: device=JOYSTICKID2; break;
				}
			} else {
				switch (joy1mode) {
					case 1: device=JOYSTICKID1; break;
					case 2: device=JOYSTICKID2; break;
				}
			}

			if ((device == JOYSTICKID1)&&((installedJoysticks & 0x01) == 0)) {
				return 1;
			} else if ((device == JOYSTICKID2)&&((installedJoysticks & 0x02) == 0)) {
				return 1;
			}

			if (device!=-1) {
				memset(&myJoy, 0, sizeof(myJoy));
				myJoy.dwSize=sizeof(myJoy);
				myJoy.dwFlags=JOY_RETURNBUTTONS | JOY_RETURNX | JOY_RETURNY | JOY_USEDEADZONE;
				MMRESULT joyret = joyGetPosEx(device, &myJoy);
				if (JOYERR_NOERROR == joyret) {
					if (0!=myJoy.dwButtons) {
						joyFire=1;
					}
					if (myJoy.dwXpos<0x4000) {
						joyX=-4;
					}
					if (myJoy.dwXpos>0xC000) {
						joyX=4;
					}
					if (myJoy.dwYpos<0x4000) {
						joyY=4;
					}
					if (myJoy.dwYpos>0xC000) {
						joyY=-4;
					}
				} else {
					// disable this joystick so we don't slow to a crawl
					// trying to access it. We'll check again on a reset
					if (device == JOYSTICKID1) {
						debug_write("Disabling joystick 1 - error %d reading it.", joyret);
						installedJoysticks&=0xfe;
					} else {
						// JOYSTICKID2
						debug_write("Disabling joystick 2 - error %d reading it.", joyret);
						installedJoysticks&=0xfd;
					}
				}
			} else {	// read the keyboard
				// if just activating the joystick, so make sure there's no fctn-arrow keys active
				// just forcibly turn them off! Should only need to do this once

				if (key[VK_TAB]) {
					joyFire=1;
					if (0 == fJoystickActiveOnKeys) {
						decode(0xf0);	// key up
						decode(VK_TAB);
					}
				}
				if (key[VK_LEFT]) {
					joyX=-4;
					if (0 == fJoystickActiveOnKeys) {
						decode(0xe0);	// extended
						decode(0xf0);	// key up
						decode(VK_LEFT);
					}
				}
				if (key[VK_RIGHT]) {
					joyX=4;
					if (0 == fJoystickActiveOnKeys) {
						decode(0xe0);	// extended
						decode(0xf0);	// key up
						decode(VK_RIGHT);
					}
				}
				if (key[VK_UP]) {
					joyY=4;
					if (0 == fJoystickActiveOnKeys) {
						decode(0xe0);	// extended
						decode(0xf0);	// key up
						decode(VK_UP);
					}
				}
				if (key[VK_DOWN]) {
					joyY=-4;
					if (0 == fJoystickActiveOnKeys) {
						decode(0xe0);	// extended
						decode(0xf0);	// key up
						decode(VK_DOWN);
					}
				}

				fJoystickActiveOnKeys=180;		// frame countdown! Don't use PS2 arrow keys for this many frames
			}
		}

		if (ad == 3)
		{	
			if ((key[KEYS[joykey][col][0]])||(joyFire))	// button reads normally
			{
				ret=0;
			}
		}
		else
		{
			if (key[KEYS[joykey][col][ad-3]])		// stick return (*not* inverted. Duh)
			{	
				ret=0;
			}
			if (ret) {
				switch (ad-3) {						// Check real joystick
				case 1: if (joyX ==-4) ret=0; break;
				case 2: if (joyX == 4) ret=0; break;
				case 3: if (joyY ==-4) ret=0; break;
				case 4: if (joyY == 4) ret=0; break;
				}
			}
		}
	}
	return ret;
}

int rcru(Word ad)
{
	int ret,col;									// temp variables

	if ((CRU[0]==1)&&(ad<16)&&(ad>0)) {				// read elapsed time from timer
		if (ad == 15) {
			return timer9901IntReq;
		}
		Word mask=0x01<<(ad-1);
		if (timer9901Read & mask) {
			return 1;
		} else {
			return 0;
		}
	}

	if ((ad > 15) && (ad < 31) && (CRU[0] == 1)) {
		// exit timer mode
		wcru(0,0);
	}

	// only certain CRU bits are implemented, check them
	if (ad >= 0x0800) {
		ad<<=1;		// puts it back into a familiar space - wasteful but easier to deal with

		switch (ad&0xff00) {
			case 0x1100:	// disk controller
				if (nDSRBank[1] != 0) {
					// TICC paged in 
					return ReadTICCCRU((ad&0xff)>>1);
				}
				return 1;	// "false"

			case 0x1300:	// RS232/PIO card
				return ReadRS232CRU((ad&0xff)>>1);

			default:
				// no other cards supported yet
				return 1;	// "false"
		}
	}

	// The CRU bits >0000 through >001f are repeated through the whole 4k range!
	ad=(ad&0x001f);										// get actual CRU line
	ret=1;												// default return code (false)

	// are we checking VDP interrupt?
	if (ad == 0x02) {		// that's the only int we have
		if (VDPINT) {
			return 0;		
		} else {
			return 1;
		}
	}
	if (ad == 0x01) {		// this would be a peripheral card interrupt
		// todo: we don't have any, though!
		return 1;
	}

    // cassette support
    if (ad == 27) {
        // tape input (the outputs can't be read back, technically)
        if (getTapeBit()) {
            return 0;   // inverted logic
        } else {
            return 1;   // this also preserves the Perfect Push 'tick' on audio gate
        }
    }
    if ((ad >= 22) && (ad < 25)) {
        // these are the cassette CRU output bits
        return 1;
    }

	// no other hardware devices at this time, check keyboard/joysticks

	// keyboard reads as an array. Bits 24, 26 and 28 set the line to	
	// scan (columns). Bits 6-14 are used for return. 0 means on.		
	// The address was divided by 2 before being given to the routine	

	// Some hacks here for 99/4 scanning
	if ((ad>=0x03)&&(ad<=0x0a))
	{	
		col=(CRU[0x14]==0 ? 1 : 0) | (CRU[0x13]==0 ? 2 : 0) | (CRU[0x12]==0 ? 4 : 0);	// get column

		if (keyboard==KEY_994A_PS2) {
			// for 99/4A only, not 99/4
			unsigned char in;

			in=CheckTIPolling(col|((CRU[0x15]==0)?8:0));	// add in bit 4 for alpha lock scanning

			if (0xff != in) {
				// (ad-3) is the row number we are checking (bit #)
				if (0 == (in & (1<<(ad-3)))) {
					ret=0;
				} else {
					ret=1;
				}
				return ret;
			}

			// else, try joysticks
			return CheckJoysticks(ad, col);
		}

		// not PS/2, use the old method
		if ((ad==0x07)&&(CRU[0x15]==0))					// is it ALPHA LOCK?
		{	
			ret=0;
			if (GetKeyState(VK_CAPITAL) & 0x01)			// check CAPS LOCK (on?)
			{	
				ret=1;									// set Alpha Lock off (invert caps lock)
			}

			return ret;
		}

		// Either joysticks or keyboard - try joysticks first
		ret = CheckJoysticks(ad, col);
		if (1 == ret) {
			// if nothing else matched, try the keyboard array
			if (key[KEYS[keyboard][col][ad-3]])				// normal key
			{	
					ret=0;
			}
		}
	}
	if ((ad>=11)&&(ad<=31)) {
		// this is an I/O pin - return whatever was last written
		ret = CRU[ad];
	}

	return(ret);
}

/////////////////////////////////////////////////////////////////////////
// Write a line to the debug buffer displayed on the debug screen
/////////////////////////////////////////////////////////////////////////
void debug_write(char *s, ...)
{
	char buf[1024];

	_vsnprintf(buf, 1023, s, (char*)((&s)+1));
	buf[1023]='\0';

	if (!quitflag) {
		OutputDebugString(buf);
		OutputDebugString("\n");
	}

	buf[DEBUGLEN-1]='\0';


	EnterCriticalSection(&DebugCS);
	
	memcpy(&lines[0][0], &lines[1][0], 33*DEBUGLEN);				// scroll data
	strncpy(&lines[33][0], buf, DEBUGLEN);							// copy in new line
	memset(&lines[33][strlen(buf)], 0x20, DEBUGLEN-strlen(buf));	// clear rest of line
	lines[33][DEBUGLEN-1]='\0';										// zero terminate

	LeaveCriticalSection(&DebugCS);

	bDebugDirty=true;												// flag redraw
}

// Simple thread that watches the event and clears the buffer, then
// restarts playback
void __cdecl SpeechBufThread(void *) {
	DWORD ret; 
	UCHAR *ptr1, *ptr2;
	unsigned long len1, len2;

	for (;;) {
		if (INVALID_HANDLE_VALUE == hSpeechBufferClearEvent) {
			Sleep(150);
			ret=WAIT_TIMEOUT;
		} else {
			ret=WaitForSingleObject(hSpeechBufferClearEvent, 150);
		}
		if (WAIT_OBJECT_0 == ret) {
			if (SUCCEEDED(speechbuf->Lock(0, 0, (void**)&ptr1, &len1, (void**)&ptr2, &len2, DSBLOCK_ENTIREBUFFER))) {
				// since we haven't started the sound, hopefully the second pointer is nil
				if (len2 != 0) {
					debug_write("Failed to lock speech buffer");
				}
				// signed 16-bit - zero the buffer
				memset(ptr1, 0, len1);

				speechbuf->Unlock(ptr1, len1, ptr2, len2);
			}

			if (FAILED(speechbuf->Play(0, 0, 0))) {
				debug_write("Speech DID NOT START");
			}
		}
		if (quitflag) break;
	}
}

//////////////////////////////////////////////////////////////
// 'Retrace' counter for timing - runs at 50 or 60 hz
// Uses 'hzRate'. Coded for lessor CPU usage, may be
// less accurate on a small scale but about the same
// over time.
//////////////////////////////////////////////////////////////
extern int dac_pos;
extern double dacupdatedistance;
void __cdecl TimerThread(void *)
{
	MY_LARGE_INTEGER nStart, nEnd, nFreq, nAccum;
	static unsigned long old_total_cycles=0;
	static int oldSystemThrottle=0, oldCPUThrottle=0;
	static int nVDPFrames = 0;
	bool bDrawDebug=false;
	long nOldCyclesLeft = 0;
	int oldHzRate = 0;
	HANDLE timer = CreateWaitableTimer(NULL, false, NULL);
	
	// Ensure the scheduler won't move us around on multicore machines
	SetThreadAffinityMask(GetCurrentThread(), 0x01);
	timeBeginPeriod(1);

	time(&STARTTIME);
	if (FALSE == QueryPerformanceCounter((LARGE_INTEGER*)&nStart)) {
		debug_write("Failed to query performance counter, error 0x%08x", GetLastError());
		MessageBox(myWnd, "Unable to run timer system.", "Classic99 Error", MB_ICONSTOP|MB_OK);
		ExitProcess(-1);
	}

	nAccum.QuadPart=0;

	while (quitflag==0) {
		// Check if the system speed has changed
		// This is actually kind of lame - we should use a message or make the vars global
		if ((CPUThrottle != oldCPUThrottle) || (SystemThrottle != oldSystemThrottle)) {
			oldCPUThrottle=CPUThrottle;
			oldSystemThrottle=SystemThrottle;
			old_total_cycles=total_cycles;
			nAccum.QuadPart=0;
		}
		if (hzRate != oldHzRate) {
			LARGE_INTEGER due;
			due.QuadPart=-1;		// now, essentially
			if (!SetWaitableTimer(timer, &due, 8, NULL, NULL, FALSE)) {	// we can wake up at any speed, the loop below works out real time
                debug_write("The waitable timer failed - code %d", GetLastError());
            }
			oldHzRate = hzRate;
		}

		// process debugger, if active
		processDbgPackets();

		if ((PauseInactive)&&(myWnd != GetForegroundWindow())&&(dbgWnd != GetForegroundWindow())) {
			// Reduce CPU usage when inactive (hack)
			Sleep(100);
		} else {
			// if hzRate==50, then it's 20000us per frame
			// if hzRate==60, then it's 16666us per frame - .6. overall this runs a little slow, but it is within the 5% tolerance (99.996%)
			// our actual speeds are 50hz and 62hz, 62hz is 99% of 62.6hz, calculated via the datasheet
			// 62hz is 16129us per frame (fractional is .03, irrelevant here, so it works out nicer too)
			switch (CPUThrottle) {
				default:
                    // TODO: using the old trick of triggering early (twice as often) helps, but still wrong
					WaitForSingleObject(timer, 1000);	// this is 16.12, rounded to 16, so 99% of 99% is still 99% (99.2% of truth)
					break;
				case CPU_OVERDRIVE:
					Sleep(1);	// minimal sleep for load's sake
					break;
				case CPU_MAXIMUM:
					// We do the exchange here since the loop below may not run
					//InterlockedExchange((LONG*)&cycles_left, max_cpf*100);
					break;
			}

			if (SystemThrottle == VDP_CPUSYNC) {
				nVDPFrames=0;
			}
			if (FALSE == QueryPerformanceCounter((LARGE_INTEGER*)&nEnd)) {
				debug_write("Failed to query performance counter, error 0x%08x", GetLastError());
				MessageBox(myWnd, "Unable to run timer system.", "Classic99 Error", MB_ICONSTOP|MB_OK);
				ExitProcess(-1);
			}
			if (nEnd.QuadPart<nStart.QuadPart) {
				// We wrapped around. This should be a once in a lifetime event, so rather
				// than go nuts, just skip this frame
				nStart.QuadPart=nEnd.QuadPart;
				continue;
			}
			QueryPerformanceFrequency((LARGE_INTEGER*)&nFreq);

			// Work out how long we actually slept for, in microseconds
			nAccum.QuadPart+=(((nEnd.QuadPart-nStart.QuadPart)*1000000i64)/nFreq.QuadPart);	
			nStart.QuadPart=nEnd.QuadPart;					// don't lose any time
			
			// see function header comments for these numbers (62hz or 60hz : 50hz)
			nFreq.QuadPart=(hzRate==HZ60) ? 16129i64/*16666i64*/ : 20000i64;

			while (nAccum.QuadPart >= nFreq.QuadPart) {
				nVDPFrames++;
				// this makes us run the right number of frames, and should account for fractions better
				nAccum.QuadPart-=nFreq.QuadPart;

				if (max_cpf > 0) {
					// to prevent runaway, if the CPU is not executing for some reason, don't increment
					// this handles the case where Windows is blocking the main thread (which doesn't happen anymore)
					if ((nOldCyclesLeft != cycles_left) || (max_cpf == 1)) {
						if (CPUThrottle==CPU_NORMAL) {			// don't increment cycles_left if running at infinite speed or paused
							InterlockedExchangeAdd((LONG*)&cycles_left, max_cpf);
						} else {
							InterlockedExchange((LONG*)&cycles_left, max_cpf*50);
						}
						nOldCyclesLeft = cycles_left;
					}
				}
				if ((nVDPFrames > 10) && (max_cpf > 0)) {
					// more than a 1/6 second behind - just drop it
					nAccum.QuadPart = 0;
					InterlockedExchange((LONG*)&cycles_left, max_cpf);
					nVDPFrames = 1;
				}
			}
			SetEvent(hWakeupEvent);		// wake up CPU if it's sleeping

			if (total_cycles_looped) {
				total_cycles_looped=false;
				old_total_cycles=0;		// mistiming, but survives the wrap.
				// very very fast machines may someday break this loop
			}

			// copy over the speech buffer -- only need to do this once
			// if our timing is right this should always work out about right
			SpeechBufferCopy();

			// This set the VDP processing rate. If VDP overdrive is active,
			// then we base it on the CPU cycles. If not, then we base it on
			// real time.
			if (SystemThrottle == VDP_CPUSYNC) {
				// this side is used in normal mode
				while (old_total_cycles+(hzRate==HZ50?DEFAULT_50HZ_CPF:DEFAULT_60HZ_CPF) <= total_cycles) {
					Counting();					// update counters & VDP interrupt
					old_total_cycles+=(hzRate==HZ50?DEFAULT_50HZ_CPF:DEFAULT_60HZ_CPF);
					bDrawDebug=true;
				}
			} else {
				// this side is used in overdrive
				if (nVDPFrames > 0) {
					// run one frame every time we're able (and it's needed)
					Counting();					// update counters & VDP interrupt
					nVDPFrames--;
					bDrawDebug=true;
					vdpForceFrame();
				}
			}

			if ((bDrawDebug)&&(dbgWnd)) {
				if (max_cpf > 0) {
					draw_debug();
				}
			}
		}	
	}

	time(&ENDTIME);
	debug_write("Seconds: %ld, ticks: %ld", (long)ENDTIME-STARTTIME, ticks);

	timeEndPeriod(1);
	debug_write("Ending Timer Thread");
}

////////////////////////////////////////////////////////////////
// Timer calls this function each tick
////////////////////////////////////////////////////////////////
//extern SID *g_mySid;
void Counting()
{
	ticks++;
	retrace_count++;
	//end_of_frame=1;		// one frame of time has elapsed, do our processing

	// update sound buffer -- eventually we should instead move this to generate from the
	// scanline based VDP (the one not written yet, hehe)
	static struct StreamData soundDat, sidDat;

	EnterCriticalSection(&csAudioBuf);

	if (NULL != soundbuf) {
		UpdateSoundBuf(soundbuf, sound_update, &soundDat);
	}
	if ((NULL != sidbuf) && (NULL != sid_update)) {
		UpdateSoundBuf(sidbuf, sid_update, &sidDat);
#if 0
// HACK - REMOVE ME - CONVERT SID MUSIC TO 9919?
// TO USE THIS HACK, BREAKPOINT IN THE SID DLL IN THE RESET
// FUNCTION, AND GET THE ADDRESS OF mySid. THEN STEP OUT,
// AND ASSIGN THAT ADDRESS TO g_mySid. The rest will just work.
// Or implement GetSidPointer in the DLL. Sadly I've lost the code.
//volume - voice.envelope.envelope_counter
//
//noise is generated on a voice when the voice.waveform == 0x08 (combinations do nothing anyway)
//voice.waveform.freq is the frequency counter
//
//FREQUENCY = (REGISTER VALUE * CLOCK)/16777216 Hz
//where CLOCK=1022730 for NTSC systems and CLOCK=985250 for PAL systems.
//
//the TI version clock is exactly 1000000, so it's 
//FREQUENCY = REGISTER_VALUE / 16.777216
//
//The TI 9919 sound chip uses:
//FREQUENCY = 111860.78125 / REGISTER_VALUE
//and inversely, 
//REGISTER_VALUE = 111860.78125 / FREQUENCY
		if (NULL == g_mySid) {
            if (NULL != GetSidPointer) {
                g_mySid = GetSidPointer();
            }
        } else {
			static int nDiv[3] = {1,1,1};		// used for scale adjust when a note is too far off (resets if all three channels are quiet)
			bool bAllQuiet=true;
			bool bNoise = false;

			sidbuf->SetVolume(DSBVOLUME_MIN);	// don't play audibly
			for (int i=0; i<3; i++) {
				// pitch
				double nFreq = g_mySid->voice[i]->wave.freq / 16.777216;
				int code = (int)((111860.78125 / nFreq) / nDiv[i]);

				// volume (is just the envelope enough? is there a master volume?)
				// looks like this is 8-bit volume, convert to 4-bit attenuation
                // TODO: is it linear instead of logarithmic?
				int nVol = ((255-g_mySid->voice[i]->envelope.envelope_counter)>>4);
				if (nVol != 0x0f) bAllQuiet = false;
				int ctrl = 0x80+(0x20*i);	

				// check for noise
				if (g_mySid->voice[i]->wave.waveform & 0x08) {
					// this is noise
					bNoise = true;
					// mute the tone and don't worry about adjusting it
					wsndbyte((ctrl+0x10) | 0x0f);
					// pick a noise - 5,6,7 are the white noise tones. Periodic may be useful too but for now...
					if ((code == 0) || (code > 0x180)) {
						code = 0xe7;
					} else if (code > 0xc0) {
						code = 0xe6;
					} else {
						code = 0xe5;
					}
					wsndbyte(code);
					// set the volume on the noise channel
					wsndbyte(0xf0 | nVol);
				} else {
					// this is tone
					if (code > 0x3ff) {
						//code=0;		// lowest possible pitch
						nDiv[i]*=2;		// scale it an octave up
						if (nDiv[i]>4) nDiv[i]=4;		// this is about the limit
						code/=2;
						if (code > 0x3ff) code=0;		// if still, get it next time
					}
					wsndbyte(ctrl|(code&0xf));
					wsndbyte((code>>4)&0xff);
					wsndbyte((ctrl+0x10) |  nVol);
				}
			}
			if (bNoise == false) {
				// make sure the noise channel is silent when not active
				wsndbyte(0xff);
			}
			if (bAllQuiet) {
				// reset the pitch divisors
				nDiv[0] = nDiv[1] = nDiv[2] = 1;
			}
		}
#endif
	}

	LeaveCriticalSection(&csAudioBuf);
}

// Debug step helpers
void DoPause() {
	if (0 != max_cpf) {
		TriggerBreakPoint();
	}
}

void DoStep() {
	if (0 == max_cpf) {
		InterlockedExchange((LONG*)&cycles_left, 1);	// allow one instruction through
		bDebugAfterStep=true;
		nStepCount=1;
		SetEvent(hWakeupEvent);		// wake up CPU if it's sleeping
	}
}

void DoStepOver() {
	if (0 == max_cpf) {
		max_cpf=cfg_cpf;
		SetWindowText(myWnd, szDefaultWindowText);
		InterlockedExchange((LONG*)&cycles_left, max_cpf);
		pCurrentCPU->SetReturnAddress(0);
		bStepOver=true;
		nStepCount=1;
		SetEvent(hWakeupEvent);		// wake up CPU if it's sleeping
	}
}

void DoPlay() {
	if (0 == max_cpf) {
		max_cpf=cfg_cpf;
		nStepCount=1;
		SetWindowText(myWnd, szDefaultWindowText);
		InterlockedExchange((LONG*)&cycles_left, max_cpf);
		SetSoundVolumes();
		UpdateMakeMenu(dbgWnd, 0);
	}
	PostMessage(myWnd, WM_COMMAND, ID_CPUTHROTTLING_NORMAL, 1);
	SetEvent(hWakeupEvent);		// wake up CPU if it's sleeping
}

void DoFastForward() {
	DoPlay();		// wake up clean, then accelerate
	PostMessage(myWnd, WM_COMMAND, ID_CPUTHROTTLING_SYSTEMMAXIMUM, 1);
}

void DoLoadInterrupt() {
	if (pCurrentCPU == pCPU) {
		if ((0 != romword(0xfffc)) && (0 != romword(0xfffe))) {
			Word x1;

			// only if a vector is set - this is RAM so should be safe
			// do a BLWP to 0xFFFC
			debug_write("Load interrupt triggered with valid vector");

			doLoadInt = true;
		
		} else {
			debug_write("Ignoring load interrupt as no vector loaded.");
		}
	} else {
		debug_write("No LOAD during GPU execution");
	}
}

void DoMemoryDump() {
	// TODO: Add GUI to select CPU RAM, AMS RAM and VDP RAM
	if (IDYES == MessageBox(myWnd, "Dump memory TO MEMDUMP.BIN and VDPDUMP.BIN?", "Classic99 Dump RAM", MB_YESNO)) {
		FILE *fp=fopen("MEMDUMP.BIN", "wb");
		if (NULL != fp) {
			unsigned char buf[8192];
			for (int idx=0; idx<65536; idx++) {
				buf[idx%8192]=ReadMemoryByte((Word)idx);
				if (idx%8192 == 8191) {
					fwrite(buf, 1, 8192, fp);
				}
			}
			fclose(fp);
		}
		fp=fopen("VDPDUMP.BIN", "wb");
		if (NULL != fp) {
			fwrite(VDP, 1, 16384, fp);
			// write the VDP registers at the end of the file (8 bytes more)
			for (int idx=0; idx<8; idx++) {
				fputc(VDPREG[idx], fp);
			}
			fclose(fp);
		}
		debug_write("Dumped memory to MEMDUMP.BIN and VDPDUMP.BIN");
	}
}

void TriggerBreakPoint(bool bForce) {
	if ((!pCurrentCPU->enableDebug)&&(!bForce)) {
		return;
	}
	SetWindowText(myWnd, "Classic99 - Breakpoint. F1 - Continue, F2 - Step, F3 - Step Over");
	max_cpf=0;
	MuteAudio();
	InterlockedExchange(&cycles_left, 0);
	UpdateMakeMenu(dbgWnd, 1);
	draw_debug();
	redraw_needed = REDRAW_LINES;
}

void memrnd(void *pRnd, int nCnt) {
	// fill memory with a random pattern 
	// We use this to randomly set RAM rather than
	// assume it's always powered up as zeroed

	// however, users have requested this be an option, not forced ;)
	if (bScrambleMemory) {
		for (int i=0; i<nCnt; i++) {
			*((unsigned char *)pRnd+i) = rand()%256;
		}
	} else {
		memset(pRnd, 0, nCnt);
	}
}

// 64k heatmap only
int nHeatMap[0x10000];		
extern HWND hHeatMap;

// only the CPU heatmap worries about displaying it
void UpdateHeatVDP(int Address) {
	// we do a little trick here to flip it vertically (and truncate to 16 bit)
	// this helps with Windows liking upside down bitmaps
	Address=(Address&0xff) | (0xff00-(Address&0xff00));
	nHeatMap[Address&0xffff]|=0xff;		// if we assume 0RGB format, this is max blue (no matter what it was before)
}

void UpdateHeatGROM(int Address) {
	// we do a little trick here to flip it vertically (and truncate to 16 bit)
	// this helps with Windows liking upside down bitmaps
	Address=(Address&0xff) | (0xff00-(Address&0xff00));
	nHeatMap[Address]|=0xff00;		// if we assume 0RGB format, this is max green (no matter what it was before)
}

void UpdateHeatmap(int Address) {
	static int nCnt=0;
	static int nIdx=0;
	static LARGE_INTEGER tLast = { 0,0 };
	static LARGE_INTEGER tSpeed = { 0,0 };

	// we do a little trick here to flip it vertically (and truncate to 16 bit)
	// this helps with Windows liking upside down bitmaps
	Address=(Address&0xff) | (0xff00-(Address&0xff00));
	nHeatMap[Address]|=0xff0000;		// if we assume 0RGB format, this is max red (no matter what it was before)

	// in order to refresh the heatmap nicely, every cycle we will fade out a few pixels
	// The idea is, we have 65536 pixels. We want a pixel to reach 0 in about 3 seconds.
	// Each pixel has 256 levels. So, that's 16,777,216 pixels in 3 seconds, 5,592,405 pixels
	// in 1 second. The CPU clock is 3000000 cycles per second, and each access cycle is two bytes,
	// (not really, but that's okay here), so 2 pixels per access is enough. Since that's not
	// really true, we'll do a few and see how that goes.
	for (int i=0; i<HeatMapFadeSpeed; i++) {
		int r,g,b;
		r=(nHeatMap[nIdx]&0xff0000);
		r=(r-0x00010000)&0xff0000;
		if (r == 0xff0000) r=0;

		g=(nHeatMap[nIdx]&0xff00);
		g=(g-0x00000100)&0xff00;
		if (g == 0xff00) g=0;

		b=(nHeatMap[nIdx]&0xff);
		b=(b-0x00000001)&0xff;
		if (b == 0xff) b=0;

		nHeatMap[nIdx]=r|g|b;

		nIdx++;
		if (nIdx >= 0xffff) nIdx=0;
	}
	
	// every 50,000 cycles, draw the heatmap. this is roughly 1/60th of a second. 
	// We use a mask for 65536 cycles, so it works out more like 45 times a second. it's fine.
	if (((++nCnt)&0xffff) != 0) {
		return;
	}
	// sort of safety, make sure it's not too frequent - some systems don't like that!
	if (tSpeed.QuadPart == 0ui64) {
		QueryPerformanceFrequency(&tSpeed);
	} else {
		LARGE_INTEGER tNow;
		QueryPerformanceCounter(&tNow);
		if (tNow.QuadPart - tLast.QuadPart < (tSpeed.QuadPart)/hzRate) {
			return;
		}
		tLast=tNow;
	}

	// dump it to the window, if the window is up
	// I sorta wish this was on a different thread, maybe VDP thread?
	if (NULL != hHeatMap) {
		BITMAPINFO myInfo;

		myInfo.bmiHeader.biSize=sizeof(myInfo.bmiHeader);
		myInfo.bmiHeader.biWidth=256;
		myInfo.bmiHeader.biHeight=256;
		myInfo.bmiHeader.biPlanes=1;
		myInfo.bmiHeader.biBitCount=32;
		myInfo.bmiHeader.biCompression=BI_RGB;
		myInfo.bmiHeader.biSizeImage=0;
		myInfo.bmiHeader.biXPelsPerMeter=1;
		myInfo.bmiHeader.biYPelsPerMeter=1;
		myInfo.bmiHeader.biClrUsed=0;
		myInfo.bmiHeader.biClrImportant=0;

		HDC myDC=GetDC(hHeatMap);
		SetDIBitsToDevice(myDC, 0, 0, 256, 256, 0, 0, 0, 256, nHeatMap, &myInfo, DIB_RGB_COLORS);
		ReleaseDC(hHeatMap, myDC);
	}
}

