#ifndef _DOSPACK_MAIN_H__
#define _DOSPACK_MAIN_H__

#include <setjmp.h>

#include "logging.h"
#include "cpu.h"
#include "memory.h"
#include "paging.h"
#include "dosblock.h"
#include "game_env.h"
#include "marshal.h"
#include "timetrack.h"
#include "bios.h"
#include "video.h"
#include "keyboard.h"
#include "hwtimer.h"
#include "events.h"
#include "platform.h"

#include <ui/ui.h>

struct dospack {
	char last_marshal_filename[0x100];
	u32 marshal_periodically_last_event_index;

	char _marshal_sep[0];

	struct dp_logging logging;
	struct dp_io io;
	struct dp_memory memory;
	struct dp_int_callback int_callback;
	struct dp_paging paging;
	struct dp_pic pic;
	struct dp_cpu cpu;
	struct dp_dosblock dosblock;
	struct dp_game_env game_env;
	struct dp_marshal marshal;
	struct dp_timetrack timetrack;
	struct dp_bios bios;
	struct dp_video video;
	struct dp_keyboard keyboard;
	struct dp_hwtimer hwtimer;
	struct dp_events events;
	struct dp_platform_context platform;

	struct dp_ui ui;

	jmp_buf main_loop_unwind;

	const char *marshal_periodically_dirname;
	int marshal_periodically_events_cond;
};

void dospack_marshal(struct dospack *dospack, const char *filename);\
void dospack_unmarshall(struct dospack *dospack, const char *filename);
struct dospack *dospack_main_entry(int argc, char *argv[], struct dp_platform_ops *ops, void *ptr);
void dospack_window_resize(struct dospack *dospack);

enum dospack_loop_ret {
	DOSPACK_LOOP_RET_RUNNING,
	DOSPACK_LOOP_RET_HOST_SLEEP,
	DOSPACK_LOOP_RET_EXIT,
	DOSPACK_LOOP_RET_ERROR,
};

enum dospack_loop_ret dospack_loop(struct dospack *dospack);
void dospack_draw(struct dospack *dospack);
void dospack_exit(struct dospack *dospack);

#endif
