#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_KEYBOARD
#define DP_LOGGING           (keyboard->logging)

#include <string.h>

#include "keyboard.h"
#include "bios.h"
#include "cpu_inlines.h"

#define KEYDELAY (keyboard->timetrack->ticks_per_second/10000*3)

static enum dp_bool IsEnhancedKey(u16 *key)
{
	/* test for special keys (return and slash on numblock) */
	if ((*key >> 8) == 0xe0) {
		if (((*key & 0xff) == 0x0a) || ((*key & 0xff) == 0x0d)) {
			/* *key is return on the numblock */
			*key = (*key & 0xff) | 0x1c00;
		} else {
			/* *key is slash on the numblock */
			*key = (*key & 0xff) | 0x3500;
		}
		/* both *keys are not considered enhanced *keys */
		return DP_FALSE;
	} else if (((*key >> 8) > 0x84) || (((*key & 0xff) == 0xf0) && (*key >> 8))) {
		/* *key is enhanced *key (either scancode part>0x84 or
		   specially-marked *keyboard combination, low part==0xf0) */
		return DP_TRUE;
	}
	/* convert *key if necessary (extended *keys) */
	if ((*key >> 8) && ((*key & 0xff) == 0xe0)) {
		*key &= 0xff00;
	}
	return DP_FALSE;
}

static enum dp_bool get_key(struct dp_keyboard *keyboard, u16 *code)
{
	struct dp_cpu *cpu = keyboard->cpu;

	u16 start, end, head, tail, thead;
	start = dp_memv_readw(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_START);
	end = dp_memv_readw(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_END);
	head = dp_memv_readw(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_HEAD);
	tail = dp_memv_readw(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_TAIL);

	if (head == tail)
		return DP_FALSE;
	thead = head + 2;
	if (thead >= end)
		thead = start;
	dp_memv_writew(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_HEAD, thead);
	*code = dp_realv_readw(cpu->memory, 0x40, head);
	return DP_TRUE;
}

static enum dp_bool check_key(struct dp_keyboard *keyboard, u16 *code)
{
	struct dp_cpu *cpu = keyboard->cpu;

	u16 head, tail;
	head = dp_memv_readw(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_HEAD);
	tail = dp_memv_readw(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_TAIL);
	if (head == tail)
		return DP_FALSE;

	*code = dp_realv_readw(cpu->memory, 0x40, head);
	return DP_TRUE;
}

static u32 dp_keyboard_int16_handler(void *ptr)
{
	struct dp_keyboard *keyboard = ptr;
	struct dp_cpu *cpu = keyboard->cpu;
	u16 temp = 0;

	switch (dcr_reg_ah) {
	case 0x00:		/* GET KEYSTROKE */
		dcr_reg_ax = 0;
		if ((get_key(keyboard, &temp)) && (!IsEnhancedKey(&temp))) {
			/* normal key found, return translated key in ax */
			dcr_reg_ax = temp;
		}
		break;

	case 0x01: {
		// enable interrupt-flag after IRET of this int16
		dp_memv_writew(cpu->memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 4, 
			       (dp_memv_readw(cpu->memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 4) | DC_FLAG_IF));
		for (;;) {
			if (check_key(keyboard, &temp)) {
				if (!IsEnhancedKey(&temp)) {
					/* normal key, return translated key in ax */
					dp_cpu_callback_szf(cpu, DP_FALSE);
					dcr_reg_ax = temp;
					break;
				} else {
					/* remove enhanced key from buffer and ignore it */
					get_key(keyboard, &temp);
				}
			} else {
				/* no key available */
				dp_cpu_callback_szf(cpu, DP_TRUE);
				break;
			}
		}
		break;
	}
	default:
		DP_FAT("unhandled int 16: AH=%02x", dcr_reg_ah);
		break;
	}

	return DP_CALLBACK_NONE;
}

enum dp_bool BIOS_AddKeyToBuffer(struct dp_keyboard *keyboard, u16 code)
{
	struct dp_cpu *cpu = keyboard->cpu;
	u16 start, end, head, tail, ttail;

	if (dp_memv_readb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS2) & 8)
		return DP_TRUE;

	start = dp_memv_readw(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_START);
	end = dp_memv_readw(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_END);
	head = dp_memv_readw(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_HEAD);
	tail = dp_memv_readw(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_TAIL);
	ttail = tail + 2;
	if (ttail >= end) {
		ttail = start;
	}
	/* Check for buffer Full */
	//TODO Maybe beeeeeeep or something although that should happend when internal buffer is full
	if (ttail == head)
		return DP_FALSE;

	dp_realv_writew(cpu->memory, 0x40, tail, code);
	dp_memv_writew(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_TAIL, ttail);
	return DP_TRUE;
}

static void add_key(struct dp_keyboard *keyboard, u16 code)
{
	if (code != 0)
		BIOS_AddKeyToBuffer(keyboard, code);
}

#define none 0

static struct {
	u16 normal;
	u16 shift;
	u16 control;
	u16 alt;
} scan_to_scanascii[DP_BIOS_MAX_SCAN_CODE + 1] = {
	{
	none, none, none, none}, {
	0x011b, 0x011b, 0x011b, 0x01f0},	/* escape */
	{
	0x0231, 0x0221, none, 0x7800},	/* 1! */
	{
	0x0332, 0x0340, 0x0300, 0x7900},	/* 2@ */
	{
	0x0433, 0x0423, none, 0x7a00},	/* 3# */
	{
	0x0534, 0x0524, none, 0x7b00},	/* 4$ */
	{
	0x0635, 0x0625, none, 0x7c00},	/* 5% */
	{
	0x0736, 0x075e, 0x071e, 0x7d00},	/* 6^ */
	{
	0x0837, 0x0826, none, 0x7e00},	/* 7& */
	{
	0x0938, 0x092a, none, 0x7f00},	/* 8* */
	{
	0x0a39, 0x0a28, none, 0x8000},	/* 9( */
	{
	0x0b30, 0x0b29, none, 0x8100},	/* 0) */
	{
	0x0c2d, 0x0c5f, 0x0c1f, 0x8200},	/* -_ */
	{
	0x0d3d, 0x0d2b, none, 0x8300},	/* =+ */
	{
	0x0e08, 0x0e08, 0x0e7f, 0x0ef0},	/* backspace */
	{
	0x0f09, 0x0f00, 0x9400, none},	/* tab */
	{
	0x1071, 0x1051, 0x1011, 0x1000},	/* Q */
	{
	0x1177, 0x1157, 0x1117, 0x1100},	/* W */
	{
	0x1265, 0x1245, 0x1205, 0x1200},	/* E */
	{
	0x1372, 0x1352, 0x1312, 0x1300},	/* R */
	{
	0x1474, 0x1454, 0x1414, 0x1400},	/* T */
	{
	0x1579, 0x1559, 0x1519, 0x1500},	/* Y */
	{
	0x1675, 0x1655, 0x1615, 0x1600},	/* U */
	{
	0x1769, 0x1749, 0x1709, 0x1700},	/* I */
	{
	0x186f, 0x184f, 0x180f, 0x1800},	/* O */
	{
	0x1970, 0x1950, 0x1910, 0x1900},	/* P */
	{
	0x1a5b, 0x1a7b, 0x1a1b, 0x1af0},	/* [{ */
	{
	0x1b5d, 0x1b7d, 0x1b1d, 0x1bf0},	/* ]} */
	{
	0x1c0d, 0x1c0d, 0x1c0a, none},	/* Enter */
	{
	none, none, none, none},	/* L Ctrl */
	{
	0x1e61, 0x1e41, 0x1e01, 0x1e00},	/* A */
	{
	0x1f73, 0x1f53, 0x1f13, 0x1f00},	/* S */
	{
	0x2064, 0x2044, 0x2004, 0x2000},	/* D */
	{
	0x2166, 0x2146, 0x2106, 0x2100},	/* F */
	{
	0x2267, 0x2247, 0x2207, 0x2200},	/* G */
	{
	0x2368, 0x2348, 0x2308, 0x2300},	/* H */
	{
	0x246a, 0x244a, 0x240a, 0x2400},	/* J */
	{
	0x256b, 0x254b, 0x250b, 0x2500},	/* K */
	{
	0x266c, 0x264c, 0x260c, 0x2600},	/* L */
	{
	0x273b, 0x273a, none, 0x27f0},	/* ;: */
	{
	0x2827, 0x2822, none, 0x28f0},	/* '" */
	{
	0x2960, 0x297e, none, 0x29f0},	/* `~ */
	{
	none, none, none, none},	/* L shift */
	{
	0x2b5c, 0x2b7c, 0x2b1c, 0x2bf0},	/* |\ */
	{
	0x2c7a, 0x2c5a, 0x2c1a, 0x2c00},	/* Z */
	{
	0x2d78, 0x2d58, 0x2d18, 0x2d00},	/* X */
	{
	0x2e63, 0x2e43, 0x2e03, 0x2e00},	/* C */
	{
	0x2f76, 0x2f56, 0x2f16, 0x2f00},	/* V */
	{
	0x3062, 0x3042, 0x3002, 0x3000},	/* B */
	{
	0x316e, 0x314e, 0x310e, 0x3100},	/* N */
	{
	0x326d, 0x324d, 0x320d, 0x3200},	/* M */
	{
	0x332c, 0x333c, none, 0x33f0},	/* ,< */
	{
	0x342e, 0x343e, none, 0x34f0},	/* .> */
	{
	0x352f, 0x353f, none, 0x35f0},	/* /? */
	{
	none, none, none, none},	/* R Shift */
	{
	0x372a, 0x372a, 0x9600, 0x37f0},	/* * */
	{
	none, none, none, none},	/* L Alt */
	{
	0x3920, 0x3920, 0x3920, 0x3920},	/* space */
	{
	none, none, none, none},	/* caps lock */
	{
	0x3b00, 0x5400, 0x5e00, 0x6800},	/* F1 */
	{
	0x3c00, 0x5500, 0x5f00, 0x6900},	/* F2 */
	{
	0x3d00, 0x5600, 0x6000, 0x6a00},	/* F3 */
	{
	0x3e00, 0x5700, 0x6100, 0x6b00},	/* F4 */
	{
	0x3f00, 0x5800, 0x6200, 0x6c00},	/* F5 */
	{
	0x4000, 0x5900, 0x6300, 0x6d00},	/* F6 */
	{
	0x4100, 0x5a00, 0x6400, 0x6e00},	/* F7 */
	{
	0x4200, 0x5b00, 0x6500, 0x6f00},	/* F8 */
	{
	0x4300, 0x5c00, 0x6600, 0x7000},	/* F9 */
	{
	0x4400, 0x5d00, 0x6700, 0x7100},	/* F10 */
	{
	none, none, none, none},	/* Num Lock */
	{
	none, none, none, none},	/* Scroll Lock */
	{
	0x4700, 0x4737, 0x7700, 0x0007},	/* 7 Home */
	{
	0x4800, 0x4838, 0x8d00, 0x0008},	/* 8 UP */
	{
	0x4900, 0x4939, 0x8400, 0x0009},	/* 9 PgUp */
	{
	0x4a2d, 0x4a2d, 0x8e00, 0x4af0},	/* - */
	{
	0x4b00, 0x4b34, 0x7300, 0x0004},	/* 4 Left */
	{
	0x4cf0, 0x4c35, 0x8f00, 0x0005},	/* 5 */
	{
	0x4d00, 0x4d36, 0x7400, 0x0006},	/* 6 Right */
	{
	0x4e2b, 0x4e2b, 0x9000, 0x4ef0},	/* + */
	{
	0x4f00, 0x4f31, 0x7500, 0x0001},	/* 1 End */
	{
	0x5000, 0x5032, 0x9100, 0x0002},	/* 2 Down */
	{
	0x5100, 0x5133, 0x7600, 0x0003},	/* 3 PgDn */
	{
	0x5200, 0x5230, 0x9200, 0x0000},	/* 0 Ins */
	{
	0x5300, 0x532e, 0x9300, none},	/* Del */
	{
	none, none, none, none}, {
	none, none, none, none}, {
	0x565c, 0x567c, none, none},	/* (102-key) */
	{
	0x8500, 0x8700, 0x8900, 0x8b00},	/* F11 */
	{
	0x8600, 0x8800, 0x8a00, 0x8c00}	/* F12 */
};

static u32 dp_keyboard_int9_handler(void *ptr)
{
	struct dp_keyboard *keyboard = ptr;
	struct dp_cpu *cpu = keyboard->cpu;
	u32 scancode = dcr_reg_al;

	u8 flags1, flags2, flags3, leds;

	DP_WRN("int 19, AL=0x%x", scancode);
	flags1 = dp_memv_readb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS1);
	flags2 = dp_memv_readb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS2);
	flags3 = dp_memv_readb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS3);
	leds = dp_memv_readb(cpu->memory, DP_BIOS_KEYBOARD_LEDS);

	switch (scancode) {
		/* First the hard ones  */
	case 0xfa:		/* ack. Do nothing for now */
		break;
	case 0xe1:		/* Extended key special. Only pause uses this */
		flags3 |= 0x01;
		break;
	case 0xe0:		/* Extended key */
		flags3 |= 0x02;
		break;
	case 0x1d:		/* Ctrl Pressed */
		if (!(flags3 & 0x01)) {
			flags1 |= 0x04;
			if (flags3 & 0x02)
				flags3 |= 0x04;
			else
				flags2 |= 0x01;
		}		/* else it's part of the pause scancodes */
		break;
	case 0x9d:		/* Ctrl Released */
		if (!(flags3 & 0x01)) {
			if (flags3 & 0x02)
				flags3 &= ~0x04;
			else
				flags2 &= ~0x01;
			if (!((flags3 & 0x04) || (flags2 & 0x01)))
				flags1 &= ~0x04;
		}
		break;
	case 0x2a:		/* Left Shift Pressed */
		flags1 |= 0x02;
		break;
	case 0xaa:		/* Left Shift Released */
		flags1 &= ~0x02;
		break;
	case 0x36:		/* Right Shift Pressed */
		flags1 |= 0x01;
		break;
	case 0xb6:		/* Right Shift Released */
		flags1 &= ~0x01;
		break;
	case 0x38:		/* Alt Pressed */
		flags1 |= 0x08;
		if (flags3 & 0x02)
			flags3 |= 0x08;
		else
			flags2 |= 0x02;
		break;
	case 0xb8:		/* Alt Released */
		if (flags3 & 0x02)
			flags3 &= ~0x08;
		else
			flags2 &= ~0x02;
		if (!((flags3 & 0x08) || (flags2 & 0x02))) {	/* Both alt released */
			flags1 &= ~0x08;
			u16 token = dp_memv_readb(cpu->memory, DP_BIOS_KEYBOARD_TOKEN);
			if (token != 0) {
				add_key(keyboard, token);
				dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_TOKEN, 0);
			}
		}
		break;

	case 0x3a:
		flags2 |= 0x40;
		break;		//CAPSLOCK
	case 0xba:
		flags1 ^= 0x40;
		flags2 &= ~0x40;
		leds ^= 0x04;
		break;
	case 0x45:
		if (flags3 & 0x01) {
			/* last scancode of pause received; first remove 0xe1-prefix */
			flags3 &= ~0x01;
			dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS3, flags3);
			if (flags2 & 1) {
				/* ctrl-pause (break), special handling needed:
				   add zero to the keyboard buffer, call int 0x1b which
				   sets ctrl-c flag which calls int 0x23 in certain dos
				   input/output functions;    not handled */
			} else if ((flags2 & 8) == 0) {
				/* normal pause key, enter loop */
				dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS2, flags2 | 8);
				DP_FAT("normal pause key not implemented");

#if (0)
				IO_Write(0x20, 0x20);
				while (dp_memv_readb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS2) & 8)
					CALLBACK_Idle();	// pause loop
				reg_ip += 5;	// skip out 20,20
#endif
				return DP_CALLBACK_NONE;
			}
		} else {
			/* Num Lock */
			flags2 |= 0x20;
		}
		break;
	case 0xc5:
		if (flags3 & 0x01) {
			/* pause released */
			flags3 &= ~0x01;
		} else {
			flags1 ^= 0x20;
			leds ^= 0x02;
			flags2 &= ~0x20;
		}
		break;
	case 0x46:
		flags2 |= 0x10;
		break;		/* Scroll Lock SDL Seems to do this one fine (so break and make codes) */
	case 0xc6:
		flags1 ^= 0x10;
		flags2 &= ~0x10;
		leds ^= 0x01;
		break;
//      case 0x52:flags2|=128;break;//See numpad                                        /* Insert */
	case 0xd2:
		if (flags3 & 0x02) {	/* Maybe honour the insert on keypad as well */
			flags1 ^= 0x80;
			flags2 &= ~0x80;
			break;
		} else {
			goto irq1_end;	/*Normal release */
		}
	case 0x47:		/* Numpad */
	case 0x48:
	case 0x49:
	case 0x4b:
	case 0x4c:
	case 0x4d:
	case 0x4f:
	case 0x50:
	case 0x51:
	case 0x52:
	case 0x53:		/* del . Not entirely correct, but works fine */
		if (flags3 & 0x02) {	/*extend key. e.g key above arrows or arrows */
			if (scancode == 0x52)
				flags2 |= 0x80;	/* press insert */
			if (flags1 & 0x08) {
				add_key(keyboard, scan_to_scanascii[scancode].normal + 0x5000);
			} else if (flags1 & 0x04) {
				add_key(keyboard, (scan_to_scanascii[scancode].control & 0xff00) | 0xe0);
			} else if (((flags1 & 0x3) != 0) || ((flags1 & 0x20) != 0)) {
				add_key(keyboard, (scan_to_scanascii[scancode].shift & 0xff00) | 0xe0);
			} else
				add_key(keyboard, (scan_to_scanascii[scancode].normal & 0xff00) | 0xe0);
			break;
		}
		if (flags1 & 0x08) {
			u8 token = dp_memv_readb(cpu->memory, DP_BIOS_KEYBOARD_TOKEN);
			token = token * 10 + (u8) (scan_to_scanascii[scancode].alt & 0xff);
			dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_TOKEN, token);
		} else if (flags1 & 0x04) {
			add_key(keyboard, scan_to_scanascii[scancode].control);
		} else if (((flags1 & 0x3) != 0) || ((flags1 & 0x20) != 0)) {
			add_key(keyboard, scan_to_scanascii[scancode].shift);
		} else
			add_key(keyboard, scan_to_scanascii[scancode].normal);
		break;

	default: {		/* Normal Key */
		u16 asciiscan;
		/* Now Handle the releasing of keys and see if they match up for a code */
		/* Handle the actual scancode */
		if (scancode & 0x80)
			goto irq1_end;
		if (scancode > DP_BIOS_MAX_SCAN_CODE)
			goto irq1_end;
		if (flags1 & 0x08) {	/* Alt is being pressed */
			asciiscan = scan_to_scanascii[scancode].alt;
#if 0				/* old unicode support disabled */
		} else if (ascii) {
			asciiscan = (scancode << 8) | ascii;
#endif
		} else if (flags1 & 0x04) {	/* Ctrl is being pressed */
			asciiscan = scan_to_scanascii[scancode].control;
		} else if (flags1 & 0x03) {	/* Either shift is being pressed */
			asciiscan = scan_to_scanascii[scancode].shift;
		} else {
			asciiscan = scan_to_scanascii[scancode].normal;
		}
		/* cancel shift is letter and capslock active */
		if (flags1 & 64) {
			if (flags1 & 3) {
				/*cancel shift */
				if (((asciiscan & 0x00ff) > 0x40) && ((asciiscan & 0x00ff) < 0x5b))
					asciiscan = scan_to_scanascii[scancode].normal;
			} else {
				/* add shift */
				if (((asciiscan & 0x00ff) > 0x60) && ((asciiscan & 0x00ff) < 0x7b))
					asciiscan = scan_to_scanascii[scancode].shift;
			}
		}
		if (flags3 & 0x02) {
			/* extended key (numblock), return and slash need special handling */
			if (scancode == 0x1c) {	/* return */
				if (flags1 & 0x08)
					asciiscan = 0xa600;
				else
					asciiscan = (asciiscan & 0xff) | 0xe000;
			} else if (scancode == 0x35) {	/* slash */
				if (flags1 & 0x08)
					asciiscan = 0xa400;
				else if (flags1 & 0x04)
					asciiscan = 0x9500;
				else
					asciiscan = 0xe02f;
			}
		}
		add_key(keyboard, asciiscan);
		break;
	}
	};
irq1_end:
	if (scancode != 0xe0)
		flags3 &= ~0x02;	//Reset 0xE0 Flag
	dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS1, flags1);
	if ((scancode & 0x80) == 0)
		flags2 &= 0xf7;
	dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS2, flags2);
	dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS3, flags3);
	dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_LEDS, leds);

	return DP_CALLBACK_NONE;
}

static void dp_keyboard_SetPort60(struct dp_keyboard *keyboard, u8 val)
{
	keyboard->p60changed = DP_TRUE;
	keyboard->p60data = val;
	dp_pic_activate_irq(keyboard->pic, 1);
}

static void dp_keyboard_TransferBuffer(void *ptr)
{
	struct dp_keyboard *keyboard = ptr;
	keyboard->scheduled = DP_FALSE;
	if (!keyboard->used) {
		DP_DBG("Transfer started with empty buffer");
		return;
	}
	dp_keyboard_SetPort60(keyboard, keyboard->buffer[keyboard->pos]);
	if (++keyboard->pos >= DP_KEYBUFSIZE)
		keyboard->pos -= DP_KEYBUFSIZE;
	keyboard->used--;
}

void dp_keyboard_clear_buffer(struct dp_keyboard *keyboard)
{
	keyboard->used = 0;
	keyboard->pos = 0;
	dp_timetrack_remove_event(keyboard->timetrack, dp_keyboard_TransferBuffer);
	keyboard->scheduled = DP_FALSE;
}

static void dp_keyboard_add_buffer(struct dp_keyboard *keyboard, u8 data)
{
	if (keyboard->used >= DP_KEYBUFSIZE) {
		DP_DBG("Buffer full, dropping code");
		return;
	}
	u32 start = keyboard->pos + keyboard->used;
	if (start >= DP_KEYBUFSIZE)
		start -= DP_KEYBUFSIZE;
	keyboard->buffer[start] = data;
	keyboard->used++;
	/* Start up an event to start the first IRQ */
	if (!keyboard->scheduled && !keyboard->p60changed) {
		keyboard->scheduled = DP_TRUE;
		dp_timetrack_add_event(keyboard->timetrack, dp_keyboard_TransferBuffer, keyboard, KEYDELAY);
	}
}

static u8 read_p60(void *ptr, u32 port)
{
	struct dp_keyboard *keyboard = ptr;

	keyboard->p60changed = DP_FALSE;
	if (!keyboard->scheduled && keyboard->used) {
		keyboard->scheduled = DP_TRUE;
		dp_timetrack_add_event(keyboard->timetrack, dp_keyboard_TransferBuffer, keyboard, KEYDELAY);
	}
	return keyboard->p60data;
}

static void write_p60(void *ptr, u32 port, u8 val)
{
	struct dp_keyboard *keyboard = ptr;

	switch (keyboard->command) {
	case DP_KBD_CMD_NONE:		/* None */
		/* No active command this would normally get sent to the keyboard then */
		dp_keyboard_clear_buffer(keyboard);
		switch (val) {
		case 0xed:	/* Set Leds */
			keyboard->command = DP_KBD_CMD_SETLEDS;
			dp_keyboard_add_buffer(keyboard, 0xfa);	/* Acknowledge */
			break;
		case 0xee:	/* Echo */
			dp_keyboard_add_buffer(keyboard, 0xfa);	/* Acknowledge */
			break;
		case 0xf2:	/* Identify keyboard */
			/* AT's just send acknowledge */
			dp_keyboard_add_buffer(keyboard, 0xfa);	/* Acknowledge */
			break;
		case 0xf3:	/* Typematic rate programming */
			keyboard->command = DP_KBD_CMD_SETTYPERATE;
			dp_keyboard_add_buffer(keyboard, 0xfa);	/* Acknowledge */
			break;
		case 0xf4:	/* Enable keyboard,clear buffer, start scanning */
			DP_DBG("Clear buffer,enable Scaning");
			dp_keyboard_add_buffer(keyboard, 0xfa);	/* Acknowledge */
			keyboard->scanning = DP_TRUE;
			break;
		case 0xf5:	/* Reset keyboard and disable scanning */
			DP_DBG("Reset, disable scanning");
			keyboard->scanning = DP_FALSE;
			dp_keyboard_add_buffer(keyboard, 0xfa);	/* Acknowledge */
			break;
		case 0xf6:	/* Reset keyboard and enable scanning */
			DP_DBG("Reset, enable scanning");
			dp_keyboard_add_buffer(keyboard, 0xfa);	/* Acknowledge */
			keyboard->scanning = DP_FALSE;
			break;
		default:
			/* Just always acknowledge strange commands */
			DP_ERR("60:Unhandled command %X", val);
			dp_keyboard_add_buffer(keyboard, 0xfa);	/* Acknowledge */
		}
		return;
	case DP_KBD_CMD_SETOUTPORT:
		// TODO: MEM_A20_Enable((val & 2) > 0);
		DP_FAT("MEM_A20_Enable");
		keyboard->command = DP_KBD_CMD_NONE;
		break;
	case DP_KBD_CMD_SETTYPERATE:
		{
			static const int delay[] = { 250, 500, 750, 1000 };
			static const int repeat[] = { 33, 37, 42, 46, 50, 54, 58, 63, 67, 75, 83, 92, 100,
				109, 118, 125, 133, 149, 167, 182, 200, 217, 233,
				250, 270, 303, 333, 370, 400, 435, 476, 500
			};
			keyboard->repeat.pause = delay[(val >> 5) & 3];
			keyboard->repeat.rate = repeat[val & 0x1f];
			keyboard->command = DP_KBD_CMD_NONE;
		}
		/* Fallthrough! as setleds does what we want */
	case DP_KBD_CMD_SETLEDS:
		keyboard->command = DP_KBD_CMD_NONE;
		dp_keyboard_clear_buffer(keyboard);
		dp_keyboard_add_buffer(keyboard, 0xfa);	/* Acknowledge */
		break;
	}
}

static u8 read_p61(void *ptr, u32 port)
{
	struct dp_keyboard *keyboard = ptr;

	keyboard->port_61_data ^= 0x20;
	keyboard->port_61_data ^= 0x10;
	return keyboard->port_61_data;
}

static void write_p61(void *ptr, u32 port, u8 val)
{
	struct dp_keyboard *keyboard = ptr;

	if ((keyboard->port_61_data ^ val) & 3) {
		if ((keyboard->port_61_data ^ val) & 1) {
			dp_hwtimer_setgate2(keyboard->hwtimer, val & 0x1);
		}
#if (0)
		PCSPEAKER_SetType(val & 3);
#endif
	}
	keyboard->port_61_data = val;
}

static void write_p64(void *ptr, u32 port, u8 val)
{
	struct dp_keyboard *keyboard = ptr;

	switch (val) {
	case 0xae:		/* Activate keyboard */
		keyboard->active = DP_TRUE;
		if (keyboard->used && !keyboard->scheduled && !keyboard->p60changed) {
			keyboard->scheduled = DP_TRUE;
			dp_timetrack_add_event(keyboard->timetrack, dp_keyboard_TransferBuffer, keyboard, KEYDELAY);
		}
		DP_DBG("Activated");
		break;
	case 0xad:		/* Deactivate keyboard */
		keyboard->active = DP_FALSE;
		DP_DBG("De-Activated");
		break;
	case 0xd0:		/* Outport on buffer */
		// dp_keyboard_SetPort60(keyboard, MEM_A20_Enabled() ? 0x02 : 0);
		DP_FAT("MEM_A20_Enabled");
		break;
	case 0xd1:		/* Write to outport */
		keyboard->command = DP_KBD_CMD_SETOUTPORT;
		break;
	default:
		DP_ERR("Port 64 write with val %d", val);
		break;
	}
}

static u8 read_p64(void *ptr, u32 port)
{
	struct dp_keyboard *keyboard = ptr;
	u8 status = 0x1c | (keyboard->p60changed ? 0x1 : 0x0);

	return status;
}

void dp_keyboard_add_key(struct dp_keyboard *keyboard, enum dp_key keytype, enum dp_bool pressed)
{
	u8 ret = 0;

	DP_DBG("add_key: %d %d", keytype, pressed);

	enum dp_bool extend = DP_FALSE;
	switch (keytype) {
	case DP_KEY_ESC:
		ret = 1;
		break;
	case DP_KEY_1:
		ret = 2;
		break;
	case DP_KEY_2:
		ret = 3;
		break;
	case DP_KEY_3:
		ret = 4;
		break;
	case DP_KEY_4:
		ret = 5;
		break;
	case DP_KEY_5:
		ret = 6;
		break;
	case DP_KEY_6:
		ret = 7;
		break;
	case DP_KEY_7:
		ret = 8;
		break;
	case DP_KEY_8:
		ret = 9;
		break;
	case DP_KEY_9:
		ret = 10;
		break;
	case DP_KEY_0:
		ret = 11;
		break;

	case DP_KEY_MINUS:
		ret = 12;
		break;
	case DP_KEY_EQUALS:
		ret = 13;
		break;
	case DP_KEY_BACKSPACE:
		ret = 14;
		break;
	case DP_KEY_TAB:
		ret = 15;
		break;

	case DP_KEY_Q:
		ret = 16;
		break;
	case DP_KEY_W:
		ret = 17;
		break;
	case DP_KEY_E:
		ret = 18;
		break;
	case DP_KEY_R:
		ret = 19;
		break;
	case DP_KEY_T:
		ret = 20;
		break;
	case DP_KEY_Y:
		ret = 21;
		break;
	case DP_KEY_U:
		ret = 22;
		break;
	case DP_KEY_I:
		ret = 23;
		break;
	case DP_KEY_O:
		ret = 24;
		break;
	case DP_KEY_P:
		ret = 25;
		break;

	case DP_KEY_LEFTBRACKET:
		ret = 26;
		break;
	case DP_KEY_RIGHTBRACKET:
		ret = 27;
		break;
	case DP_KEY_ENTER:
		ret = 28;
		break;
	case DP_KEY_LEFTCTRL:
		ret = 29;
		break;

	case DP_KEY_A:
		ret = 30;
		break;
	case DP_KEY_S:
		ret = 31;
		break;
	case DP_KEY_D:
		ret = 32;
		break;
	case DP_KEY_F:
		ret = 33;
		break;
	case DP_KEY_G:
		ret = 34;
		break;
	case DP_KEY_H:
		ret = 35;
		break;
	case DP_KEY_J:
		ret = 36;
		break;
	case DP_KEY_K:
		ret = 37;
		break;
	case DP_KEY_L:
		ret = 38;
		break;

	case DP_KEY_SEMICOLON:
		ret = 39;
		break;
	case DP_KEY_QUOTE:
		ret = 40;
		break;
	case DP_KEY_GRAVE:
		ret = 41;
		break;
	case DP_KEY_LEFTSHIFT:
		ret = 42;
		break;
	case DP_KEY_BACKSLASH:
		ret = 43;
		break;
	case DP_KEY_Z:
		ret = 44;
		break;
	case DP_KEY_X:
		ret = 45;
		break;
	case DP_KEY_C:
		ret = 46;
		break;
	case DP_KEY_V:
		ret = 47;
		break;
	case DP_KEY_B:
		ret = 48;
		break;
	case DP_KEY_N:
		ret = 49;
		break;
	case DP_KEY_M:
		ret = 50;
		break;

	case DP_KEY_COMMA:
		ret = 51;
		break;
	case DP_KEY_PERIOD:
		ret = 52;
		break;
	case DP_KEY_SLASH:
		ret = 53;
		break;
	case DP_KEY_RIGHTSHIFT:
		ret = 54;
		break;
	case DP_KEY_KPMULTIPLY:
		ret = 55;
		break;
	case DP_KEY_LEFTALT:
		ret = 56;
		break;
	case DP_KEY_SPACE:
		ret = 57;
		break;
	case DP_KEY_CAPSLOCK:
		ret = 58;
		break;

	case DP_KEY_F1:
		ret = 59;
		break;
	case DP_KEY_F2:
		ret = 60;
		break;
	case DP_KEY_F3:
		ret = 61;
		break;
	case DP_KEY_F4:
		ret = 62;
		break;
	case DP_KEY_F5:
		ret = 63;
		break;
	case DP_KEY_F6:
		ret = 64;
		break;
	case DP_KEY_F7:
		ret = 65;
		break;
	case DP_KEY_F8:
		ret = 66;
		break;
	case DP_KEY_F9:
		ret = 67;
		break;
	case DP_KEY_F10:
		ret = 68;
		break;

	case DP_KEY_NUMLOCK:
		ret = 69;
		break;
	case DP_KEY_SCROLLLOCK:
		ret = 70;
		break;

	case DP_KEY_KP7:
		ret = 71;
		break;
	case DP_KEY_KP8:
		ret = 72;
		break;
	case DP_KEY_KP9:
		ret = 73;
		break;
	case DP_KEY_KPMINUS:
		ret = 74;
		break;
	case DP_KEY_KP4:
		ret = 75;
		break;
	case DP_KEY_KP5:
		ret = 76;
		break;
	case DP_KEY_KP6:
		ret = 77;
		break;
	case DP_KEY_KPPLUS:
		ret = 78;
		break;
	case DP_KEY_KP1:
		ret = 79;
		break;
	case DP_KEY_KP2:
		ret = 80;
		break;
	case DP_KEY_KP3:
		ret = 81;
		break;
	case DP_KEY_KP0:
		ret = 82;
		break;
	case DP_KEY_KPPERIOD:
		ret = 83;
		break;

	case DP_KEY_EXTRA_LT_GT:
		ret = 86;
		break;
	case DP_KEY_F11:
		ret = 87;
		break;
	case DP_KEY_F12:
		ret = 88;
		break;

		//The Extended keys

	case DP_KEY_KPENTER:
		extend = DP_TRUE;
		ret = 28;
		break;
	case DP_KEY_RIGHTCTRL:
		extend = DP_TRUE;
		ret = 29;
		break;
	case DP_KEY_KPDIVIDE:
		extend = DP_TRUE;
		ret = 53;
		break;
	case DP_KEY_RIGHTALT:
		extend = DP_TRUE;
		ret = 56;
		break;
	case DP_KEY_HOME:
		extend = DP_TRUE;
		ret = 71;
		break;
	case DP_KEY_UP:
		extend = DP_TRUE;
		ret = 72;
		break;
	case DP_KEY_PAGEUP:
		extend = DP_TRUE;
		ret = 73;
		break;
	case DP_KEY_LEFT:
		extend = DP_TRUE;
		ret = 75;
		break;
	case DP_KEY_RIGHT:
		extend = DP_TRUE;
		ret = 77;
		break;
	case DP_KEY_END:
		extend = DP_TRUE;
		ret = 79;
		break;
	case DP_KEY_DOWN:
		extend = DP_TRUE;
		ret = 80;
		break;
	case DP_KEY_PAGEDOWN:
		extend = DP_TRUE;
		ret = 81;
		break;
	case DP_KEY_INSERT:
		extend = DP_TRUE;
		ret = 82;
		break;
	case DP_KEY_DELETE:
		extend = DP_TRUE;
		ret = 83;
		break;
	case DP_KEY_PAUSE:
		dp_keyboard_add_buffer(keyboard, 0xe1);
		dp_keyboard_add_buffer(keyboard, 29 | (pressed ? 0 : 0x80));
		dp_keyboard_add_buffer(keyboard, 69 | (pressed ? 0 : 0x80));
		return;
	case DP_KEY_PRINTSCREEN:
		/* Not handled yet. But usuable in mapper for special events */
		return;
	default:
		DP_FAT("Unsupported key press %d", keytype);
		break;
	}
	/* Add the actual key in the keyboard queue */
	if (pressed) {
		if (keyboard->repeat.key == keytype)
			keyboard->repeat.wait = keyboard->repeat.rate;
		else
			keyboard->repeat.wait = keyboard->repeat.pause;
		keyboard->repeat.key = keytype;
	} else {
		keyboard->repeat.key = DP_KEY_NONE;
		keyboard->repeat.wait = 0;
		ret += 128;
	}
	if (extend)
		dp_keyboard_add_buffer(keyboard, 0xe0);

	dp_keyboard_add_buffer(keyboard, ret);
}

static struct dp_io_port port60_io_desc = {
	.read8 = read_p60,
	.write8 = write_p60,
};

static struct dp_io_port port61_io_desc = {
	.read8 = read_p61,
	.write8 = write_p61,
};

static struct dp_io_port port64_io_desc = {
	.read8 = read_p64,
	.write8 = write_p64,
};

static void dp_keyboard_every_msec(void *ptr)
{
	struct dp_keyboard *keyboard = ptr;

	if (keyboard->repeat.wait) {
		keyboard->repeat.wait--;
		if (!keyboard->repeat.wait)
			dp_keyboard_add_key(keyboard, keyboard->repeat.key, DP_TRUE);
	}

	dp_timetrack_add_event(keyboard->timetrack, dp_keyboard_every_msec, keyboard, keyboard->timetrack->ticks_per_second/1000);
}

void dp_keyboard_init(struct dp_keyboard *keyboard, struct dp_logging *logging, struct dp_marshal *marshal,
		      struct dp_cpu *cpu, struct dp_int_callback *int_callback, struct dp_io *io,
		      struct dp_timetrack *timetrack, struct dp_pic *pic, struct dp_hwtimer *hwtimer)
{
	u8 flag1 = 0;
	u8 leds = 16;	/* Ack recieved */

	memset(keyboard, 0, sizeof(*keyboard));
	keyboard->logging = logging;
	keyboard->cpu = cpu;
	keyboard->pic = pic;
	keyboard->timetrack = timetrack;
	keyboard->hwtimer = hwtimer;

	DP_INF("initializing KEYBOARD");

	dp_memv_writew(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_START, 0x1e);
	dp_memv_writew(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_END, 0x3e);
	dp_memv_writew(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_HEAD, 0x1e);
	dp_memv_writew(cpu->memory, DP_BIOS_KEYBOARD_BUFFER_TAIL, 0x1e);

	dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS1, flag1);
	dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS2, 0);
	dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_FLAGS3, 16);	/* Enhanced keyboard installed */
	dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_TOKEN, 0);
	dp_memv_writeb(cpu->memory, DP_BIOS_KEYBOARD_LEDS, leds);

	dp_marshal_register_pointee(marshal, keyboard, "keyboard");

	dp_int_callback_register_inthandler(int_callback, 0x16, dp_keyboard_int16_handler, keyboard, DP_CB_TYPE_INT16);
	dp_marshal_register_pointee(marshal, dp_keyboard_int16_handler, "keyboard_int16");

	dp_int_callback_register_inthandler_addr(int_callback, 0x9, dp_keyboard_int9_handler, keyboard, DP_CB_TYPE_IRQ1, DP_BIOS_DEFAULT_IRQ1_LOCATION);
	dp_marshal_register_pointee(marshal, dp_keyboard_int9_handler, "keyboard_int9");

	dp_marshal_register_pointee(marshal, &port60_io_desc, "keyboard_p60");
	dp_io_register_ports(io, keyboard, &port60_io_desc, 0x60, 1);

	dp_marshal_register_pointee(marshal, &port61_io_desc, "keyboard_p61");
	dp_io_register_ports(io, keyboard, &port61_io_desc, 0x61, 1);

	dp_marshal_register_pointee(marshal, &port64_io_desc, "keyboard_p64");
	dp_io_register_ports(io, keyboard, &port64_io_desc, 0x64, 1);

	dp_marshal_register_pointee(marshal, &dp_keyboard_every_msec, "keyboard_tick");
	dp_timetrack_add_event(keyboard->timetrack, dp_keyboard_every_msec, keyboard, keyboard->timetrack->ticks_per_second/1000);

	dp_marshal_register_pointee(marshal, dp_keyboard_TransferBuffer, "keyboard_xferbuf");

	write_p61(keyboard, 0, 0);

	keyboard->active = DP_TRUE;
	keyboard->scanning = DP_TRUE;
	keyboard->command = DP_KBD_CMD_NONE;
	keyboard->p60changed = DP_FALSE;
	keyboard->repeat.key = DP_KEY_NONE;
	keyboard->repeat.pause = 500;
	keyboard->repeat.rate = 33;
	keyboard->repeat.wait = 0;
	dp_keyboard_clear_buffer(keyboard);
}

void dp_keyboard_marshal(struct dp_keyboard *keyboard, struct dp_marshal *marshal)
{
	dp_marshal_write_version(marshal, 1);

	dp_marshal_write(marshal, keyboard, offsetof(struct dp_keyboard, _marshal_sep));
}

void dp_keyboard_unmarshal(struct dp_keyboard *keyboard, struct dp_marshal *marshal)
{
	u32 version = 0;
	dp_marshal_read_version(marshal, &version);
	DP_ASSERT(version == 1);

	dp_marshal_read(marshal, keyboard, offsetof(struct dp_keyboard, _marshal_sep));
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
