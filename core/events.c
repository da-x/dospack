#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_EVENTS
#define DP_LOGGING           (events->logging)

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>

#include "events.h"

void dp_events_init(struct dp_events *events, struct dp_logging *logging,
		    struct dp_timetrack *timetrack, struct dp_marshal *marshal,
		    struct dp_keyboard *keyboard)
{
	memset(events, 0, sizeof(*events));
	events->logging = logging;
	events->timetrack = timetrack;
	events->keyboard = keyboard;

	DP_INF("initializing EVENTS");
}

void dp_events_inject_now(struct dp_events *events, struct dp_user_event *event)
{
	size_t s;

	DP_DBG("RECORD %d: %lld", events->event_index, events->timetrack->ticks);

	if (events->record_file != NULL) {
		struct dp_timed_user_event timed_event;

		timed_event.event = *event;
		timed_event.time = events->timetrack->ticks;

		s = fwrite(&timed_event, sizeof(timed_event), 1, events->record_file);
		DP_ASSERT(s == 1);
		fflush(events->record_file);
	}

	events->event_index++;

	switch (event->type) {
	case DP_USER_EVENT_TYPE_INVALID:
		break;
	case DP_USER_EVENT_TYPE_QUIT:
		break;
	case DP_USER_EVENT_TYPE_POINTER:
		break;
	case DP_USER_EVENT_TYPE_KEYBOARD:
		if (event->keyboard.key >= 0  &&   event->keyboard.key < DP_KEY_LAST) {
			events->key_state[event->keyboard.key] = !event->keyboard.is_up;
		}
		dp_keyboard_add_key(events->keyboard, event->keyboard.key, !event->keyboard.is_up);
		break;
	case DP_USER_EVENT_TYPE_REWIND:
		break;
	}
}

void dp_events_replay_one_event(struct dp_events *events);

static void dp_events_timetrack_cb(void *ptr)
{
	struct dp_events *events = ptr;

	DP_DBG("REPLAY %d: %lld %lld", events->event_index, events->timed_event_replay.time, events->timetrack->ticks);

	dp_events_inject_now(events, &events->timed_event_replay.event);
	dp_events_replay_one_event(events);
}

void dp_events_replay_one_event(struct dp_events *events)
{
	hwticks_t delay;
	size_t s;

	while (events->replay_file != NULL) {
		s = fread(&events->timed_event_replay, sizeof(events->timed_event_replay), 1, events->replay_file);
		if (s == 0) {
			fclose(events->replay_file);
			events->replay_file = NULL;
			break;
		}
		DP_ASSERT(s == 1);

		if (events->timed_event_replay.time >= events->timetrack->ticks) {
			delay = events->timed_event_replay.time - events->timetrack->ticks;
		} else {
			continue;
		}

		dp_timetrack_add_event(events->timetrack, dp_events_timetrack_cb, events, delay);
		break;
	}
}

void dp_events_record_seek_event_index(struct dp_events *events)
{
	if (events->record_file != NULL) {
		off_t new_length = events->event_index * sizeof(struct dp_timed_user_event);

		fseek(events->record_file, new_length, SEEK_SET);
		int ret = ftruncate(fileno(events->record_file), new_length);
		assert(ret >= 0);

		DP_INF("restarting recording at index %d", events->event_index);
	}
}

void dp_events_start_replay(struct dp_events *events, const char *filename)
{
	events->replay_file = fopen(filename, "rb");

	DP_ASSERT(events->replay_file != NULL);

	dp_events_replay_one_event(events);
}

void dp_events_start_record(struct dp_events *events, const char *filename)
{
	if (events->event_index != 0) {
		events->record_file = fopen(filename, "r+b");
		dp_events_record_seek_event_index(events);
	} else {
		events->record_file = fopen(filename, "wb");
	}

	DP_ASSERT(events->record_file != NULL);
}

void dp_events_marshal(struct dp_events *events, struct dp_marshal *marshal)
{
	dp_marshal_write_version(marshal, 1);
	dp_marshal_write(marshal, events, offsetof(struct dp_events, _marshal_sep));
}

void dp_events_unmarshal(struct dp_events *events, struct dp_marshal *marshal)
{
	u32 version = 0;
	dp_marshal_read_version(marshal, &version);
	DP_ASSERT(version == 1);
	dp_marshal_read(marshal, events, offsetof(struct dp_events, _marshal_sep));

	dp_events_record_seek_event_index(events);
}

void dp_events_post_unmarshal(struct dp_events *events, struct dp_marshal *marshal)
{
	int i;

	/* Released pressed keys for marshalling */

	for (i = 0; i < DP_KEY_LAST; i++) {
		if (events->key_state[i]) {
			struct dp_user_event event;

			event.type = DP_USER_EVENT_TYPE_KEYBOARD;
			event.keyboard.key = i;
			event.keyboard.is_up = 1;

			dp_events_inject_now(events, &event);
		}
	}
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
