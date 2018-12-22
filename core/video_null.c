#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_VIDEO
#include <stdlib.h>

#include "video_null.h"
#include "logging.h"

struct dp_video_null {
	int dummy;
	struct dp_video *video;
	void *surface;
};

enum dp_bool dp_null_set_video_mode(void *ptr, int width, int height, int colors)
{
	struct dp_video_null *null = ptr;

	if (null->surface)
		free(null->surface);

	null->surface = malloc(width*height*colors/8);
	return DP_TRUE;
}

void *dp_null_get_video_surface(void *ptr)
{
	struct dp_video_null *null = ptr;

	return null->surface;
}

void dp_null_put_video_surface(void *ptr, int upd_x, int upd_y, int upd_w, int upd_h)
{
}

void dp_null_set_colors(void *ptr, void *array, int index, int colors)
{
}

int dp_null_get_events(void *ptr, struct dp_user_event *events, int max_events)
{
	return 0;
}

static void null_exit(void *ptr)
{
}

static struct dp_video_ops ops = {
	.set_video_mode = dp_null_set_video_mode,
	.get_video_surface = dp_null_get_video_surface,
	.put_video_surface = dp_null_put_video_surface,
	.set_colors = dp_null_set_colors,
	.get_events = dp_null_get_events,
	.exit = null_exit,
};

enum dp_bool dp_video_null_init(struct dp_video *video)
{
	struct dp_video_null *null = (struct dp_video_null *)video->engine_data;

	null->video = video;
	video->ops = &ops;

	return DP_TRUE;
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING

