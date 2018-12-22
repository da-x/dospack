#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_HWTIMER
#define DP_LOGGING           (hwtimer->logging)

#include <string.h>

#include "hwtimer.h"

#define PIT_TICK_RATE 1193182

static inline void BIN2BCD(u16 *val)
{
	u16 temp = *val % 10 + (((*val / 10) % 10) << 4) + (((*val / 100) % 10) << 8) + (((*val / 1000) % 10) << 12);
	*val = temp;
}

static inline void BCD2BIN(u16 *val)
{
	u16 temp = (*val & 0x0f) + ((*val >> 4) & 0x0f) * 10 + ((*val >> 8) & 0x0f) * 100 + ((*val >> 12) & 0x0f) * 1000;
	*val = temp;
}

static void dp_hwtimer_pit_event(void *user_ptr)
{
	struct dp_hwtimer *hwtimer = user_ptr;

	dp_pic_activate_irq(hwtimer->pic, 0);

	if (hwtimer->pit[0].mode != 0) {
		hwtimer->pit[0].start += hwtimer->pit[0].delay;

		if (hwtimer->pit[0].update_count) {
			hwtimer->pit[0].delay = (hwtimer->pit[0].cntr * hwtimer->timetrack->ticks_per_second) / PIT_TICK_RATE;
			hwtimer->pit[0].update_count = DP_FALSE;
		}

		dp_timetrack_add_event(hwtimer->timetrack, dp_hwtimer_pit_event, hwtimer, hwtimer->pit[0].delay);
	}
}

static enum dp_bool counter_output(struct dp_hwtimer *hwtimer, u32 counter)
{
	struct dp_hwtimer_pit *p = &hwtimer->pit[counter];
	hwticks_t index = hwtimer->timetrack->ticks - p->start;
	switch (p->mode) {
	case 0:
		if (p->new_mode)
			return DP_FALSE;
		if (index > p->delay)
			return DP_TRUE;
		else
			return DP_FALSE;
		break;
	case 2:
		if (p->new_mode)
			return DP_TRUE;
		index = index % p->delay;
		return index > 0;
	case 3:
		if (p->new_mode)
			return DP_TRUE;
		index = index % p->delay;
		return index * 2 < p->delay;
	case 4:
		//Only low on terminal count
		// if(fmod(index,(double)p->delay) == 0) return DP_FALSE; //Maybe take one rate tick in consideration
		//Easiest solution is to report always high (Space marines uses this mode)
		return DP_TRUE;
	default:
		DP_ERR("Illegal Mode %d for reading output", p->mode);
		return DP_TRUE;
	}
}

static void status_latch(struct dp_hwtimer *hwtimer, u32 counter)
{
	// the timer status can not be overwritten until it is read or the timer was 
	// reprogrammed.
	if (!hwtimer->latched_timerstatus_locked) {
		struct dp_hwtimer_pit *p = &hwtimer->pit[counter];
		hwtimer->latched_timerstatus = 0;
		// Timer Status Word
		// 0: BCD 
		// 1-3: Timer mode
		// 4-5: read/load mode
		// 6: "NULL" - this is 0 if "the counter value is in the counter" ;)
		// should rarely be 1 (i.e. on exotic modes)
		// 7: OUT - the logic level on the Timer output pin
		if (p->bcd)
			hwtimer->latched_timerstatus |= 0x1;
		hwtimer->latched_timerstatus |= ((p->mode & 7) << 1);
		if ((p->read_state == 0) || (p->read_state == 3))
			hwtimer->latched_timerstatus |= 0x30;
		else if (p->read_state == 1)
			hwtimer->latched_timerstatus |= 0x10;
		else if (p->read_state == 2)
			hwtimer->latched_timerstatus |= 0x20;
		if (counter_output(hwtimer, counter))
			hwtimer->latched_timerstatus |= 0x80;
		if (p->new_mode)
			hwtimer->latched_timerstatus |= 0x40;
		// The first thing that is being read from this counter now is the
		// counter status.
		p->counterstatus_set = DP_TRUE;
		hwtimer->latched_timerstatus_locked = DP_TRUE;
	}
}

static void counter_latch(struct dp_hwtimer *hwtimer, u32 counter)
{
	/* Fill the read_latch of the selected counter with current count */
	struct dp_hwtimer_pit *p = &hwtimer->pit[counter];
	p->go_read_latch = DP_FALSE;

	//If gate2 is disabled don't update the read_latch
	if (counter == 2 && !hwtimer->gate2 && p->mode != 1)
		return;

	hwticks_t index = hwtimer->timetrack->ticks - p->start;
	switch (p->mode) {
	case 4:		/* Software Triggered Strobe */
	case 0:		/* Interrupt on Terminal Count */
		/* Counter keeps on counting after passing terminal count */
		if (index > p->delay) {
			index -= p->delay;
			index %= (hwtimer->timetrack->ticks_per_second * 10000) / PIT_TICK_RATE;

			/*
			 * index was sec/1000.0.
			 * index is now sec/tps.
			 * y = x * 1000 / tps;
			 * x = y * tps / 1000;
			 */

			if (p->bcd) {
				p->read_latch = (u16) (9999 - (index * PIT_TICK_RATE) / hwtimer->timetrack->ticks_per_second);
			} else {
				p->read_latch = (u16) (0xffff - (index * PIT_TICK_RATE) / hwtimer->timetrack->ticks_per_second);
			}
		} else {
			p->read_latch = (u16) (p->cntr - (index * PIT_TICK_RATE) / hwtimer->timetrack->ticks_per_second);
		}
		break;
	case 1:		// countdown
		if (p->counting) {
			if (index > p->delay) {	// has timed out
				p->read_latch = 0xffff;	//unconfirmed
			} else {
				p->read_latch = (u16) (p->cntr - (index * PIT_TICK_RATE) / hwtimer->timetrack->ticks_per_second);
			}
		}
		break;
	case 2:		/* Rate Generator */
		index %= p->delay;
		p->read_latch = (u16) (p->cntr - (((double)index) / p->delay) * p->cntr);
		break;
	case 3:		/* Square Wave Rate Generator */
		index %= p->delay;
		index *= 2;
		if (index > p->delay)
			index -= p->delay;
		p->read_latch = (u16) (p->cntr - (((double)index) / p->delay) * p->cntr);
		// In mode 3 it never returns odd numbers LSB (if odd number is written 1 will be
		// subtracted on first clock and then always 2)
		// fixes "Corncob 3D"
		p->read_latch &= 0xfffe;
		break;
	default:
		DP_ERR("Illegal Mode %d for reading counter %d", p->mode, counter);
		p->read_latch = 0xffff;
		break;
	}
}

static void write_latch(void *user_ptr, u32 port, u8 val)
{
	struct dp_hwtimer *hwtimer = user_ptr;

	//LOG(LOG_PIT,LOG_ERROR)("port %X write:%X state:%X",port,val,pit[port-0x40].write_state);

	u32 counter = port - 0x40;
	struct dp_hwtimer_pit *p = &hwtimer->pit[counter];
	if (p->bcd == DP_TRUE)
		BIN2BCD(&p->write_latch);

	switch (p->write_state) {
	case 0:
		p->write_latch = p->write_latch | ((val & 0xff) << 8);
		p->write_state = 3;
		break;
	case 3:
		p->write_latch = val & 0xff;
		p->write_state = 0;
		break;
	case 1:
		p->write_latch = val & 0xff;
		break;
	case 2:
		p->write_latch = (val & 0xff) << 8;
		break;
	}
	if (p->bcd == DP_TRUE)
		BCD2BIN(&p->write_latch);
	if (p->write_state != 0) {
		if (p->write_latch == 0) {
			if (p->bcd == DP_FALSE)
				p->cntr = 0x10000;
			else
				p->cntr = 9999;
		} else
			p->cntr = p->write_latch;

		if ((!p->new_mode) && (p->mode == 2) && (counter == 0)) {
			// In mode 2 writing another value has no direct effect on the count
			// until the old one has run out. This might apply to other modes too.
			// This is not fixed for PIT2 yet!!
			p->update_count = DP_TRUE;
			return;
		}
		p->start = hwtimer->timetrack->ticks;
		p->delay = (p->cntr * hwtimer->timetrack->ticks_per_second) / PIT_TICK_RATE;

		switch (counter) {
		case 0x00:	/* Timer hooked to IRQ 0 */
			if (p->new_mode || p->mode == 0) {
				if (p->mode == 0)
					dp_timetrack_remove_event(hwtimer->timetrack, dp_hwtimer_pit_event);	// DoWhackaDo demo
				dp_timetrack_add_event(hwtimer->timetrack, dp_hwtimer_pit_event, hwtimer, p->delay);
			} else
				DP_DBG("PIT 0 Timer set without new control word");
			DP_DBG("PIT 0 Timer at %.4f Hz mode %d", 1000.0 / p->delay, p->mode);
			break;
		case 0x02:	/* Timer hooked to PC-Speaker */
//                      LOG(LOG_PIT,"PIT 2 Timer at %.3g Hz mode %d",PIT_TICK_RATE/(double)p->cntr,p->mode);
#if (0)
			PCSPEAKER_SetCounter(p->cntr, p->mode);
#endif
			break;
		default:
			DP_ERR("PIT:Illegal timer selected for writing");
		}
		p->new_mode = DP_FALSE;
	}
}

static u8 read_latch(void *user_ptr, u32 port)
{
	struct dp_hwtimer *hwtimer = user_ptr;
	//LOG(LOG_PIT,LOG_ERROR)("port read %X",port);
	u32 counter = port - 0x40;
	u8 ret = 0;

	if (hwtimer->pit[counter].counterstatus_set) {
		hwtimer->pit[counter].counterstatus_set = DP_FALSE;
		hwtimer->latched_timerstatus_locked = DP_FALSE;
		ret = hwtimer->latched_timerstatus;
	} else {
		if (hwtimer->pit[counter].go_read_latch == DP_TRUE)
			counter_latch(hwtimer, counter);

		if (hwtimer->pit[counter].bcd == DP_TRUE)
			BIN2BCD(&hwtimer->pit[counter].read_latch);

		switch (hwtimer->pit[counter].read_state) {
		case 0:	/* read MSB & return to state 3 */
			ret = (hwtimer->pit[counter].read_latch >> 8) & 0xff;
			hwtimer->pit[counter].read_state = 3;
			hwtimer->pit[counter].go_read_latch = DP_TRUE;
			break;
		case 3:	/* read LSB followed by MSB */
			ret = hwtimer->pit[counter].read_latch & 0xff;
			hwtimer->pit[counter].read_state = 0;
			break;
		case 1:	/* read LSB */
			ret = hwtimer->pit[counter].read_latch & 0xff;
			hwtimer->pit[counter].go_read_latch = DP_TRUE;
			break;
		case 2:	/* read MSB */
			ret = (hwtimer->pit[counter].read_latch >> 8) & 0xff;
			hwtimer->pit[counter].go_read_latch = DP_TRUE;
			break;
		default:
			DP_FAT("error in readlatch");
			break;
		}
		if (hwtimer->pit[counter].bcd == DP_TRUE)
			BCD2BIN(&hwtimer->pit[counter].read_latch);
	}
	return ret;
}

static void write_p43(void *user_ptr, u32 port, u8 val)
{
	struct dp_hwtimer *hwtimer = user_ptr;
	//LOG(LOG_PIT,LOG_ERROR)("port 43 %X",val);

	u32 latch = (val >> 6) & 0x03;
	switch (latch) {
	case 0:
	case 1:
	case 2:
		if ((val & 0x30) == 0) {
			/* Counter latch command */
			counter_latch(hwtimer, latch);
		} else {
			hwtimer->pit[latch].bcd = (val & 1) > 0;
			if (val & 1) {
				if (hwtimer->pit[latch].cntr >= 9999)
					hwtimer->pit[latch].cntr = 9999;
			}
			// Timer is being reprogrammed, unlock the status
			if (hwtimer->pit[latch].counterstatus_set) {
				hwtimer->pit[latch].counterstatus_set = DP_FALSE;
				hwtimer->latched_timerstatus_locked = DP_FALSE;
			}
			hwtimer->pit[latch].update_count = DP_FALSE;
			hwtimer->pit[latch].counting = DP_FALSE;
			hwtimer->pit[latch].read_state = (val >> 4) & 0x03;
			hwtimer->pit[latch].write_state = (val >> 4) & 0x03;
			u8 mode = (val >> 1) & 0x07;
			if (mode > 5)
				mode -= 4;	//6,7 become 2 and 3

			/* Don't set it directly so counter_output uses the old mode */
			/* That's theory. It breaks panic. So set it here again */
			if (!hwtimer->pit[latch].mode)
				hwtimer->pit[latch].mode = mode;

			/* If the line goes from low to up => generate irq. 
			 *      ( BUT needs to stay up until acknowlegded by the cpu!!! therefore: )
			 * If the line goes to low => disable irq.
			 * Mode 0 starts with a low line. (so always disable irq)
			 * Mode 2,3 start with a high line.
			 * counter_output tells if the current counter is high or low 
			 * So actually a mode 2 timer enables and disables irq al the time. (not handled) */

			if (latch == 0) {
				dp_timetrack_remove_event(hwtimer->timetrack, dp_hwtimer_pit_event);	// DoWhackaDo demo

				if (!counter_output(hwtimer, 0) && mode) {
					dp_pic_activate_irq(hwtimer->pic, 0);
#if (0)
					//Don't raise instantaniously. (Origamo)
					if (CPU_Cycles < 25)
						CPU_Cycles = 25;
#endif
				}
				if (!mode)
					dp_pic_activate_irq(hwtimer->pic,  0);
			}
			hwtimer->pit[latch].new_mode = DP_TRUE;
			hwtimer->pit[latch].mode = mode;	//Set the correct mode (here)
		}
		break;
	case 3:
		if ((val & 0x20) == 0) {	/* Latch multiple pit counters */
			if (val & 0x02)
				counter_latch(hwtimer, 0);
			if (val & 0x04)
				counter_latch(hwtimer, 1);
			if (val & 0x08)
				counter_latch(hwtimer, 2);
		}
		// status and values can be latched simultaneously
		if ((val & 0x10) == 0) {	/* Latch status words */
			// but only 1 status can be latched simultaneously
			if (val & 0x02)
				status_latch(hwtimer, 0);
			else if (val & 0x04)
				status_latch(hwtimer, 1);
			else if (val & 0x08)
				status_latch(hwtimer, 2);
		}
		break;
	}
}

void dp_hwtimer_setgate2(struct dp_hwtimer *hwtimer, enum dp_bool in)
{
	//No changes if gate doesn't change
	if (hwtimer->gate2 == in)
		return;
	u8 mode = hwtimer->pit[2].mode;
	switch (mode) {
	case 0:
		if (in)
			hwtimer->pit[2].start = hwtimer->timetrack->ticks;
		else {
			//Fill readlatch and store it.
			counter_latch(hwtimer, 2);
			hwtimer->pit[2].cntr = hwtimer->pit[2].read_latch;
		}
		break;
	case 1:
		// gate 1 on: reload counter; off: nothing
		if (in) {
			hwtimer->pit[2].counting = DP_TRUE;
			hwtimer->pit[2].start = hwtimer->timetrack->ticks;
		}
		break;
	case 2:
	case 3:
		//If gate is enabled restart counting. If disable store the current read_latch
		if (in)
			hwtimer->pit[2].start = hwtimer->timetrack->ticks;
		else
			counter_latch(hwtimer, 2);
		break;
	case 4:
	case 5:
		DP_WRN("unsupported gate 2 mode %x", mode);
		break;
	}
	hwtimer->gate2 = in;		//Set it here so the counter_latch above works
}

static struct dp_io_port latch_io_desc = {
	.read8 = read_latch,
	.write8 = write_latch,
};

static struct dp_io_port read_latch_io_desc = {
	.read8 = read_latch,
};

static struct dp_io_port p43_io_desc = {
	.write8 = write_p43,
};

void dp_hwtimer_init(struct dp_hwtimer *hwtimer, struct dp_logging *logging, struct dp_marshal *marshal, struct dp_pic *pic, struct dp_io *io,
		     struct dp_timetrack *timetrack)
{
	memset(hwtimer, 0, sizeof(*hwtimer));
	hwtimer->logging = logging;
	hwtimer->timetrack = timetrack;
	hwtimer->pic = pic;

	DP_INF("initializing HWTIMER");

	dp_marshal_register_pointee(marshal, hwtimer, "hwtimer");

	/* Setup Timer 0 */

	hwtimer->pit[0].cntr = 0x10000;
	hwtimer->pit[0].write_state = 3;
	hwtimer->pit[0].read_state = 3;
	hwtimer->pit[0].read_latch = 0;
	hwtimer->pit[0].write_latch = 0;
	hwtimer->pit[0].mode = 3;
	hwtimer->pit[0].bcd = DP_FALSE;
	hwtimer->pit[0].go_read_latch = DP_TRUE;
	hwtimer->pit[0].counterstatus_set = DP_FALSE;
	hwtimer->pit[0].update_count = DP_FALSE;

	hwtimer->pit[1].bcd = DP_FALSE;
	hwtimer->pit[1].write_state = 1;
	hwtimer->pit[1].read_state = 1;
	hwtimer->pit[1].go_read_latch = DP_TRUE;
	hwtimer->pit[1].cntr = 18;
	hwtimer->pit[1].mode = 2;
	hwtimer->pit[1].write_state = 3;
	hwtimer->pit[1].counterstatus_set = DP_FALSE;

	hwtimer->pit[2].read_latch = 1320;	/* MadTv1 */
	hwtimer->pit[2].write_state = 3;	/* Chuck Yeager */
	hwtimer->pit[2].read_state = 3;
	hwtimer->pit[2].mode = 3;
	hwtimer->pit[2].bcd = DP_FALSE;
	hwtimer->pit[2].cntr = 1320;
	hwtimer->pit[2].go_read_latch = DP_TRUE;
	hwtimer->pit[2].counterstatus_set = DP_FALSE;
	hwtimer->pit[2].counting = DP_FALSE;

	hwtimer->pit[0].delay = (hwtimer->pit[0].cntr * timetrack->ticks_per_second) / PIT_TICK_RATE;
	hwtimer->pit[1].delay = (hwtimer->pit[1].cntr * timetrack->ticks_per_second) / PIT_TICK_RATE;
	hwtimer->pit[2].delay = (hwtimer->pit[2].cntr * timetrack->ticks_per_second) / PIT_TICK_RATE;

	hwtimer->latched_timerstatus_locked = DP_FALSE;
	hwtimer->gate2 = DP_FALSE;

	dp_marshal_register_pointee(marshal, &latch_io_desc, "hwtimerio1");
	dp_marshal_register_pointee(marshal, &read_latch_io_desc, "hwtimerio2");
	dp_marshal_register_pointee(marshal, &p43_io_desc, "hwtimerio3");

	dp_io_register_ports(io, hwtimer, &latch_io_desc, 0x40, 1);
	dp_io_register_ports(io, hwtimer, &read_latch_io_desc, 0x41, 1);
	dp_io_register_ports(io, hwtimer, &latch_io_desc, 0x42, 1);
	dp_io_register_ports(io, hwtimer, &p43_io_desc, 0x43, 1);

	dp_marshal_register_pointee(marshal, dp_hwtimer_pit_event, "hwtimerpicev");

	dp_timetrack_add_event(timetrack, dp_hwtimer_pit_event, hwtimer, hwtimer->pit[0].delay);
}

void dp_hwtimer_marshal(struct dp_hwtimer *hwtimer, struct dp_marshal *marshal)
{
	dp_marshal_write_version(marshal, 1);
	dp_marshal_write(marshal, hwtimer, offsetof(struct dp_hwtimer, _marshal_sep));
}

void dp_hwtimer_unmarshal(struct dp_hwtimer *hwtimer, struct dp_marshal *marshal)
{
	u32 version = 0;
	dp_marshal_read_version(marshal, &version);
	DP_ASSERT(version == 1);
	dp_marshal_read(marshal, hwtimer, offsetof(struct dp_hwtimer, _marshal_sep));
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
