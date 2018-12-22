#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_TIMETRACK
#define DP_LOGGING           (timetrack->logging)

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "timetrack.h"

static u64 dp_timetack_get_machine_time(void *ptr)
{
	struct dp_timetrack *timetrack = ptr;

	return timetrack->ticks;
}

static inline u64 get_monotonic_clock(struct dp_timetrack *timetrack)
{
	struct timespec tp;
	int ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &tp);

	DP_ASSERT(ret == 0);

	return (((u64)tp.tv_sec) * 1000000000) + tp.tv_nsec;
}

void dp_timetrack_init(struct dp_timetrack *timetrack, struct dp_logging *logging, struct dp_marshal *marshal)
{
	int i;

	memset(timetrack, 0, sizeof(*timetrack));
	timetrack->logging = logging;
	timetrack->ticks = 0;
	timetrack->ticks_per_second = 3000000;
	timetrack->base_unix_time = 631144800; /* January 1st, 1990 */
	timetrack->tick_marshal = -1;
	timetrack->host_ticks_keepup = 0;

	DP_INF("initializing timetrack");

	for (i = 0; i < DP_TIMETRACK_MAX_EVENTS - 1; i++)
		timetrack->events[i].next_event = 2 + (i % DP_TIMETRACK_MAX_EVENTS);
	timetrack->free_event = 1;

	timetrack->host_clock_base = get_monotonic_clock(timetrack);
	timetrack->host_delay_check_delta = timetrack->ticks_per_second / 100;
	timetrack->host_time_sync = DP_TRUE;

	dp_log_set_machine_time_cb(logging, dp_timetack_get_machine_time, timetrack);
}

void dp_timetrack_host_time_skip(struct dp_timetrack *timetrack)
{
	timetrack->host_ticks_keepup = timetrack->ticks;
	timetrack->host_clock_base = get_monotonic_clock(timetrack);
}

void dp_timetrack_host_delay(struct dp_timetrack *timetrack)
{
	u64 host_time_elapsed, now, host_ticks, diff;
	u32 artificial_delay;

	while (timetrack->ticks > timetrack->host_ticks_keepup) {
		if (timetrack->ticks > timetrack->host_ticks_keepup) {
			diff = timetrack->ticks - timetrack->host_ticks_keepup;
			artificial_delay = (diff * 100000)/timetrack->ticks_per_second;

			if (artificial_delay > 0) {
				usleep(artificial_delay);
			}
		}

		now = get_monotonic_clock(timetrack);
		host_time_elapsed = now - timetrack->host_clock_base;

		host_ticks = (timetrack->ticks_per_second * host_time_elapsed)/1000000000;

		timetrack->host_ticks_keepup += host_ticks;
		timetrack->host_clock_base = now;
	}

	timetrack->host_ticks_keepup = timetrack->ticks;
}

void dp_timetrack_run_events(struct dp_timetrack *timetrack)
{
	struct dp_timetrack_event *event;
	dp_timetrack_event_index_t next_event;

	next_event = timetrack->next_event;
	while (next_event != DP_TIMETRACK_NULL_EVENT) {
		event = dp_timetrack_get_event(timetrack, next_event);
		if (event->exe_time > timetrack->ticks)
			break;

		DP_DBG("running event #%d", next_event);

		timetrack->next_event = event->next_event;
		event->cb(event->cb_data);

		event->next_event = timetrack->free_event;
		event->cb = NULL;
		event->cb_data = NULL;
		event->exe_time = 0;
		timetrack->free_event = next_event;
		next_event = timetrack->next_event;
	}
}

void dp_timetrack_add_event(struct dp_timetrack *timetrack, dp_timetrack_event_cb_t cb, void *cb_data, u64 delay)
{
	struct dp_timetrack_event *event, *closest_event = NULL;
	dp_timetrack_event_index_t next_event, cur_event;
	dp_timetrack_event_index_t *last_link;

	DP_ASSERT(timetrack->free_event != DP_TIMETRACK_NULL_EVENT);

	cur_event = timetrack->free_event;
	event = dp_timetrack_get_event(timetrack, cur_event);
	timetrack->free_event = event->next_event;
	event->exe_time = timetrack->ticks + delay;
	event->cb = cb;
	event->cb_data = cb_data;

	last_link = &timetrack->next_event;
	next_event = timetrack->next_event;
	while (next_event != DP_TIMETRACK_NULL_EVENT) {
		closest_event = dp_timetrack_get_event(timetrack, next_event);
		if (closest_event->exe_time > event->exe_time)
			break;

		next_event = closest_event->next_event;
		last_link = &closest_event->next_event;
	}

	event->next_event = next_event;

	DP_DBG("scheduling event #%d to run %lld ticks from now, next event is #%d", cur_event, delay, event->next_event);

	*last_link = cur_event;
}

void dp_timetrack_remove_event(struct dp_timetrack *timetrack, dp_timetrack_event_cb_t cb)
{
	struct dp_timetrack_event *event;
	dp_timetrack_event_index_t next_event, this_event;
	dp_timetrack_event_index_t *last_link;

	this_event = timetrack->next_event;
	last_link = &timetrack->next_event;

	while (this_event != DP_TIMETRACK_NULL_EVENT) {
		event = dp_timetrack_get_event(timetrack, this_event);
		next_event = event->next_event;

		if (event->cb == cb) {
			DP_DBG("removing event #%d", this_event);

			event->cb = NULL;
			event->cb_data = NULL;
			event->exe_time = 0;
			event->next_event = timetrack->free_event;
			timetrack->free_event = this_event;
			*last_link = next_event;
			continue;
		}

		this_event = next_event;
		last_link = &event->next_event;
	}
}

void dp_timetrack_remove_event_data(struct dp_timetrack *timetrack, dp_timetrack_event_cb_t cb, void *cb_data)
{
	struct dp_timetrack_event *event;
	dp_timetrack_event_index_t next_event, this_event;
	dp_timetrack_event_index_t *last_link;

	this_event = timetrack->next_event;
	last_link = &timetrack->next_event;

	while (this_event != DP_TIMETRACK_NULL_EVENT) {
		event = dp_timetrack_get_event(timetrack, this_event);
		next_event = event->next_event;

		if (event->cb == cb  &&  event->cb_data == cb_data) {
			DP_DBG("removing event #%d", this_event);

			event->cb = NULL;
			event->cb_data = NULL;
			event->exe_time = 0;
			event->next_event = timetrack->free_event;
			timetrack->free_event = this_event;
			*last_link = next_event;
			continue;
		}

		this_event = next_event;
		last_link = &event->next_event;
	}
}

void dp_timetrack_get_timestamp(struct dp_timetrack *timetrack, int *year, int *month, int *day,
				int *hour, int *min, int *sec, int *usec)

{
	time_t t = timetrack->base_unix_time;
	struct tm x;

	t += timetrack->ticks / timetrack->ticks_per_second;
	gmtime_r(&t, &x);

	if (year)
		*year = x.tm_year;

	if (month)
		*month = x.tm_mon;

	if (day)
		*day = x.tm_mday;

	if (hour)
		*hour = x.tm_hour;

	if (min)
		*min = x.tm_min;

	if (sec)
		*sec = x.tm_sec;

	if (usec) {
		*usec = 1000000*((double)(timetrack->ticks % timetrack->ticks_per_second))/timetrack->ticks_per_second;
		if (*usec == 1000000)
			*usec = 999999;
	}

}

void dp_timetrack_marshal(struct dp_timetrack *timetrack, struct dp_marshal *marshal)
{
	dp_marshal_write_version(marshal, 1);

	dp_marshal_write(marshal, timetrack, offsetof(struct dp_timetrack, _marshal_sep));
}

void dp_timetrack_unmarshal(struct dp_timetrack *timetrack, struct dp_marshal *marshal)
{
	u32 version = 0;
	int i;

	dp_marshal_read_version(marshal, &version);
	DP_ASSERT(version == 1);

	dp_marshal_read(marshal, timetrack, offsetof(struct dp_timetrack, _marshal_sep));

	for (i = 0; i < DP_TIMETRACK_MAX_EVENTS; i++) {
		dp_marshal_read_ptr_fix(marshal, (void **)&timetrack->events[i].cb);
		dp_marshal_read_ptr_fix(marshal, (void **)&timetrack->events[i].cb_data);
	}

	if (timetrack->tick_marshal_period != 0) {
		timetrack->tick_marshal = timetrack->tick_marshal_save;
	}
}

void dp_timetrack_marshal_dump(struct dp_timetrack *timetrack)
{
	char filename[0x30];

	snprintf(filename, sizeof(filename), "timetrack-%015llu.dp", timetrack->ticks);

	timetrack->marshal_func(timetrack->marshal_data, filename);
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
