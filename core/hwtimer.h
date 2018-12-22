#ifndef _DOSPACK_HWTIMER_H__
#define _DOSPACK_HWTIMER_H__

#include "common.h"
#include "logging.h"
#include "marshal.h"
#include "io.h"
#include "pic.h"
#include "timetrack.h"

struct dp_hwtimer_pit {
	u32 cntr;
	hwticks_t delay;
	hwticks_t start;

	u16 read_latch;
	u16 write_latch;

	u8 mode;
	u8 latch_mode;
	u8 read_state;
	u8 write_state;

	enum dp_bool bcd;
	enum dp_bool go_read_latch;
	enum dp_bool new_mode;
	enum dp_bool counterstatus_set;
	enum dp_bool counting;
	enum dp_bool update_count;
};

struct dp_hwtimer {
	struct dp_hwtimer_pit pit[3];
	enum dp_bool gate2;

	u8 latched_timerstatus;
	enum dp_bool latched_timerstatus_locked;

	char _marshal_sep[0];

	struct dp_pic *pic;
	struct dp_timetrack *timetrack;
	struct dp_logging *logging;
};

void dp_hwtimer_init(struct dp_hwtimer *hwtimer, struct dp_logging *logging, struct dp_marshal *marshal, struct dp_pic *pic, struct dp_io *io,
		     struct dp_timetrack *timetrack);
void dp_hwtimer_setgate2(struct dp_hwtimer *hwtimer, enum dp_bool in);
void dp_hwtimer_marshal(struct dp_hwtimer *hwtimer, struct dp_marshal *marshal);
void dp_hwtimer_unmarshal(struct dp_hwtimer *hwtimer, struct dp_marshal *marshal);

#endif
