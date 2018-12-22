#ifndef _DOSPACK_DOSBLOCK_H__
#define _DOSPACK_DOSBLOCK_H__

#include "logging.h"
#include "memory.h"
#include "game_env.h"
#include "cpu.h"
#include "int_callback.h"

#define DP_DOS_FILES    127
#define DP_DOS_DRIVES   26
#define DP_DOS_DEVICES  10

/* Special segment numbers: */

#define DP_DOS_INFOBLOCK_SEG       0x0080	// sysvars (list of lists)
#define DP_DOS_CONDRV_SEG          0x00a0
#define DP_DOS_CONSTRING_SEG       0x00a8

#define DP_DOS_SDA_SEG             0x00b2	// dos swappable area
#define DP_DOS_SDA_OFS             0

#define DP_DOS_CDS_SEG             0x0108
#define DP_DOS_FIRST_SHELL         0x0118

#define DP_DOS_MEM_START           0x016f // First segment that DOS can use
#define DP_DOS_MEM_END             0x9ffe // Last segment available for apps

#define DP_DOS_PRIVATE_SEGMENT     0xc800
#define DP_DOS_PRIVATE_SEGMENT_END 0xd000

/* Structs */

struct dos_command_desc {
	u8 count;				/* number of bytes returned */
	u8 buffer[127];			/* the buffer itself */
} __attribute__((packed));

struct dos_date {
	u16 year;
	u8 month;
	u8 day;
};

struct dos_version {
	u8 major, minor, revision;
};

struct dos_info_block {
	u8 unknown1[4];
	u16 magicWord;	// -0x22 needs to be 1
	u8 unknown2[8];
	u16 regCXfrom5e;	// -0x18 CX from last int21/ah=5e
	u16 countLRUcache;	// -0x16 LRU counter for FCB caching
	u16 countLRUopens;	// -0x14 LRU counter for FCB openings
	u8 stuff[6];	// -0x12 some stuff, hopefully never used....
	u16 sharingCount;	// -0x0c sharing retry count
	u16 sharingDelay;	// -0x0a sharing retry delay
	real_pt_addr_t diskBufPtr;	// -0x08 pointer to disk buffer
	u16 ptrCONinput;	// -0x04 pointer to con input
	u16 firstMCB;	// -0x02 first memory control block
	real_pt_addr_t firstDPB;	//  0x00 first drive parameter block
	real_pt_addr_t firstFileTable;	//  0x04 first system file table
	real_pt_addr_t activeClock;	//  0x08 active clock device header
	real_pt_addr_t activeCon;	//  0x0c active console device header
	u16 maxSectorLength;	//  0x10 maximum bytes per sector of any block device;
	real_pt_addr_t diskInfoBuffer;	//  0x12 pointer to disk info buffer
	real_pt_addr_t curDirStructure;	//  0x16 pointer to current array of directory structure
	real_pt_addr_t fcbTable;	//  0x1a pointer to system FCB table
	u16 protFCBs;	//  0x1e protected fcbs
	u8 blockDevices;	//  0x20 installed block devices
	u8 lastdrive;	//  0x21 lastdrive
	u32 nulNextDriver;	//  0x22 NUL driver next pointer
	u16 nulAttributes;	//  0x26 NUL driver aattributes
	u32 nulStrategy;	//  0x28 NUL driver strategy routine
	u8 nulString[8];	//  0x2c NUL driver name string
	u8 joindedDrives;	//  0x34 joined drives
	u16 specialCodeSeg;	//  0x35 special code segment
	real_pt_addr_t setverPtr;	//  0x37 pointer to setver
	u16 a20FixOfs;	//  0x3b a20 fix routine offset
	u16 pspLastIfHMA;	//  0x3d psp of last program (if dos in hma)
	u16 buffers_x;	//  0x3f x in BUFFERS x,y
	u16 buffers_y;	//  0x41 y in BUFFERS x,y
	u8 bootDrive;	//  0x43 boot drive
	u8 useDwordMov;	//  0x44 use dword moves
	u16 extendedSize;	//  0x45 size of extended memory
	u32 diskBufferHeadPt;	//  0x47 pointer to least-recently used buffer header
	u16 dirtyDiskBuffers;	//  0x4b number of dirty disk buffers
	u32 lookaheadBufPt;	//  0x4d pointer to lookahead buffer
	u16 lookaheadBufNumber;	//  0x51 number of lookahead buffers
	u8 bufferLocation;	//  0x53 workspace buffer location
	u32 workspaceBuffer;	//  0x54 pointer to workspace buffer
	u8 unknown3[11];	//  0x58
	u8 chainingUMB;	//  0x63 bit0: UMB chain linked to MCB chain
	u16 minMemForExec;	//  0x64 minimum paragraphs needed for current program
	u16 startOfUMBChain;	//  0x66 segment of first UMB-MCB
	u16 memAllocScanStart;	//  0x68 start paragraph for memory allocation
} __attribute__((packed));

#define DOS_PSP_MAX_FILES 20

struct dos_psp {
	u8	exit[2];			/* CP/M-like exit poimt */
	u16	next_seg;			/* Segment of first byte beyond memory allocated or program */
	u8	fill_1;			/* single char fill */
	u8	far_call;			/* far call opcode */
	real_pt_addr_t	cpm_entry;			/* CPM Service Request address*/
	real_pt_addr_t	int_22;				/* Terminate Address */
	real_pt_addr_t	int_23;				/* Break Address */
	real_pt_addr_t	int_24;				/* Critical Error Address */
	u16	psp_parent;			/* Parent PSP Segment */
	u8	files[DOS_PSP_MAX_FILES];	/* File Table - 0xff is unused */
	u16	environment;			/* Segment of evironment table */
	real_pt_addr_t	stack;				/* SS:SP Save point for int 0x21 calls */
	u16	max_files;			/* Maximum open files */
	real_pt_addr_t	file_table;			/* Pointer to File Table PSP:0x18 */
	real_pt_addr_t	prev_psp;			/* Pointer to previous PSP */
	u8	interim_flag;
	u8	truename_flag;
	u16	nn_flags;
	u16	dos_version;
	u8	fill_2[14];			/* Lot's of unused stuff i can't care aboue */
	u8	service[3];			/* INT 0x21 Service call int 0x21;retf; */
	u8	fill_3[9];			/* This has some blocks with FCB info */
	u8	fcb1[16];			/* first FCB */
	u8	fcb2[16];			/* second FCB */
	u8	fill_4[4];			/* unused */
	struct dos_command_desc cmdtail;
} __attribute__((packed));

struct dos_sda {
	u8 crit_error_flag;	/* 0x00 Critical Error Flag */
	u8 inDOS_flag;	/* 0x01 InDOS flag (count of active INT 21 calls) */
	u8 drive_crit_error;	/* 0x02 Drive on which current critical error occurred or FFh */
	u8 locus_of_last_error;	/* 0x03 locus of last error */
	u16 extended_error_code;	/* 0x04 extended error code of last error */
	u8 suggested_action;	/* 0x06 suggested action for last error */
	u8 error_class;	/* 0x07 class of last error */
	u32 last_error_pointer;	/* 0x08 ES:DI pointer for last error */
	u32 current_dta;	/* 0x0C current DTA (Disk Transfer Address) */
	u16 current_psp;	/* 0x10 current PSP */
	u16 sp_int_23;	/* 0x12 stores SP across an INT 23 */
	u16 return_code;	/* 0x14 return code from last process termination (zerod after reading with AH=4Dh) */
	u8 current_drive;	/* 0x16 current drive */
	u8 extended_break_flag;	/* 0x17 extended break flag */
	u8 fill[2];	/* 0x18 flag: code page switching || flag: copy of previous byte in case of INT 24 Abort */
} __attribute__((packed));

struct dos_exec_block {
	u16 envseg;
	real_pt_addr_t cmdtail;
	real_pt_addr_t fcb1;
	real_pt_addr_t fcb2;
	real_pt_addr_t initsssp;
	real_pt_addr_t initcsip;
} __attribute__((packed));

#define DP_MCB_TYPE_END    0x5a
#define DP_MCB_TYPE_CONT   0x4d

#define DP_MCB_PSP_FREE    0x0000
#define DP_MCB_PSP_DOS     0x0008

struct dos_mcb {
	u8 type;
	u16 psp_segment;
	u16 size;
	u8 unused[3];
	u8 filename[8];
} __attribute__((packed));

struct dos_psp_desc {
	real_pt_addr_t addr;
	u16 seg;
};

struct dp_dosblock {
	struct dos_version version;

	u32 next_priv_seg; /* Used for allocation of private DOS memory pools */
	u8 current_drive;
	u16 first_mcb;

	struct dos_info_block *infoblockp;
	struct dos_sda *sdap;

	/* some tables */
	real_pt_addr_t mediaid;
	real_pt_addr_t tempdta;
	real_pt_addr_t tempdta_fcbdelete;
	real_pt_addr_t dbcs;
	real_pt_addr_t filenamechar;
	real_pt_addr_t collatingseq;
	real_pt_addr_t upcase;

	char _marshal_sep[0];

	struct dp_memory *memory;
	struct dp_cpu *cpu;
	struct dp_logging *logging;
	struct dp_int_callback *int_callback;
	struct dp_timetrack *timetrack;
};

void dp_dosblock_init(struct dp_dosblock *dosblock,
		      struct dp_memory *memory,
		      struct dp_logging *logging,
		      struct dp_int_callback *int_callback,
		      struct dp_cpu *cpu,
		      struct dp_marshal *marshal,
		      struct dp_timetrack *timetrack);
void dp_dosblock_marshal(struct dp_dosblock *dosblock, struct dp_marshal *marshal);
void dp_dosblock_unmarshal(struct dp_dosblock *dosblock, struct dp_marshal *marshal);
void dp_dosblock_load(struct dp_dosblock *dosblock,
		      struct dp_game_env *game_env);

#endif
