#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_BIOS
#define DP_LOGGING           (bios->logging)

#include <string.h>

#include "bios.h"

#define BIOS_BASE_ADDRESS_COM1          0x400
#define BIOS_BASE_ADDRESS_COM2          0x402
#define BIOS_BASE_ADDRESS_COM3          0x404
#define BIOS_BASE_ADDRESS_COM4          0x406
#define BIOS_ADDRESS_LPT1               0x408
#define BIOS_ADDRESS_LPT2               0x40a
#define BIOS_ADDRESS_LPT3               0x40c
/* 0x40e is reserved */
#define BIOS_CONFIGURATION              0x410
/* 0x412 is reserved */
#define BIOS_MEMORY_SIZE                0x413
#define BIOS_TRUE_MEMORY_SIZE           0x415
/* #define bios_expansion_memory_size      (*(unsigned int   *) 0x415) */

/* used for keyboard input with Alt-Number */
/* #define bios_keyboard_buffer            (*(unsigned int   *) 0x41e) */
#define BIOS_DRIVE_ACTIVE               0x43e
#define BIOS_DRIVE_RUNNING              0x43f
#define BIOS_DISK_MOTOR_TIMEOUT         0x440
#define BIOS_DISK_STATUS                0x441
/* #define bios_fdc_result_buffer          (*(unsigned short *) 0x442) */
#define BIOS_VIDEO_MODE                 0x449
#define BIOS_SCREEN_COLUMNS             0x44a
#define BIOS_VIDEO_MEMORY_USED          0x44c
#define BIOS_VIDEO_MEMORY_ADDRESS       0x44e
#define BIOS_VIDEO_CURSOR_POS	        0x450

#define BIOS_CURSOR_SHAPE               0x460
#define BIOS_CURSOR_LAST_LINE           0x460
#define BIOS_CURSOR_FIRST_LINE          0x461
#define BIOS_CURRENT_SCREEN_PAGE        0x462
#define BIOS_VIDEO_PORT                 0x463
#define BIOS_VDU_CONTROL                0x465
#define BIOS_VDU_COLOR_REGISTER         0x466
/* 0x467-0x468 is reserved */
#define BIOS_TIMER                      0x46c
#define BIOS_24_HOURS_FLAG              0x470
#define BIOS_KEYBOARD_FLAGS             0x471
#define BIOS_CTRL_ALT_DEL_FLAG          0x472
#define BIOS_HARDDISK_COUNT		0x475
/* 0x474, 0x476, 0x477 is reserved */
#define BIOS_LPT1_TIMEOUT               0x478
#define BIOS_LPT2_TIMEOUT               0x479
#define BIOS_LPT3_TIMEOUT               0x47a
/* 0x47b is reserved */
#define BIOS_COM1_TIMEOUT               0x47c
#define BIOS_COM2_TIMEOUT               0x47d
#define BIOS_COM3_TIMEOUT               0x47e
#define BIOS_COM4_TIMEOUT               0x47f
			/* 0x47e is reserved *///<- why that?
/* 0x47f-0x4ff is unknow for me */

#define BIOS_ROWS_ON_SCREEN_MINUS_1     0x484
#define BIOS_FONT_HEIGHT                0x485

#define BIOS_VIDEO_INFO_0               0x487
#define BIOS_VIDEO_INFO_1               0x488
#define BIOS_VIDEO_INFO_2               0x489
#define BIOS_VIDEO_COMBO                0x48a


#define BIOS_WAIT_FLAG_POINTER          0x498
#define BIOS_WAIT_FLAG_COUNT	        0x49c
#define BIOS_WAIT_FLAG_ACTIVE			0x4a0
#define BIOS_WAIT_FLAG_TEMP				0x4a1

#define BIOS_PRINT_SCREEN_FLAG          0x500

#define BIOS_VIDEO_SAVEPTR              0x4a8

static u32 dp_bios_int8_handler(void *ptr)
{
	struct dp_bios *bios = ptr;
	struct dp_cpu *cpu = bios->cpu;
	u32 value = dp_memv_readd(cpu->memory, BIOS_TIMER) + 1;

	DP_DBG("BIOS int 8 handling (timer value %d)", value);

	/* Increase the bios tick counter */
	dp_memv_writed(cpu->memory, BIOS_TIMER, value);

	/* decrease floppy motor timer */
	u8 val = dp_memv_readb(cpu->memory, BIOS_DISK_MOTOR_TIMEOUT);
	if (val)
		dp_memv_writeb(cpu->memory, BIOS_DISK_MOTOR_TIMEOUT, val - 1);

	/* and running drive */
	dp_memv_writeb(cpu->memory, BIOS_DRIVE_RUNNING, dp_memv_readb(cpu->memory, BIOS_DRIVE_RUNNING) & 0xF0);
	return DP_CALLBACK_NONE;
}

static u32 dp_bios_int11_handler(void *ptr)
{
	struct dp_bios *bios = ptr;

	/* Get equipment list */
	DP_FAT("unhandled int 11");

	return DP_CALLBACK_NONE;
}

static u32 dp_bios_int12_handler(void *ptr)
{
	struct dp_bios *bios = ptr;

	/* Memory size */
	DP_FAT("unhandled int 12");

	return DP_CALLBACK_NONE;
}

static u32 dp_bios_int14_handler(void *ptr)
{
	struct dp_bios *bios = ptr;

	DP_FAT("unhandled int 14");

	return DP_CALLBACK_NONE;
}

static u32 dp_bios_int15_handler(void *ptr)
{
	struct dp_bios *bios = ptr;
	struct dp_cpu *cpu = bios->cpu;

	switch (dcr_reg_ah) {
	case 0x4f:		/* BIOS - Keyboard intercept */
		/* Carry should be set but let's just set it just in case */
		dp_cpu_callback_scf(cpu, DP_TRUE);
		break;
	default:
		DP_FAT("unhandled int 15, AH=0x%x", dcr_reg_ah);
		break;
	}

	return DP_CALLBACK_NONE;
}

static u32 dp_bios_int1a_handler(void *ptr)
{
	struct dp_bios *bios = ptr;
	struct dp_cpu *cpu = bios->cpu;

	switch (dcr_reg_ah) {
	case 00:
		/* NOP */
		break;

	default:
		DP_FAT("unhandled int 1a: AH=%02x", dcr_reg_ah);
		break;
	}

	return DP_CALLBACK_NONE;
}

static u32 dp_bios_int1c_handler(void *ptr)
{
	struct dp_bios *bios = ptr;

	DP_DBG("empty int 1c");

	return DP_CALLBACK_NONE;
}

static u32 dp_bios_int70_handler(void *ptr)
{
	struct dp_bios *bios = ptr;

	DP_FAT("unhandled int 70");

	return DP_CALLBACK_NONE;
}

static u32 dp_bios_int18_handler(void *ptr)
{
	struct dp_bios *bios = ptr;

	DP_FAT("unhandled int 18");

	return DP_CALLBACK_NONE;
}

static u32 dp_bios_int19_handler(void *ptr)
{
	struct dp_bios *bios = ptr;

	DP_FAT("unhandled int 19");

	return DP_CALLBACK_NONE;
}


void dp_bios_init(struct dp_bios *bios, struct dp_logging *logging, struct dp_marshal *marshal,
		  struct dp_cpu *cpu, struct dp_int_callback *int_callback)
{
	int i;

	memset(bios, 0, sizeof(*bios));
	bios->logging = logging;
	bios->cpu = cpu;

	DP_INF("initializing BIOS");

	dp_marshal_register_pointee(marshal, bios, "bios");

	for (i = 0; i < 0x200; i++)
		dp_realv_writed(cpu->memory, 0x40, i, 0);

	dp_memv_writew(cpu->memory, BIOS_TIMER, 0);
	dp_memv_writew(cpu->memory, BIOS_MEMORY_SIZE, 640);

	dp_int_callback_register_inthandler_addr(int_callback, 0x08, dp_bios_int8_handler, bios, DP_CB_TYPE_IRQ0, DP_BIOS_DEFAULT_IRQ0_LOCATION);
	dp_marshal_register_pointee(marshal, dp_bios_int8_handler, "bios_int8");
	dp_int_callback_register_inthandler(int_callback, 0x11, dp_bios_int11_handler, bios, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_bios_int11_handler, "bios_int11");
	dp_int_callback_register_inthandler(int_callback, 0x12, dp_bios_int12_handler, bios, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_bios_int12_handler, "bios_int12");

	/* MISSING: BIOS Disk support */

	/* Serial ports  */
	dp_int_callback_register_inthandler(int_callback, 0x14, dp_bios_int14_handler, bios, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_bios_int14_handler, "bios_int14");

	/* Misc calls */
	dp_int_callback_register_inthandler(int_callback, 0x15, dp_bios_int15_handler, bios, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_bios_int15_handler, "bios_int15");

	/* MISSING: Keyboard setup */
	/* MISSING: Printer setup */
	dp_int_callback_register_inthandler(int_callback, 0x1a, dp_bios_int1a_handler, bios, DP_CB_TYPE_IRET_STI);
	dp_marshal_register_pointee(marshal, dp_bios_int1a_handler, "bios_int1a");

	dp_int_callback_register_inthandler(int_callback, 0x1c, dp_bios_int1c_handler, bios, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_bios_int1c_handler, "bios_int1c");

	dp_int_callback_register_inthandler(int_callback, 0x70, dp_bios_int70_handler, bios, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_bios_int70_handler, "bios_int70");

	dp_int_callback_register_inthandler(int_callback, 0x71, NULL, bios, DP_CB_TYPE_IRQ9);

	dp_int_callback_register_inthandler(int_callback, 0x18, dp_bios_int18_handler, bios, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_bios_int18_handler, "bios_int18");

	dp_int_callback_register_inthandler(int_callback, 0x19, dp_bios_int19_handler, bios, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_bios_int19_handler, "bios_int19");

	/* System BIOS identification */
	dp_memp_writestr(cpu->memory, 0xfe00e, "IBM COMPATIBLE 386 BIOS");
	dp_memp_writestr(cpu->memory, 0xfe061, "BIOS v1.0");
	dp_memp_writestr(cpu->memory, 0xffff5, "01/01/90");
	dp_memp_writeb(cpu->memory, 0xfffff, 0x55);

	dp_memv_writeb(cpu->memory, BIOS_LPT1_TIMEOUT, 1);
	dp_memv_writeb(cpu->memory, BIOS_LPT2_TIMEOUT, 1);
	dp_memv_writeb(cpu->memory, BIOS_LPT3_TIMEOUT, 1);
	dp_memv_writeb(cpu->memory, BIOS_COM1_TIMEOUT, 1);
	dp_memv_writeb(cpu->memory, BIOS_COM2_TIMEOUT, 1);
	dp_memv_writeb(cpu->memory, BIOS_COM3_TIMEOUT, 1);
	dp_memv_writeb(cpu->memory, BIOS_COM4_TIMEOUT, 1);

	/* MISSING: Parallel ports detection */

#if  (0)
	//u16 config=0x4400; //1 Floppy, 2 serial and 1 parallel 
	u16 config = 0x0;

	// set number of parallel ports
	// if(ppindex == 0) config |= 0x8000; // looks like 0 ports are not specified
	//else if(ppindex == 1) config |= 0x0000;
	config |= 0x4000;
	config |= 0x04;
	config |= 0x1000;
	dp_memv_writew(cpu->memory, BIOS_CONFIGURATION, config);
	// CMOS_SetRegister(0x14, (u8) (config & 0xff));	//Should be updated on changes
	/* Setup extended memory size */
	IO_Write(0x70, 0x30);
	size_extended = IO_Read(0x71);
	IO_Write(0x70, 0x31);
	size_extended |= (IO_Read(0x71) << 8);
#endif
}

void dp_bios_marshal(struct dp_bios *bios, struct dp_marshal *marshal)
{
	dp_marshal_write_version(marshal, 1);

	dp_marshal_write(marshal, bios, offsetof(struct dp_bios, _marshal_sep));
}

void dp_bios_unmarshal(struct dp_bios *bios, struct dp_marshal *marshal)
{
	u32 version = 0;
	dp_marshal_read_version(marshal, &version);
	DP_ASSERT(version == 1);

	dp_marshal_read(marshal, bios, offsetof(struct dp_bios, _marshal_sep));
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
