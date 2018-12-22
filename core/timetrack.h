#ifndef _DOSPACK_TIMETRACK_H__
#define _DOSPACK_TIMETRACK_H__

#include "common.h"
#include "logging.h"
#include "marshal.h"

#define DP_TIMETRACK_MARSHAL_SUPPORT  0
#define DP_TIMETRACK_MAX_EVENTS       512

typedef u64 hwticks_t;

typedef void (*dp_timetrack_event_cb_t)(void *ptr);
typedef u16 dp_timetrack_event_index_t;

#define DP_TIMETRACK_NULL_EVENT  0

struct dp_timetrack_event {
	hwticks_t exe_time;

	dp_timetrack_event_cb_t cb;
	void *cb_data;

	dp_timetrack_event_index_t next_event;
};

struct dp_timetrack {
	u64 ticks;
	u64 ticks_per_second;
	u64 base_unix_time;
	u64 host_ticks_keepup;
	u64 host_delay_check_delta;
	u64 tick_marshal_save;

	dp_timetrack_event_index_t next_event;
	dp_timetrack_event_index_t free_event;
	struct dp_timetrack_event events[DP_TIMETRACK_MAX_EVENTS];

	char _marshal_sep[0];

	void (*marshal_func)(void *ptr, const char *filename);
	void *marshal_data;
	s64 tick_marshal;
	s64 tick_marshal_period;
	u64 host_clock_base;
	u64 total_host_elapsed;
	enum dp_bool host_time_sync;

	struct dp_logging *logging;
};

void dp_timetrack_init(struct dp_timetrack *timetrack, struct dp_logging *logging, struct dp_marshal *marshal);
void dp_timetrack_marshal(struct dp_timetrack *timetrack, struct dp_marshal *marshal);
void dp_timetrack_unmarshal(struct dp_timetrack *timetrack, struct dp_marshal *marshal);
void dp_timetrack_get_timestamp(struct dp_timetrack *timetrack, int *year, int *month, int *day,
				int *hour, int *min, int *sec, int *usec);
void dp_timetrack_marshal_dump(struct dp_timetrack *timetrack);
void dp_timetrack_host_time_skip(struct dp_timetrack *timetrack);
void dp_timetrack_host_delay(struct dp_timetrack *timetrack);
void dp_timetrack_run_events(struct dp_timetrack *timetrack);
void dp_timetrack_add_event(struct dp_timetrack *timetrack, dp_timetrack_event_cb_t cb, void *cb_data, u64 delay);
void dp_timetrack_remove_event(struct dp_timetrack *timetrack, dp_timetrack_event_cb_t cb);
void dp_timetrack_remove_event_data(struct dp_timetrack *timetrack, dp_timetrack_event_cb_t cb, void *cb_data);

static inline struct dp_timetrack_event *dp_timetrack_get_event(struct dp_timetrack *timetrack, u16 index)
{
	if (index == DP_TIMETRACK_NULL_EVENT  ||  index > DP_TIMETRACK_MAX_EVENTS)
		return NULL;

	return &timetrack->events[index - 1];
}

static inline void dp_timetrack_run_events_check(struct dp_timetrack *timetrack)
{
	if (timetrack->next_event  &&  dp_timetrack_get_event(timetrack, timetrack->next_event)->exe_time <= timetrack->ticks) {
		dp_timetrack_run_events(timetrack);
	}
}

static inline void dp_timetrack_charge_ticks(struct dp_timetrack *timetrack, u64 diff)
{
	u64 next_ticks = timetrack->ticks + diff;
#ifdef DP_TIMETRACK_MARSHAL_SUPPORT
	if (timetrack->marshal_func != NULL) {
		if (timetrack->ticks <= timetrack->tick_marshal  &&  timetrack->tick_marshal < next_ticks) {
			timetrack->tick_marshal += timetrack->tick_marshal_period;
			timetrack->tick_marshal_save = timetrack->tick_marshal;
			dp_timetrack_marshal_dump(timetrack);
			timetrack->ticks = next_ticks;
			return;
		}
	}
#endif
	timetrack->ticks = next_ticks;
}

static inline enum dp_bool dp_timetrack_host_delay_check(struct dp_timetrack *timetrack)
{
	if (timetrack->ticks > timetrack->host_ticks_keepup) {
		if (timetrack->ticks - timetrack->host_ticks_keepup > timetrack->host_delay_check_delta) {
			if (timetrack->host_time_sync)
				dp_timetrack_host_delay(timetrack);
			else
				timetrack->host_ticks_keepup = timetrack->ticks;
			return DP_TRUE;
		}
	}

	return DP_FALSE;
}

#endif
