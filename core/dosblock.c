#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_DOSBLOCK
#define DP_LOGGING           (dosblock->logging)

#include <string.h>

#include "cpu_inlines.h"
#include "dosblock.h"
#include "games.h"
#include "game_env.h"

#define DOSERR_NONE 0
#define DOSERR_FUNCTION_NUMBER_INVALID 1
#define DOSERR_FILE_NOT_FOUND 2
#define DOSERR_PATH_NOT_FOUND 3
#define DOSERR_TOO_MANY_OPEN_FILES 4
#define DOSERR_ACCESS_DENIED 5
#define DOSERR_INVALID_HANDLE 6
#define DOSERR_MCB_DESTROYED 7
#define DOSERR_INSUFFICIENT_MEMORY 8
#define DOSERR_MB_ADDRESS_INVALID 9
#define DOSERR_ENVIRONMENT_INVALID 10
#define DOSERR_FORMAT_INVALID 11
#define DOSERR_ACCESS_CODE_INVALID 12
#define DOSERR_DATA_INVALID 13
#define DOSERR_RESERVED 14
#define DOSERR_FIXUP_OVERFLOW 14
#define DOSERR_INVALID_DRIVE 15
#define DOSERR_REMOVE_CURRENT_DIRECTORY 16
#define DOSERR_NOT_SAME_DEVICE 17
#define DOSERR_NO_MORE_FILES 18
#define DOSERR_FILE_ALREADY_EXISTS 80

#define DOSNAMEBUF 256

static u32 dp_dosblock_default_handler(void *ptr)
{
	// struct dp_dosblock *dosblock = ptr;

	// DP_DBG("unhandled default");

	return DP_CALLBACK_NONE;

}

static u32 dp_dosblock_int20handler(void *ptr)
{
	struct dp_dosblock *dosblock = ptr;

	DP_FAT("unhandled int 20");

	return DP_CALLBACK_NONE;
}

static void dp_dosblock_int21_get_dos_version(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	DP_TRC_DBG("AL=%d", dcr_reg_al);

	if (dcr_reg_al == 0)
		dcr_reg_bh = 0xFF;	/* Fake Microsoft DOS */
	if (dcr_reg_al == 1)
		dcr_reg_bh = 0x10;	/* DOS is in HMA */
	dcr_reg_al = dosblock->version.major;
	dcr_reg_ah = dosblock->version.minor;
	/* Serialnumber */
	dcr_reg_bl = 0x00;
	dcr_reg_cx = 0x0000;
}

static void dp_dosblock_int21_get_interrupt_vector(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	u16 ofs, seg;
	DP_TRC_DBG("AL=%d", dcr_reg_al);

	ofs = dp_realv_readw(dosblock->memory, 0, ((u32)dcr_reg_al) * 4);
	seg = dp_realv_readw(dosblock->memory, 0, ((u32)dcr_reg_al) * 4 + 2);

	DP_TRC_DBG("addr=%04x%0x", seg, ofs);

	dcr_reg_bx = ofs;
	dp_seg_set16(&dcr_reg_es, seg);
}

static void dp_dosblock_int21_set_interrupt_vector(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	real_pt_addr_t real_addr = real_make(dcr_reg_ds.val, dcr_reg_dx);

	DP_TRC_DBG("AL=%d, addr=%08x", dcr_reg_al, real_addr);

	dp_memp_set_realvec(dosblock->memory, dcr_reg_al, real_make(dcr_reg_ds.val, dcr_reg_dx));
}

static enum dp_bool dp_dosblock_mem_resize(struct dp_dosblock *dosblock, u16 seg, u16 *new_size);

static void dp_dosblock_int21_resize_memory_block(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	u16 new_size;
	enum dp_bool ret;

	DP_TRC_DBG("BX=0x%x, ES=0x%x", dcr_reg_bx, dcr_reg_es.val);

	new_size = dcr_reg_bx;

	ret = dp_dosblock_mem_resize(dosblock, dcr_reg_es.val, &new_size);
	if (ret == DP_TRUE) {
		dcr_reg_ax = dcr_reg_es.val;
		dp_cpu_callback_scf(cpu, DP_FALSE);
	} else {
		dcr_reg_ax = DOSERR_INSUFFICIENT_MEMORY;
		dcr_reg_bx = new_size;
		DP_TRC_DBG("insufficient memory: BX=0x%x", dcr_reg_bx);
		dp_cpu_callback_scf(cpu, DP_TRUE);
	}
}

static void dp_dosblock_int21_ioctl(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	DP_TRC_WRN("not implemented AL=0x%x BX=0x%x", dcr_reg_al, dcr_reg_bx);

	switch (dcr_reg_ah) {
	default:
		break;
	}

	dp_cpu_callback_scf(cpu, DP_FALSE);
}

static void dp_dosblock_int21_write(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	phys_addr_t phys = dcr_reg_ds.phys + dcr_reg_dx;
	u16 bytes_to_write = dcr_reg_cx, b_size, ofs = 0;
	char chunk[0x20] = {0, };
	int i;

	DP_WRN("dos write syscall went to nul");

	while (bytes_to_write > 0) {
		b_size = bytes_to_write;
		if (b_size > sizeof(chunk) - 1)
			b_size = sizeof(chunk) - 1;

		dp_memv_block_read(dosblock->memory, phys, chunk, b_size);
		chunk[b_size] = '\0';
		for (i = 0; i < b_size; i++) {
			if (!(chunk[i] >= 32  &&  chunk[i] < 128))
				chunk[i] = '.';
		}

		DP_DBG("dos write: at: %d \"%s\"", ofs, chunk);

		phys += b_size;
		bytes_to_write -= b_size;
		ofs += b_size;
	}
}

static void dp_dosblock_int21_read(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	DP_WRN("dos read syscall returns nothing with success");

	dcr_reg_ax = 0;
	dp_cpu_callback_scf(cpu, DP_FALSE);
}

static void dp_dosblock_int21_open_existing_file(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	char dosname[DOSNAMEBUF];
	phys_addr_t phys = dcr_reg_ds.phys + dcr_reg_dx;
	u8 mode = dcr_reg_al;

	dp_memv_readstr(dosblock->memory, phys, dosname, sizeof(dosname));

	DP_INF("open existing file %s, modes=%d", dosname, mode);
	DP_WRN("open existing file always succeeding");

	dcr_reg_ax = 5;
	dp_cpu_callback_scf(cpu, DP_FALSE);
}

static void dp_dosblock_int21_close_file(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	DP_DBG("BX=0x%02x", dcr_reg_bx);

	DP_WRN("close file always succeeding");

	dp_cpu_callback_scf(cpu, DP_FALSE);
}

static u32 dp_dosblock_int21_terminate(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	DP_INF("DOS program terminated");

	return DP_CALLBACK_STOP;
}

static u32 dp_dosblock_int21_seek_file(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	u32 pos = (((u32)dcr_reg_cx) << 16) | dcr_reg_dx;
	u16 fhandle = dcr_reg_bx;
	u8 whence = dcr_reg_al;

	DP_DBG("file seek: file_handle=%d, pos=%d, whence=%d", fhandle, pos, whence);
	DP_WRN("file seek always succeeding");

	return DP_CALLBACK_STOP;
}

static u32 dp_dosblock_int21_get_system_date(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	int year = 0, month = 0, day = 0;

	dp_timetrack_get_timestamp(dosblock->timetrack, &year, &month, &day,
				   NULL, NULL, NULL, NULL);

	dcr_reg_cx = year;
	dcr_reg_dh = month;
	dcr_reg_dl = day;

	DP_DBG("get date: %d.%d.%d", day, month, year);

	return DP_CALLBACK_NONE;
}

static u32 dp_dosblock_int21_get_system_time(struct dp_dosblock *dosblock, struct dp_cpu *cpu)
{
	int hour = 0, minute = 0, second = 0, usec =0;

	dp_timetrack_get_timestamp(dosblock->timetrack, NULL, NULL, NULL,
				   &hour, &minute, &second, &usec);

	dcr_reg_ch = hour;
	dcr_reg_cl = minute;
	dcr_reg_dh = second;
	dcr_reg_dl = usec / 10000;

	DP_DBG("get time: %02d:%02d:%02d.%2d", hour, minute, second, usec / 10000);

	return DP_CALLBACK_NONE;
}

static u32 dp_dosblock_int21handler(void *ptr)
{
	struct dp_dosblock *dosblock = ptr;
	struct dp_cpu *cpu = dosblock->cpu;

	switch (dcr_reg_ah) {
	case 0x25: dp_dosblock_int21_set_interrupt_vector(dosblock, cpu); break;
	case 0x30: dp_dosblock_int21_get_dos_version(dosblock, cpu); break;
	case 0x35: dp_dosblock_int21_get_interrupt_vector(dosblock, cpu); break;
	case 0x3d: dp_dosblock_int21_open_existing_file(dosblock, cpu); break;
	case 0x3e: dp_dosblock_int21_close_file(dosblock, cpu); break;
	case 0x3f: dp_dosblock_int21_read(dosblock, cpu); break;
	case 0x40: dp_dosblock_int21_write(dosblock, cpu); break;
	case 0x42: dp_dosblock_int21_seek_file(dosblock, cpu); break;
	case 0x44: dp_dosblock_int21_ioctl(dosblock, cpu); break;
	case 0x4a: dp_dosblock_int21_resize_memory_block(dosblock, cpu); break;
	case 0x2a: dp_dosblock_int21_get_system_date(dosblock, cpu); break;
	case 0x2c: dp_dosblock_int21_get_system_time(dosblock, cpu); break;
	case 0x4c: return dp_dosblock_int21_terminate(dosblock, cpu); break;
	default:
		DP_FAT("unhandled int 21: AH=%02x", dcr_reg_ah);
		break;
	}

	return DP_CALLBACK_NONE;
}

static u32 dp_dosblock_int25handler(void *ptr)
{
	struct dp_dosblock *dosblock = ptr;

	DP_FAT("unhandled int 25");

	return DP_CALLBACK_NONE;
}

static u32 dp_dosblock_int26handler(void *ptr)
{
	struct dp_dosblock *dosblock = ptr;

	DP_FAT("unhandled int 26");

	return DP_CALLBACK_NONE;
}

static u32 dp_dosblock_int27handler(void *ptr)
{
	struct dp_dosblock *dosblock = ptr;

	DP_FAT("unhandled int 27");

	return DP_CALLBACK_NONE;
}

static u32 dp_dosblock_int28handler(void *ptr)
{
	struct dp_dosblock *dosblock = ptr;

	DP_FAT("unhandled int 28");

	return DP_CALLBACK_NONE;
}

static u32 dp_dosblock_int29handler(void *ptr)
{
	struct dp_dosblock *dosblock = ptr;

	DP_FAT("unhandled int 29");

	return DP_CALLBACK_NONE;
}

static u32 dp_dosblock_int2ahandler(void *ptr)
{
	struct dp_dosblock *dosblock = ptr;

	DP_FAT("unhandled int 2a");

	return DP_CALLBACK_NONE;
}

static u32 dp_dosblock_int2fhandler(void *ptr)
{
	struct dp_dosblock *dosblock = ptr;

	DP_FAT("unhandled int 2f");

	return DP_CALLBACK_NONE;
}

static void dp_dosblock_install_callbacks(struct dp_dosblock *dosblock, struct dp_marshal *marshal)
{
	dp_int_callback_register_inthandler(dosblock->int_callback, 0x20, dp_dosblock_int20handler, dosblock, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_dosblock_int20handler, "dosb_20");
	dp_int_callback_register_inthandler(dosblock->int_callback, 0x21, dp_dosblock_int21handler, dosblock, DP_CB_TYPE_INT21);
	dp_marshal_register_pointee(marshal, dp_dosblock_int21handler, "dosb_21");
	dp_int_callback_register_inthandler(dosblock->int_callback, 0x25, dp_dosblock_int25handler, dosblock, DP_CB_TYPE_RETF);
	dp_marshal_register_pointee(marshal, dp_dosblock_int25handler, "dosb_25");
	dp_int_callback_register_inthandler(dosblock->int_callback, 0x26, dp_dosblock_int26handler, dosblock, DP_CB_TYPE_RETF);
	dp_marshal_register_pointee(marshal, dp_dosblock_int26handler, "dosb_26");
	dp_int_callback_register_inthandler(dosblock->int_callback, 0x27, dp_dosblock_int27handler, dosblock, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_dosblock_int27handler, "dosb_27");
	dp_int_callback_register_inthandler(dosblock->int_callback, 0x28, dp_dosblock_int28handler, dosblock, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_dosblock_int28handler, "dosb_28");
	dp_int_callback_register_inthandler(dosblock->int_callback, 0x29, dp_dosblock_int29handler, dosblock, DP_CB_TYPE_INT29);
	dp_marshal_register_pointee(marshal, dp_dosblock_int29handler, "dosb_29");
	dp_int_callback_register_inthandler(dosblock->int_callback, 0x28, dp_dosblock_int2ahandler, dosblock, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_dosblock_int2ahandler, "dosb_2a");
	dp_int_callback_register_inthandler(dosblock->int_callback, 0x29, dp_dosblock_int2fhandler, dosblock, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_dosblock_int2fhandler, "dosb_2f");
}

static u16 dp_dosblock_priv_alloc(struct dp_dosblock *dosblock, u16 segs)
{
	u16 pos;

	if ((u32)segs + dosblock->next_priv_seg >= DP_DOS_PRIVATE_SEGMENT_END) {
		DP_FAT("cannot allocate %d segs from private block", segs);
	}

	pos = dosblock->next_priv_seg;

	DP_DBG("allocating DOS private area: SEG:0x%x [%d bytes]", pos, segs * 16);

	dosblock->next_priv_seg += segs;
	return pos;
}

static u16 dp_dosblock_priv_allocb(struct dp_dosblock *dosblock, u32 b)
{
	return dp_dosblock_priv_alloc(dosblock, (b+15)/16);
}

static void dp_dosblock_init_infoblock(struct dp_dosblock *dosblock)
{
	u16 seg =  DP_DOS_INFOBLOCK_SEG;
	u16 sftOffset;
	struct dos_info_block *infoblockp;
	struct dp_memory *memory = dosblock->memory;

	infoblockp = dp_memp_get_real_seg(dosblock->memory, seg);
	dosblock->infoblockp = infoblockp;

	/* DIRECT MEMORY ACCESS */

	memset(infoblockp, 0xff, sizeof(*infoblockp));
	memset(infoblockp, 0, 14);

	infoblockp->regCXfrom5e = htole16((u16) 0);
	infoblockp->countLRUcache = htole16((u16) 0);
	infoblockp->countLRUopens = htole16((u16) 0);

	infoblockp->protFCBs = htole16((u16) 0);
	infoblockp->specialCodeSeg = htole16((u16) 0);
	infoblockp->joindedDrives = 0;
	infoblockp->lastdrive = htole16((u8) 0x01);	//increase this if you add drives to cds-chain

	infoblockp->diskInfoBuffer = htole32(real_make(seg, offsetof(struct dos_info_block, diskBufferHeadPt)));
	infoblockp->setverPtr = htole16((u32) 0);

	infoblockp->a20FixOfs = htole16((u16) 0);
	infoblockp->pspLastIfHMA = htole16((u16) 0);
	infoblockp->blockDevices = htole16((u8) 0);

	infoblockp->bootDrive = 0;
	infoblockp->useDwordMov = 1;
	infoblockp->extendedSize = htole16((u16) (dp_mem_size_in_pages(memory)*4 - 1024));
	infoblockp->magicWord = htole16((u16) 0x0001);	// dos5+

	infoblockp->sharingCount = htole16((u16) 0);
	infoblockp->sharingDelay = htole16((u16) 0);
	infoblockp->ptrCONinput = htole16((u16) 0);	// no unread input available
	infoblockp->maxSectorLength = htole16((u16) 0x200);

	infoblockp->dirtyDiskBuffers = htole16((u16) 0);
	infoblockp->lookaheadBufPt = htole32((u32) 0);
	infoblockp->lookaheadBufNumber = htole16((u16) 0);
	infoblockp->bufferLocation = 0;	// buffer in base memory = no workspace
	infoblockp->workspaceBuffer = htole32((u32) 0);

	infoblockp->minMemForExec = htole16((u16) 0);
	infoblockp->memAllocScanStart = htole16((u16) DP_DOS_MEM_START);
	infoblockp->startOfUMBChain = htole16((u16) 0xffff);
	infoblockp->chainingUMB = 0;

	infoblockp->nulNextDriver = htole32((u32) 0xffffffff);
	infoblockp->nulAttributes = htole16((u16) 0x8004);
	infoblockp->nulStrategy = htole32((u32) 0x00000000);

	infoblockp->nulString[0] = (u8) 0x4e;
	infoblockp->nulString[1] = (u8) 0x55;
	infoblockp->nulString[2] = (u8) 0x4c;
	infoblockp->nulString[3] = (u8) 0x20;
	infoblockp->nulString[4] = (u8) 0x20;
	infoblockp->nulString[5] = (u8) 0x20;
	infoblockp->nulString[6] = (u8) 0x20;
	infoblockp->nulString[7] = (u8) 0x20;

	/* Create a fake SFT, so programs think there are 100 file handles */
	sftOffset = offsetof(struct dos_info_block, firstFileTable) + 0xa2;
	infoblockp->firstFileTable = htole32(real_make(seg, sftOffset));

	dp_realv_writed(memory, seg, sftOffset + 0x00, real_make(seg + 0x26, 0));	//Next File Table
	dp_realv_writew(memory, seg, sftOffset + 0x04, 100);	//File Table supports 100 files
	dp_realv_writed(memory, seg + 0x26, 0x00, 0xffffffff);	//Last File Table
	dp_realv_writew(memory, seg + 0x26, 0x04, 100);	//File Table supports 100 files
}

static void dp_dosblock_save_psp_vectors(struct dp_dosblock *dosblock, struct dos_psp *psp)
{
	psp->int_22 = htole32(dp_memp_get_realvec(dosblock->memory, 0x22));
	psp->int_23 = htole32(dp_memp_get_realvec(dosblock->memory, 0x23));
	psp->int_24 = htole32(dp_memp_get_realvec(dosblock->memory, 0x24));
}

#if (0)
static void dp_dosblock_load_psp_vectors(struct dp_dosblock *dosblock, struct dos_psp *psp)
{
	dp_memp_set_realvec(dosblock->memory, 0x22, le32toh(psp->int_22));
	dp_memp_set_realvec(dosblock->memory, 0x23, le32toh(psp->int_23));
	dp_memp_set_realvec(dosblock->memory, 0x24, le32toh(psp->int_24));
}
#endif

static struct dos_psp *dp_dosblock_init_psp(struct dp_dosblock *dosblock, u16 psp_seg, u16 mem_size)
{
	struct dos_psp *psp;
	int ct;

	psp = dp_memp_get_real_seg(dosblock->memory, psp_seg);

	/* DIRECT MEMORY ACCESS */
	memset(psp, 0, sizeof(*psp));
	psp->next_seg = htole16(psp_seg + mem_size);
	/* far call opcode */
	psp->far_call = 0xea;
	// far call to interrupt 0x21 - faked for bill & ted
	// lets hope nobody really uses this address
	psp->cpm_entry = htole32(real_make(0xdead, 0xffff));
	/* Standard blocks,int 20 and int21 retf */
	psp->exit[0] = 0xcd;
	psp->exit[1] = 0x20;
	psp->service[0] = 0xcd;
	psp->service[1] = 0x21;
	psp->service[2] = 0xcb;
	/* psp and psp-parent */
	psp->psp_parent = dosblock->sdap->current_psp;
	psp->prev_psp = le32toh(0xffffffff);
	psp->dos_version = htole32(dosblock->version.major);
	/* terminate 22, break 23, crititcal error 24 address stored */

	dp_dosblock_save_psp_vectors(dosblock, psp);

	/* FCBs are filled with 0 */

	/* Init file pointer and max_files */
	psp->file_table = real_make(psp_seg, offsetof(struct dos_psp, files));
	psp->max_files = htole16(DOS_PSP_MAX_FILES);

	for (ct = 0; ct < DOS_PSP_MAX_FILES; ct++)
		psp->files[ct] = 0xff;

	return psp;
}

static void dp_dosblock_init_sda(struct dp_dosblock *dosblock)
{
	u16 seg =  DP_DOS_SDA_SEG;
	struct dos_sda *sda = dp_memp_get_real_seg(dosblock->memory, seg);

	/* DIRECT MEMORY ACCESS */

	memset(sda, 0, sizeof(*sda));
	sda->drive_crit_error = 0xff;
	dosblock->sdap = sda;
}

static void dp_dosblock_init_constring_hack(struct dp_dosblock *dosblock)
{
	dp_realv_writed(dosblock->memory, DP_DOS_CONSTRING_SEG, 0x0a, 0x204e4f43);
	dp_realv_writed(dosblock->memory, DP_DOS_CONSTRING_SEG, 0x1a, 0x204e4f43);
	dp_realv_writed(dosblock->memory, DP_DOS_CONSTRING_SEG, 0x2a, 0x204e4f43);
}

static void dp_dosblock_init_condriver(struct dp_dosblock *dosblock)
{
	u32 seg = DP_DOS_CONDRV_SEG;

	dp_realv_writed(dosblock->memory, seg, 0x00, 0xffffffff);	// next ptr
	dp_realv_writew(dosblock->memory, seg, 0x04, 0x8013);		// attributes
	dp_realv_writed(dosblock->memory, seg, 0x06, 0xffffffff);	// strategy routine
	dp_realv_writed(dosblock->memory, seg, 0x0a, 0x204e4f43);	// driver name
	dp_realv_writed(dosblock->memory, seg, 0x0e, 0x20202020);	// driver name

	/* DIRECT MEMORY ACCESS */
	dosblock->infoblockp->nulNextDriver = htole32(real_make(seg, 0));
}

static void dp_dosblock_init_curdir(struct dp_dosblock *dosblock)
{
	u32 seg = DP_DOS_CDS_SEG;

	dp_realv_writed(dosblock->memory, seg, 0x00, 0x005c3a43);

	/* DIRECT MEMORY ACCESS */
	dosblock->infoblockp->curDirStructure = htole32(real_make(seg, 0));
}

static void dp_dosblock_init_tables(struct dp_dosblock *dosblock)
{
	int i;

	dosblock->mediaid = real_make(dp_dosblock_priv_alloc(dosblock, 4), 0);
	dosblock->tempdta = real_make(dp_dosblock_priv_alloc(dosblock, 4), 0);
	dosblock->tempdta_fcbdelete = real_make(dp_dosblock_priv_alloc(dosblock, 4), 0);

	for (i = 0; i < DP_DOS_DRIVES; i++)
		dp_memv_writew(dosblock->memory, real_to_phys(dosblock->mediaid) + i * 2, 0);

	DP_DBG("initializing info block");

	dp_dosblock_init_infoblock(dosblock);
	dp_dosblock_init_sda(dosblock);
	dp_dosblock_init_constring_hack(dosblock);
	dp_dosblock_init_condriver(dosblock);
	dp_dosblock_init_curdir(dosblock);

	/* FIXME: NOT DONE */

#if (0)
	/* DOSBOX CUT */

	/* Allocate DCBS DOUBLE BYTE CHARACTER SET LEAD-BYTE TABLE */
	dos.tables.dbcs = real_make(DOS_GetMemory(12), 0);
	dp_memv_writed(cpu->memory, Real2Phys(dos.tables.dbcs), 0);	//empty table
	/* FILENAME CHARACTER TABLE */
	dos.tables.filenamechar = real_make(DOS_GetMemory(2), 0);
	dp_memv_writew(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x00, 0x16);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x02, 0x01);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x03, 0x00);	// allowed chars from
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x04, 0xff);	// ...to
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x05, 0x00);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x06, 0x00);	// excluded chars from
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x07, 0x20);	// ...to
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x08, 0x02);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x09, 0x0e);	// number of illegal separators
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x0a, 0x2e);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x0b, 0x22);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x0c, 0x2f);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x0d, 0x5c);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x0e, 0x5b);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x0f, 0x5d);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x10, 0x3a);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x11, 0x7c);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x12, 0x3c);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x13, 0x3e);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x14, 0x2b);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x15, 0x3d);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x16, 0x3b);
	dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.filenamechar) + 0x17, 0x2c);
	/* COLLATING SEQUENCE TABLE + UPCASE TABLE */
	// 256 bytes for col table, 128 for upcase, 4 for number of entries
	dos.tables.collatingseq = real_make(DOS_GetMemory(25), 0);
	dp_memv_writew(cpu->memory, Real2Phys(dos.tables.collatingseq), 0x100);
	for (i = 0; i < 256; i++)
		dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.collatingseq) + i + 2, i);
	dos.tables.upcase = dos.tables.collatingseq + 258;
	dp_memv_writew(cpu->memory, Real2Phys(dos.tables.upcase), 0x80);
	for (i = 0; i < 128; i++)
		dp_memv_writeb(cpu->memory, Real2Phys(dos.tables.upcase) + i + 2, 0x80 + i);

	/* Create a fake FCB SFT */
	seg = DOS_GetMemory(4);
	real_writed(seg, 0, 0xffffffff);	//Last File Table
	real_writew(seg, 4, 100);	//File Table supports 100 files
	dos_infoblock.SetFCBTable(real_make(seg, 0));

	/* Create a fake DPB */
	dos.tables.dpb = DOS_GetMemory(2);
	for (Bitu d = 0; d < 26; d++)
		real_writeb(dos.tables.dpb, d, d);

	/* Create a fake disk buffer head */
	seg = DOS_GetMemory(6);
	for (Bitu ct = 0; ct < 0x20; ct++)
		real_writeb(seg, ct, 0);
	real_writew(seg, 0x00, 0xffff);	// forward ptr
	real_writew(seg, 0x02, 0xffff);	// backward ptr
	real_writeb(seg, 0x04, 0xff);	// not in use
	real_writeb(seg, 0x0a, 0x01);	// number of FATs
	real_writed(seg, 0x0d, 0xffffffff);	// pointer to DPB
	dos_infoblock.SetDiskBufferHeadPt(real_make(seg, 0));

	/* Set buffers to a nice value */
	dos_infoblock.SetBuffers(50, 50);

	/* case map routine INT 0x21 0x38 */
	call_casemap = CALLBACK_Allocate();
	CALLBACK_Setup(call_casemap, DOS_CaseMapFunc, CB_RETF, "DOS CaseMap");
	/* Add it to country structure */
	host_writed(country_info + 0x12, dp_cb_index_to_realaddr(call_casemap));
	dos.tables.country = country_info;
#endif
}

static void dp_dosblock_compact_mcbs(struct dp_dosblock *dosblock)
{
	u16 mcb_segment = dosblock->first_mcb;
	struct dos_mcb *mcb, *mcb_next;

	mcb = dp_memp_get_real_seg(dosblock->memory, mcb_segment);

	/* DIRECT MEMORY ACCESS */

	while (mcb->type != DP_MCB_TYPE_END) {
		mcb_next = dp_memp_get_real_seg(dosblock->memory, mcb_segment + le16toh(mcb->size) + 1);

		if (mcb->psp_segment == 0  &&  mcb_next->psp_segment == 0) {
			mcb->size = htole16(le16toh(mcb->size) + le16toh(mcb_next->size) + 1);
			mcb->type = mcb_next->type;
		} else {
			mcb_segment += le16toh(mcb->size) + 1;
			mcb = dp_memp_get_real_seg(dosblock->memory, mcb_segment);
		}
	}
}

static u16 dp_dosblock_mem_get_largest_block(struct dp_dosblock *dosblock)
{
	u16 mcb_segment = dosblock->first_mcb;
	struct dos_mcb *mcb;
	u16 largest_block = 0;

	mcb = dp_memp_get_real_seg(dosblock->memory, mcb_segment);

	/* DIRECT MEMORY ACCESS */

	for (;;) {
		if (mcb->psp_segment == htole16(DP_MCB_PSP_FREE))
			if (le16toh(mcb->size) > largest_block)
				largest_block = le16toh(mcb->size);
		if (mcb->type == DP_MCB_TYPE_END)
			break;

		mcb_segment += le16toh(mcb->size) + 1;
		mcb = dp_memp_get_real_seg(dosblock->memory, mcb_segment);
	}

	/* TODO: Support UMB */

	return largest_block;
}

static u16 dp_dosblock_mem_allocate(struct dp_dosblock *dosblock, u16 psp_seg, u16 segs)
{
	u16 mcb_segment = dosblock->first_mcb;
	struct dos_mcb *mcb, *split_mcb;

	dp_dosblock_compact_mcbs(dosblock);

	mcb = dp_memp_get_real_seg(dosblock->memory, mcb_segment);

	/* DIRECT MEMORY ACCESS */

	for (;;) {
		if (mcb->psp_segment == htole16(DP_MCB_PSP_FREE)) {
			u16 old_size = le16toh(mcb->size);
			u8 old_type = mcb->type;

			if (segs <= old_size) {
				if (psp_seg == 0)
					psp_seg = mcb_segment;
				mcb->psp_segment = htole16(psp_seg);

				if (segs < old_size) {
					mcb->size = htole16(segs);
					mcb->type = DP_MCB_TYPE_CONT;
					split_mcb = dp_memp_get_real_seg(dosblock->memory,
									 mcb_segment + 1 + segs);
					memset(split_mcb, 0, sizeof(*split_mcb));
					split_mcb->type = old_type;
					split_mcb->psp_segment = htole16(DP_MCB_PSP_FREE);
					split_mcb->size = htole16(old_size - 1 - segs);
				}

				return mcb_segment + 1;
			}
		}

		if (mcb->type == DP_MCB_TYPE_END)
			break;

		mcb_segment += le16toh(mcb->size) + 1;
		mcb = dp_memp_get_real_seg(dosblock->memory, mcb_segment);
	}

	/* TODO: Support UMB */

	return 0;
}

static enum dp_bool dp_dosblock_mem_resize(struct dp_dosblock *dosblock, u16 seg, u16 *new_size)
{
	struct dos_mcb *mcb, *split_mcb;

	DP_DBG("mem_resize: seg %d: new size %d", seg, *new_size);

	dp_dosblock_compact_mcbs(dosblock);

	/* DIRECT MEMORY ACCESS */

	mcb = dp_memp_get_real_seg(dosblock->memory, seg - 1);
	if (*new_size == le16toh(mcb->size)) {
		DP_DBG("mem_resize: same size");
		return DP_TRUE;
	}

	if (*new_size < le16toh(mcb->size)) {
		DP_DBG("mem_resize: shrinking from size %d, split mcb at %d", le16toh(mcb->size), seg + *new_size);

		split_mcb = dp_memp_get_real_seg(dosblock->memory, seg + *new_size);
		memset(split_mcb, 0, sizeof(*split_mcb));
		split_mcb->type = mcb->type;
		split_mcb->psp_segment = htole16(DP_MCB_PSP_FREE);
		split_mcb->size = htole16(le16toh(mcb->size) - 1 - *new_size);
		mcb->size = htole16(*new_size);
		mcb->type = DP_MCB_TYPE_CONT;
		mcb->psp_segment = dosblock->sdap->current_psp;
		return DP_TRUE;
	} else {
		u16 total = 0, cur_seg = seg;
		u8 last_type;
		struct dos_mcb *next_mcb = mcb, *last_mcb = mcb;

		while (*new_size > total) {
			u16 next_seg, skip;
			if (next_mcb == mcb)
				skip = le16toh(next_mcb->size);
			else
				skip = le16toh(next_mcb->size) + 1;

			total += skip;

			if (next_mcb->type == DP_MCB_TYPE_END)
				break;

			next_seg = cur_seg + skip;
			next_mcb = dp_memp_get_real_seg(dosblock->memory, next_seg);
			if (next_mcb->psp_segment != htole16(DP_MCB_PSP_FREE))
				break;

			cur_seg = next_seg;
			last_mcb = next_mcb;
		}

		if (total < *new_size) {
			DP_WRN("tried to resize above bounds, seg %d: %d < %d", seg, total, *new_size);
			*new_size = total;
			return DP_FALSE;
		}

		DP_DBG("mem_resize: expanding to %d", total);
		last_type = last_mcb->type;

		mcb->size = htole16(*new_size);
		mcb->type = DP_MCB_TYPE_CONT;
		mcb->psp_segment = dosblock->sdap->current_psp;

		split_mcb = dp_memp_get_real_seg(dosblock->memory, seg + *new_size);
		memset(split_mcb, 0, sizeof(*split_mcb));
		split_mcb->type = last_type;
		split_mcb->psp_segment = htole16(DP_MCB_PSP_FREE);
		split_mcb->size = htole16(total - 1 - *new_size);
		return DP_TRUE;
	}
}

void dp_dosblock_mem_free_process(struct dp_dosblock *dosblock, u16 pspseg)
{
	u16 mcb_segment = dosblock->first_mcb;
	struct dos_mcb *mcb;

	mcb = dp_memp_get_real_seg(dosblock->memory, mcb_segment);

	/* DIRECT MEMORY ACCESS */

	for (;;) {
		if (mcb->psp_segment == htole16(pspseg))
			mcb->psp_segment = htole16(DP_MCB_PSP_FREE);
		if (mcb->type == DP_MCB_TYPE_END)
			break;

		mcb_segment = le16toh(mcb->size) + 1;
		mcb = dp_memp_get_real_seg(dosblock->memory, mcb_segment);
	}

	/* TODO: Support UMB */
}

static struct dos_mcb *dp_dosblock_set_mcb(struct dp_dosblock *dosblock, u16 seg, u16 psp_seg, u16 size)
{
	struct dos_mcb *mcb;

	/* DIRECT MEMORY ACCESS */

	mcb = dp_memp_get_real_seg(dosblock->memory, seg);
	mcb->psp_segment = htole16(psp_seg);
	mcb->size = htole16(size);
	mcb->type = DP_MCB_TYPE_CONT;

	return mcb;
}

static struct dos_mcb *dp_dosblock_set_internal_mcb(struct dp_dosblock *dosblock, u16 *seg, u16 size)
{
	struct dos_mcb *mcb;

	/* DIRECT MEMORY ACCESS */

	mcb = dp_memp_get_real_seg(dosblock->memory, *seg);
	mcb->psp_segment = htole16(DP_MCB_PSP_DOS);
	mcb->size = htole16(size);
	mcb->type = DP_MCB_TYPE_CONT;
	*seg += size + 1;

	return mcb;
}

static struct dos_mcb *dp_dosblock_set_free_end_mcb(struct dp_dosblock *dosblock, u16 seg, u16 size)
{
	struct dos_mcb *mcb;

	/* DIRECT MEMORY ACCESS */

	mcb = dp_memp_get_real_seg(dosblock->memory, seg);
	mcb->psp_segment = htole16(DP_MCB_PSP_FREE);
	mcb->size = htole16(size);
	mcb->type = DP_MCB_TYPE_END;

	return mcb;
}

void dp_dosblock_init_mem(struct dp_dosblock *dosblock, struct dp_marshal *marshal)
{
	/* WTF is IH here? */
	u16 ihseg = 0x70;
	u16 ihofs = 0x08;
	struct dos_mcb *mcb_temp;
	u16 seg;

	DP_DBG("initializing dosblock memory");

	/* Let dos claim a few bios interrupts. Makes DOSBox more compatible with
	 * buggy games, which compare against the interrupt table. (probably a
	 * broken linked list implementation) */

	dp_int_callback_register(dosblock->int_callback, dp_dosblock_default_handler, dosblock,
				 DP_CB_TYPE_IRET, phys_make(ihseg, ihofs));
	dp_marshal_register_pointee(marshal, dp_dosblock_default_handler, "dos_def");

	dp_memp_set_realvec(dosblock->memory, 0x01, real_make(ihseg, ihofs));
	dp_memp_set_realvec(dosblock->memory, 0x02, real_make(ihseg, ihofs));
	dp_memp_set_realvec(dosblock->memory, 0x03, real_make(ihseg, ihofs));
	dp_memp_set_realvec(dosblock->memory, 0x04, real_make(ihseg, ihofs));

	seg = DP_DOS_MEM_START;
	dp_dosblock_set_internal_mcb(dosblock, &seg, 1);
	dp_dosblock_set_internal_mcb(dosblock, &seg, 4);
	mcb_temp = dp_dosblock_set_internal_mcb(dosblock, &seg, 16);
	mcb_temp->psp_segment = htole16(0x40); /* WTF? */
	dp_dosblock_set_free_end_mcb(dosblock, seg, DP_DOS_MEM_END - seg);
	dosblock->first_mcb = DP_DOS_MEM_START;
	dosblock->infoblockp->firstMCB = htole16(dosblock->first_mcb);
}

#define DUMMY_SHELL_STACK_SIZE   2048

void dp_dosblock_dummyshell_init(struct dp_dosblock *dosblock)
{
	static char const *const path_string = "PATH=Z:\\";
	static char const *const comspec_string = "COMSPEC=Z:\\COMMAND.COM";
	static char const *const full_name = "Z:\\COMMAND.COM";
#if (0)
	static char const *const init_line = "/INIT AUTOEXEC.BAT";
#endif

	struct dp_cpu *cpu = dosblock->cpu;
	struct dp_memory *memory = dosblock->memory;

	/* Now call up the shell for the first time */
	u16 psp_seg = DP_DOS_FIRST_SHELL;
	u16 env_seg = DP_DOS_FIRST_SHELL + 19; /* WHY? */
	u16 stack_seg;

	stack_seg = dp_dosblock_priv_allocb(dosblock, DUMMY_SHELL_STACK_SIZE);
	dp_seg_set16(&dcr_reg_ss, stack_seg);
	dcr_reg_sp = DUMMY_SHELL_STACK_SIZE - 2;

	/* Set up int 24 and psp (Telarium games) */
	dp_realv_writeb(memory, psp_seg + 16 + 1, 0, 0xea);	/* far jmp */
	dp_realv_writed(memory, psp_seg + 16 + 1, 1, dp_realv_readd(memory, 0, 0x24 * 4));
	dp_realv_writed(memory, 0, 0x24 * 4, ((u32) psp_seg << 16) | ((16 + 1) << 4));

	/* Set up int 23 to "int 20" in the psp. Fixes what.exe */
	dp_realv_writed(memory, 0, 0x23 * 4, ((u32) psp_seg << 16));

	/* Setup MCBs */
	dp_dosblock_set_mcb(dosblock, psp_seg - 1, psp_seg, 0x10 + 2); /* Has something to do with the 19 above? */
	dp_dosblock_set_mcb(dosblock, env_seg - 1, psp_seg, DP_DOS_MEM_START - env_seg);

	/* Setup environment */
	phys_addr_t env_write = phys_make(env_seg, 0);
	dp_memv_block_write(memory, env_write, path_string, (u32) (strlen(path_string) + 1));
	env_write += (phys_addr_t) (strlen(path_string) + 1);
	dp_memv_block_write(memory, env_write, comspec_string, (u32) (strlen(comspec_string) + 1));
	env_write += (phys_addr_t) (strlen(comspec_string) + 1);
	dp_memv_writeb(memory, env_write++, 0);
	dp_memv_writew(memory, env_write, 1);
	env_write += 2;
	dp_memv_block_write(memory, env_write, full_name, (u32) (strlen(full_name) + 1));

	dp_dosblock_init_psp(dosblock, psp_seg, 0);
	dosblock->sdap->current_psp = htole16(psp_seg);

	/* The start of the filetable in the psp must look like this:
	 * 01 01 01 00 02
	 * In order to achieve this: First open 2 files. Close the first and
	 * duplicate the second (so the entries get 01) */

#if (0)
	/* TODO: Setup files and environment */
	u16 dummy = 0;
	DOS_OpenFile("CON", OPEN_READWRITE, &dummy);	/* STDIN  */
	DOS_OpenFile("CON", OPEN_READWRITE, &dummy);	/* STDOUT */
	DOS_CloseFile(0);	/* Close STDIN */
	DOS_ForceDuplicateEntry(1, 0);	/* "new" STDIN */
	DOS_ForceDuplicateEntry(1, 2);	/* STDERR */
	DOS_OpenFile("CON", OPEN_READWRITE, &dummy);	/* STDAUX */
	DOS_OpenFile("CON", OPEN_READWRITE, &dummy);	/* STDPRN */

	psp->psp_parent = htole16(psp_seg);
	/* Set the environment */
	psp.SetEnvironment(env_seg);
	/* Set the command line for the shell start up */
	CommandTail tail;
	tail.count = (u8) strlen(init_line);
	strcpy(tail.buffer, init_line);
	MEM_BlockWrite(phys_make(psp_seg, 128), &tail, 128);
#endif
	dosblock->sdap->current_dta = htole32(real_make(psp_seg, 0x80));

	// AGAIN? dosblock->sdap->current_psp = htole16(psp_seg);
}

void dp_dosblock_init(struct dp_dosblock *dosblock,
		      struct dp_memory *memory,
		      struct dp_logging *logging,
		      struct dp_int_callback *int_callback,
		      struct dp_cpu *cpu,
		      struct dp_marshal *marshal,
		      struct dp_timetrack *timetrack)

{
	dp_marshal_register_pointee(marshal, dosblock, "dosb");

	memset(dosblock, 0, sizeof(*dosblock));
	dosblock->memory = memory;
	dosblock->logging = logging;
	dosblock->int_callback = int_callback;
	dosblock->next_priv_seg = DP_DOS_PRIVATE_SEGMENT;
	dosblock->cpu = cpu;
	dosblock->timetrack = timetrack;

	DP_INF("initializing dosblock");

	dp_dosblock_install_callbacks(dosblock, marshal);
	// TODO: Init files, set virtual drive Z (or C, whatever)
	// TODO: Init devices
	dp_dosblock_init_tables(dosblock);
	dp_dosblock_init_mem(dosblock, marshal);

	// TODO: Set default drive

	dosblock->version.major = 5;
	dosblock->version.minor = 0;

	/* TODO: Pick another date (depending on game) */
	// dosblock->date.day = 1;
	// dosblock->date.month = 11;
	// dosblock->date.year = 1982;

	dp_dosblock_dummyshell_init(dosblock);
}

#if (0)

static void save_regs(struct dp_cpu *cpu)
{
	struct dp_memory *memory = cpu->memory;
	dcr_reg_sp -= 18;
	dp_memv_writew(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 0, dcr_reg_ax);
	dp_memv_writew(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 2, dcr_reg_cx);
	dp_memv_writew(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 4, dcr_reg_dx);
	dp_memv_writew(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 6, dcr_reg_bx);
	dp_memv_writew(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 8, dcr_reg_si);
	dp_memv_writew(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 10, dcr_reg_di);
	dp_memv_writew(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 12, dcr_reg_bp);
	dp_memv_writew(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 14, dcr_reg_ds.val);
	dp_memv_writew(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 16, dcr_reg_es.val);
}

static void restore_regs(struct dp_cpu *cpu)
{
	struct dp_memory *memory = cpu->memory;

	dcr_reg_ax = dp_memv_readw(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 0);
	dcr_reg_cx = dp_memv_readw(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 2);
	dcr_reg_dx = dp_memv_readw(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 4);
	dcr_reg_bx = dp_memv_readw(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 6);
	dcr_reg_si = dp_memv_readw(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 8);
	dcr_reg_di = dp_memv_readw(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 10);
	dcr_reg_bp = dp_memv_readw(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 12);
	dp_seg_set16(&dcr_reg_ds, dp_memv_readw(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 14));
	dp_seg_set16(&dcr_reg_es, dp_memv_readw(memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 16));
	dcr_reg_sp += 18;
}
#endif

struct dp_dos_exe_header {
	u16 signature;	/* EXE Signature MZ or ZM */
	u16 extrabytes;	/* Bytes on the last page */
	u16 pages;		/* Pages in file */
	u16 relocations;	/* Relocations in file */
	u16 headersize;	/* Paragraphs in header */
	u16 minmemory;	/* Minimum amount of memory */
	u16 maxmemory;	/* Maximum amount of memory */
	u16 initSS;
	u16 initSP;
	u16 checksum;
	u16 initIP;
	u16 initCS;
	u16 reloctable;
	u16 overlay;
} __attribute__((packed));

#define DP_DOS_EXE_MAGIC 0x5a4d

void dp_dosblock_load(struct dp_dosblock *dosblock, struct dp_game_env *game_env)
{
	char exe_name[0x20] = {0, }; /* FIXME: length */
	struct dp_game *game_desc = game_env->game_desc;
	struct dp_cpu *cpu = dosblock->cpu;
	const char *cmd = game_env->game_desc->command_line;
	struct dp_fnode *node;
	struct dp_fregfile *regfile;
	const char *p;
	enum dp_bool comfile = DP_FALSE;
	struct dp_dos_exe_header exe_hdr;
	int l;
	u32 headersize, imagesize, maxsize;
	s32 s;
	u32 max_allocatable;
	u16 psp_seg;
	u16 load_seg;
	void *memptr;
	struct dos_psp *old_psp, *new_psp;
	real_pt_addr_t csip, sssp;

	p = strchr(cmd, ' ');
	if (p)
		l = p - cmd;
	else
		l = strlen(cmd);
	if (l > sizeof(exe_name) - 1)
		l = sizeof(exe_name) -1;

	memcpy(exe_name, cmd, l);

	node = dp_files_dir_lookup_name(game_desc->vfs_root, exe_name);
	if (node->type != DP_FNODE_TYPE_REGFILE) {
		DP_FAT("%s not a file", exe_name);
		return;
	}

	regfile = node->u.regfile;

	if (dp_file_get_size(game_env, regfile) < sizeof(exe_hdr)) {
		comfile = DP_TRUE;
	} else {
		s = dp_file_read(game_env, regfile, 0, &exe_hdr, sizeof(exe_hdr));
		if (s != sizeof(exe_hdr)) {
			DP_FAT("error reading EXE header");
			return;
		}

		if (DP_DOS_EXE_MAGIC != le16toh(exe_hdr.signature)) {
			comfile = DP_FALSE;
		}
	}

	if (comfile) {
		DP_FAT("COM files not supported yet");
		return;
	}

	s = le16toh(exe_hdr.pages);
	if (s > 0x7ff) {
		DP_FAT("EXE file too big (%d pages)", s);
		return;
	}

	headersize = le16toh(exe_hdr.headersize) * 16;
	imagesize = le16toh(exe_hdr.pages) * 512 - headersize;
	if (imagesize + headersize < 512)
		imagesize = 512 - headersize;

	DP_DBG("header size: %d, image size: %d, maxmemory: %d", headersize, imagesize, le16toh(exe_hdr.maxmemory));
	maxsize = (((imagesize + (le16toh(exe_hdr.maxmemory)*16))+15))/16;
	max_allocatable = dp_dosblock_mem_get_largest_block(dosblock);

	DP_DBG("max size: %d, max_allocatable: %d", maxsize, max_allocatable);
	if (max_allocatable < maxsize)
		maxsize = max_allocatable;

	if ((exe_hdr.minmemory == 0) && (exe_hdr.maxmemory == 0)) {
		DP_FAT("loading to upper of allocation not supported");
	}

	psp_seg = dp_dosblock_mem_allocate(dosblock, 0, maxsize);
	load_seg = psp_seg + 16;

	DP_DBG("PSP at seg: 0x%x", psp_seg);

	memptr = dp_memp_get_real_seg(dosblock->memory, load_seg);
	s = dp_file_read(game_env, regfile, headersize, memptr, imagesize);

	if (s >= 0  &&  s < imagesize) {
		DP_DBG("loaded image size: 0x%x", s);
	} else {
		DP_FAT("failed loading file");
	}

	if (le16toh(exe_hdr.relocations) > 0) {
		DP_FAT("relocations not supported");
	}

	/* Setup PSP and crap */

	dp_dosblock_init_psp(dosblock, psp_seg, maxsize);

	if ((dcr_reg_sp > 0xfffe) || (dcr_reg_sp < 18)) {
		DP_FAT("invalid SP in load");
	}

	/* TODO: CLEAR THE CARRY FLAG */

	csip = real_make(load_seg + le16toh(exe_hdr.initCS), le16toh(exe_hdr.initIP));
	sssp = real_make(load_seg + le16toh(exe_hdr.initSS), le16toh(exe_hdr.initSP));

	DP_DBG("CS:IP=%08x, SSSP:%08x", csip, sssp);

	old_psp = dp_memp_get_real_seg(dosblock->memory, le16toh(dosblock->sdap->current_psp));
	new_psp = dp_memp_get_real_seg(dosblock->memory, psp_seg);
	old_psp->stack = htole32(real_make(dcr_reg_ss.val, dcr_reg_sp));
	dosblock->sdap->current_psp = htole16(psp_seg);
	dosblock->sdap->current_dta = htole32(real_make(psp_seg, 0x80));
	dp_dosblock_save_psp_vectors(dosblock, new_psp);

	/* TODO: Copy FCBs */

	dp_seg_set16(&dcr_reg_ss, real_to_seg(sssp));
	dcr_reg_sp = real_to_offset(sssp);
	dp_seg_set16(&dcr_reg_cs, real_to_seg(csip));
	dcr_reg_ip = real_to_offset(csip);
	dp_seg_set16(&dcr_reg_ds, psp_seg);
	dp_seg_set16(&dcr_reg_es, psp_seg);

	/* DOS starts programs with a RETF, so critical flags
	 * should not be modified (IOPL in v86 mode);
	 * interrupt flag is set explicitly, test flags cleared */

	dcr_reg_flags = (dcr_reg_flags & (~DC_FMASK_TEST)) | DC_FLAG_IF;
	dcr_reg_ax = dcr_reg_bx = 0;
	dcr_reg_cx = 0xff;
	dcr_reg_dx = psp_seg;
	dcr_reg_si = real_to_offset(csip);
	dcr_reg_di = real_to_offset(sssp);
	dcr_reg_bp = 0x91c;	/* DOS internal stack begin relict */

	/* TODO: Set up environment and command line args */
}

void dp_dosblock_marshal(struct dp_dosblock *dosblock, struct dp_marshal *marshal)
{
	dp_marshal_write_version(marshal, 1);
	dp_marshal_write(marshal, dosblock, offsetof(struct dp_dosblock, _marshal_sep));
}

void dp_dosblock_unmarshal(struct dp_dosblock *dosblock, struct dp_marshal *marshal)
{
	u32 version = 0;
	dp_marshal_read_version(marshal, &version);
	DP_ASSERT(version == 1);
	dp_marshal_read(marshal, dosblock, offsetof(struct dp_dosblock, _marshal_sep));

	dp_marshal_read_ptr_fix(marshal, (void **)&dosblock->infoblockp);
	dp_marshal_read_ptr_fix(marshal, (void **)&dosblock->sdap);
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
