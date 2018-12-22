#ifndef _DOSPACK_EVENTS_H__
#define _DOSPACK_EVENTS_H__

#include "keyboard.h"

enum dp_user_event_type {
	DP_USER_EVENT_TYPE_INVALID,
	DP_USER_EVENT_TYPE_KEYBOARD,
	DP_USER_EVENT_TYPE_POINTER,
	DP_USER_EVENT_TYPE_QUIT,
	DP_USER_EVENT_TYPE_REWIND,
};

enum dp_pointer_event_type {
	DP_POINTER_EVENT_TYPE_MOVE,
	DP_POINTER_EVENT_TYPE_UP,
	DP_POINTER_EVENT_TYPE_DOWN,
};

struct dp_user_event {
	enum dp_user_event_type type;
	union {
		struct {
			enum dp_key key;
			int is_up;
		} keyboard;

		struct {
			enum dp_pointer_event_type type;
			int index;
			int x, y;
		} pointer;
	};
};

struct dp_timed_user_event {
	hwticks_t time;
	struct dp_user_event event;
};

struct dp_events {
	u32 event_index;
	u8 key_state[DP_KEY_LAST];

	char _marshal_sep[0];

	struct dp_logging *logging;
	struct dp_keyboard *keyboard;
	struct dp_timetrack *timetrack;

	FILE *record_file;
	FILE *replay_file;
	struct dp_timed_user_event timed_event_replay;
};

void dp_events_init(struct dp_events *events, struct dp_logging *logging,
		    struct dp_timetrack *timetrack, struct dp_marshal *marshal,
		    struct dp_keyboard *keyboard);
void dp_events_start_replay(struct dp_events *events, const char *filename);
void dp_events_start_record(struct dp_events *events, const char *filename);
void dp_events_inject_now(struct dp_events *events, struct dp_user_event *event);

void dp_events_marshal(struct dp_events *events, struct dp_marshal *marshal);
void dp_events_post_unmarshal(struct dp_events *events, struct dp_marshal *marshal);
void dp_events_unmarshal(struct dp_events *events, struct dp_marshal *marshal);

#endif
