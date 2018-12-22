#ifndef _DOSPACK_PLATFORM_H__
#define _DOSPACK_PLATFORM_H__

struct dp_platform_ops {
	void (*frame_ready)(void *ptr);
	int (*get_events)(void *ptr, struct dp_user_event *events, int max_events);
	void (*get_viewport_size)(void *ptr, int *x, int *y);
};

struct dp_platform_context {
	struct dp_platform_ops *ops;
	void *ptr;
};

#endif
