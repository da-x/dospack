#ifndef _DOSPACK_BIOS_H__
#define _DOSPACK_BIOS_H__

#include "common.h"
#include "logging.h"
#include "marshal.h"
#include "int_callback.h"
#include "cpu.h"

#define DP_BIOS_DEFAULT_HANDLER_LOCATION	(real_make(0xf000,0xff53))
#define DP_BIOS_DEFAULT_IRQ0_LOCATION	(real_make(0xf000,0xfea5))
#define DP_BIOS_DEFAULT_IRQ1_LOCATION	(real_make(0xf000,0xe987))
#define DP_BIOS_DEFAULT_IRQ2_LOCATION	(real_make(0xf000,0xff55))

#define DP_BIOS_KEYBOARD_STATE             0x417
#define DP_BIOS_KEYBOARD_FLAGS1            DP_BIOS_KEYBOARD_STATE
#define DP_BIOS_KEYBOARD_FLAGS2            0x418
#define DP_BIOS_KEYBOARD_FLAGS3            0x496
#define DP_BIOS_KEYBOARD_LEDS              0x497

#define DP_BIOS_KEYBOARD_TOKEN             0x419

#define DP_BIOS_KEYBOARD_BUFFER_HEAD       0x41a
#define DP_BIOS_KEYBOARD_BUFFER_TAIL       0x41c
#define DP_BIOS_KEYBOARD_BUFFER            0x41e
#define DP_BIOS_KEYBOARD_BUFFER_START      0x480
#define DP_BIOS_KEYBOARD_BUFFER_END        0x482

#define DP_BIOS_MAX_SCAN_CODE 0x58

struct dp_bios {
	u64 ticks;
	u64 ticks_per_msec;

	char _marshal_sep[0];

	struct dp_logging *logging;
	struct dp_cpu *cpu;
};

void dp_bios_init(struct dp_bios *bios, struct dp_logging *logging, struct dp_marshal *marshal,
		  struct dp_cpu *cpu, struct dp_int_callback *int_callback);
void dp_bios_marshal(struct dp_bios *bios, struct dp_marshal *marshal);
void dp_bios_unmarshal(struct dp_bios *bios, struct dp_marshal *marshal);

#endif
