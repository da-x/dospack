#ifndef _DOSPACK_VIDEO_H__
#define _DOSPACK_VIDEO_H__

#include "common.h"
#include "logging.h"
#include "marshal.h"
#include "int_callback.h"
#include "cpu.h"
#include "io.h"
#include "timetrack.h"
#include "events.h"
#include "keyboard.h"
#include "platform.h"

#include <ui/ui.h>

#define VGAREG_VGA_WRITE_FEATURE_CTL   0x3da

#define VGAREG_PEL_MASK                0x3c6
#define VGAREG_DAC_READ_ADDRESS        0x3c7
#define VGAREG_DAC_WRITE_ADDRESS       0x3c8
#define VGAREG_DAC_DATA                0x3c9

struct dp_video_ops {
	enum dp_bool (*set_video_mode)(void *ptr, int width, int height, int colors);
	void *(*get_video_surface)(void *ptr);
	void (*exit)(void *ptr);
	void (*put_video_surface)(void *ptr, int upd_x, int upd_y, int upd_w, int upd_h);
	void (*set_colors)(void *ptr, void *array, int index, int colors);
};

struct dp_video {
	u8 machine_video_mode;
	u32 frame_size_in_ticks;
	u64 cur_frame;
	u8 machine_palette[256*3];

	char _marshal_sep[0];

	struct dp_logging *logging;
	struct dp_cpu *cpu;
	struct dp_timetrack *timetrack;
	struct dp_keyboard *keyboard;
	struct dp_platform_context *platform;
	struct dp_ui *ui;

	struct dp_video_ops *ops;
	char engine_data[0x1000];
	u8 actual_video_mode;
	enum dp_bool full_screen;
};

void dp_video_init(struct dp_video *video, struct dp_logging *logging, struct dp_marshal *marshal,
		   struct dp_cpu *cpu, struct dp_int_callback *int_callback, struct dp_io *io,
		   struct dp_timetrack *timetrack, struct dp_keyboard *keyboard, struct dp_ui *ui,
		   enum dp_bool full_screen, struct dp_platform_context *platform);
hwticks_t dp_video_get_ticks_until_vblank(struct dp_video *video);
void dp_video_fini(struct dp_video *video);
void dp_video_marshal(struct dp_video *video, struct dp_marshal *marshal);
void dp_video_unmarshal(struct dp_video *video, struct dp_marshal *marshal);

#endif
