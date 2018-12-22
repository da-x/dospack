#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_MAIN
#define DP_LOGGING           (&dospack->logging)

#include "dospack.h"
#include "memory.h"
#include "games.h"

#include <games/dave/dave.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static u8 global_memory[0x100000];

struct abort_handler {
	struct dospack *dospack;
	const char *marshal_on_stop;
	int dumpmem;
	int halt_on_exit;
} abort_handler;

void dospack_abort_callback(void)
{
	struct dospack *dospack = abort_handler.dospack;

	if (dospack) {
		dospack->logging.abort = NULL;

		if (abort_handler.dumpmem)
			dp_mem_dump(&dospack->memory);
		if (abort_handler.marshal_on_stop)
			dospack_marshal(dospack, abort_handler.marshal_on_stop);
	}

	if (abort_handler.halt_on_exit) {
		pause();
	}

	abort();
}

enum dospack_loop_ret dospack_loop(struct dospack *dospack)
{
	struct dp_cpu_decoder *decoder = &dospack->cpu.decoder;
	s32 ret;

	ret = setjmp(dospack->main_loop_unwind);
	if (ret == 0) {
		ret = decoder->func(&dospack->cpu);
		if (ret > 0) {
			u32 cb_index = ret;
			struct dp_int_callback_desc *cb = &dospack->int_callback.list[cb_index];
			ret = cb->func(cb->ptr);
			if (ret) {
				DP_DBG("CB %d returned %d", cb_index, ret);
				return DOSPACK_LOOP_RET_ERROR;
			}
		} else if (ret < 0) {
			DP_DBG("Decode func returned %d", ret);
			return DOSPACK_LOOP_RET_ERROR;
		}
	}

	dp_timetrack_run_events_check(&dospack->timetrack);

	ret = dp_timetrack_host_delay_check(&dospack->timetrack);
	if (ret) {
		const int max_events = 0x10;
		struct dp_user_event events[max_events], *event;
		struct dp_game *game_desc = dospack->game_env.game_desc;
		int num_events, i, ret;

		num_events = dospack->platform.ops->get_events(dospack->platform.ptr, events, max_events);
		for (i = 0; i < num_events; i++) {
			event = &events[i];

			if (game_desc->ops->user_event) {
				ret = game_desc->ops->user_event(&dospack->game_env, event);
				if (ret)
					return DOSPACK_LOOP_RET_EXIT;
			}
		}

		return DOSPACK_LOOP_RET_HOST_SLEEP;
	}

	return DOSPACK_LOOP_RET_RUNNING;
}

void dospack_draw(struct dospack *dospack)
{
	dp_ui_render(&dospack->ui);
}

void dospack_marshal(struct dospack *dospack, const char *filename)
{
	char final_filename[0x100];

	if (dospack->marshal_periodically_events_cond  &&
	    dospack->marshal_periodically_last_event_index == dospack->events.event_index)
		return;

	dospack->marshal_periodically_last_event_index = dospack->events.event_index;

	if (dospack->marshal_periodically_dirname) {
		snprintf(final_filename, sizeof(final_filename),
			 "%s/%s", dospack->marshal_periodically_dirname, filename);
	} else {
		snprintf(final_filename, sizeof(final_filename), "%s", filename);
	}

	DP_INF("marshalling to file '%s'", final_filename);

	dp_marshal_start_dump(&dospack->marshal, final_filename);
	dp_marshal_write_version(&dospack->marshal, 1);

	dp_marshal_write(&dospack->marshal, dospack, offsetof(struct dospack, _marshal_sep));

	dp_timetrack_marshal(&dospack->timetrack, &dospack->marshal);
	dp_io_marshal(&dospack->io, &dospack->marshal);
	dp_mem_marshal(&dospack->memory, &dospack->marshal);
	dp_int_callback_marshal(&dospack->int_callback, &dospack->marshal);
	dp_paging_marshal(&dospack->paging, &dospack->marshal);
	dp_pic_marshal(&dospack->pic, &dospack->marshal);
	dp_hwtimer_marshal(&dospack->hwtimer, &dospack->marshal);
	dp_bios_marshal(&dospack->bios, &dospack->marshal);
	dp_cpu_marshal(&dospack->cpu, &dospack->marshal);
	dp_events_marshal(&dospack->events, &dospack->marshal);
	dp_video_marshal(&dospack->video, &dospack->marshal);
	dp_keyboard_marshal(&dospack->keyboard, &dospack->marshal);
	dp_dosblock_marshal(&dospack->dosblock, &dospack->marshal);

	dp_marshal_end(&dospack->marshal);

	snprintf(dospack->last_marshal_filename,
		 sizeof(dospack->last_marshal_filename),
		 "%s", final_filename);
}

void dospack_unmarshall(struct dospack *dospack, const char *filename)
{
	char final_filename[0x100];
	u32 version = 0;

	snprintf(final_filename, sizeof(final_filename), "%s", filename);

	DP_INF("unmarshalling from file '%s'", filename);

	dp_marshal_start_load(&dospack->marshal, filename);
	dp_marshal_read_version(&dospack->marshal, &version);
	DP_ASSERT(version == 1);

	dp_marshal_read(&dospack->marshal, dospack, offsetof(struct dospack, _marshal_sep));

	DP_DBG("timetrack unmarshal");
	dp_timetrack_unmarshal(&dospack->timetrack, &dospack->marshal);
	DP_DBG("io unmarshal");
	dp_io_unmarshal(&dospack->io, &dospack->marshal);
	DP_DBG("mem unmarshal");
	dp_mem_unmarshal(&dospack->memory, &dospack->marshal);
	DP_DBG("int_callback unmarshal");
	dp_int_callback_unmarshal(&dospack->int_callback, &dospack->marshal);
	DP_DBG("paging unmarshal");
	dp_paging_unmarshal(&dospack->paging, &dospack->marshal);
	DP_DBG("pic unmarshal");
	dp_pic_unmarshal(&dospack->pic, &dospack->marshal);
	DP_DBG("hwtimer unmarshal");
	dp_hwtimer_unmarshal(&dospack->hwtimer, &dospack->marshal);
	DP_DBG("bios unmarshal");
	dp_bios_unmarshal(&dospack->bios, &dospack->marshal);
	DP_DBG("cpu unmarshal");
	dp_cpu_unmarshal(&dospack->cpu, &dospack->marshal);
	DP_DBG("events unmarshal");
	dp_events_unmarshal(&dospack->events, &dospack->marshal);
	DP_DBG("video unmarshal");
	dp_video_unmarshal(&dospack->video, &dospack->marshal);
	DP_DBG("keyboard unmarshal");
	dp_keyboard_unmarshal(&dospack->keyboard, &dospack->marshal);
	DP_DBG("dosblock unmarshal");
	dp_dosblock_unmarshal(&dospack->dosblock, &dospack->marshal);

	/* Post unmarshalling */
	dp_events_post_unmarshal(&dospack->events, &dospack->marshal);

	dp_marshal_end(&dospack->marshal);

	if (strlen(dospack->last_marshal_filename) == 0) {
		snprintf(dospack->last_marshal_filename,
			 sizeof(dospack->last_marshal_filename),
			 "%s", final_filename);
	}

	DP_INF("will rewind from '%s'", dospack->last_marshal_filename);
}

void dospack_init(struct dospack *dospack, enum dp_bool full_screen)
{
	dp_marshal_init(&dospack->marshal, &dospack->logging);

	dp_timetrack_init(&dospack->timetrack, &dospack->logging, &dospack->marshal);
	dp_io_init(&dospack->io, &dospack->logging, &dospack->marshal);
	// NOT FULLY IMPLEMENTED:
	dp_mem_init(&dospack->memory, &dospack->logging, sizeof(global_memory), &global_memory, &dospack->marshal);
	dp_int_callback_init(&dospack->int_callback, &dospack->memory, &dospack->logging, &dospack->marshal);
	// NOT FULLY IMPLEMENTED:
	dp_paging_init(&dospack->paging, &dospack->memory, &dospack->logging, &dospack->marshal);
	// NOT FULLY IMPLEMENTED:
	dp_pic_init(&dospack->pic, &dospack->logging, &dospack->marshal, &dospack->io);
	// MISSING: Timer
	// MISSING: CMOS
	// MISSING: Render
	dp_cpu_init(&dospack->cpu, &dospack->logging, &dospack->memory, &dospack->io, &dospack->paging, &dospack->pic,
		    &dospack->int_callback, &dospack->timetrack, &dospack->marshal);
	dp_bios_init(&dospack->bios, &dospack->logging, &dospack->marshal, &dospack->cpu, &dospack->int_callback);
	dp_hwtimer_init(&dospack->hwtimer, &dospack->logging, &dospack->marshal, &dospack->pic, &dospack->io,
			&dospack->timetrack);
	// MISSING: FPU
	// MISSING: DMA
	// MISSING: VGA
	// MISSING: KEYBOARD
	// MISSING: MIXER
	// MISSING: MIDI/MPU401
	// MISSING: SBLASTER/GUS/PCSPEAKER
	// MISSING: BIOS INT10 services
	// MISSING: Mouse/joystick
	// MISSING: Serial

	// NOT FULLY IMPLEMENTED: DOS
	dp_keyboard_init(&dospack->keyboard, &dospack->logging, &dospack->marshal, &dospack->cpu, &dospack->int_callback, &dospack->io,
			 &dospack->timetrack, &dospack->pic, &dospack->hwtimer);
	dp_events_init(&dospack->events, &dospack->logging, &dospack->timetrack,
		       &dospack->marshal, &dospack->keyboard);
	dp_ui_init(&dospack->ui, &dospack->logging, &dospack->platform);
	dp_video_init(&dospack->video, &dospack->logging, &dospack->marshal, &dospack->cpu, &dospack->int_callback, &dospack->io,
		      &dospack->timetrack, &dospack->keyboard, &dospack->ui, full_screen, &dospack->platform);
	dp_dosblock_init(&dospack->dosblock, &dospack->memory, &dospack->logging, &dospack->int_callback, &dospack->cpu, &dospack->marshal,
			 &dospack->timetrack);
	dp_ui_reset(&dospack->ui);
	dp_marshal_init_done(&dospack->marshal);

	// MISSING: CDROM/MSCDEX/Drives
	// NOT NEEDED? DOS SHELL

}

void dospack_main(struct dospack *dospack, const char *logging_spec, struct dp_game *game_desc, const char *marshal_load_filename,
		  s64 tick_marshal, int disable_host_time_sync, int full_screen, const char *record_events_filename,
		  const char *marshal_periodically_dirname, s64 tick_marshal_period, int marshal_periodically_events_cond,
		  const char *replay_events_filename)
{
	dospack->game_env.game_desc = game_desc;
	dp_log_init(&dospack->logging, logging_spec);
	dospack->game_env.logging = &dospack->logging;
	dospack->game_env.dospack = dospack;
	dospack->logging.abort = dospack_abort_callback;

	DP_INF("new dospack running, game %s", game_desc->name);

	dospack_init(dospack, full_screen);

	DP_INF("memory footprint: %d bytes", sizeof(*dospack) + dospack->memory.size);

	if (tick_marshal_period != -1) {
		dospack->timetrack.tick_marshal_period = tick_marshal_period;
	}

	if (game_desc->ops->init)
	    game_desc->ops->init(&dospack->game_env);

	if (marshal_load_filename) {
		dospack_unmarshall(dospack, marshal_load_filename);
	} else {
		dp_dosblock_load(&dospack->dosblock, &dospack->game_env);
		if (game_desc->ops->after_exe)
			game_desc->ops->after_exe(&dospack->game_env);
	}

	if (record_events_filename) {
		dp_events_start_record(&dospack->events, record_events_filename);
	}

	if (replay_events_filename) {
		dp_events_start_replay(&dospack->events, replay_events_filename);
	}

	if (tick_marshal != -1) {
		dospack->timetrack.tick_marshal = tick_marshal;
		dospack->timetrack.marshal_func = (void (*)(void *, const char *))(dospack_marshal);
		dospack->timetrack.marshal_data = dospack;
	}

	if (tick_marshal_period != -1) {
		DP_INF("will marshal every %lld ticks", tick_marshal_period);
		dospack->marshal_periodically_dirname = marshal_periodically_dirname;
		dospack->marshal_periodically_events_cond = marshal_periodically_events_cond;
		dospack->timetrack.tick_marshal_period = tick_marshal_period;
		if (dospack->timetrack.tick_marshal == -1) {
			dospack->timetrack.tick_marshal = tick_marshal_period;
		}
		dospack->timetrack.marshal_func = (void (*)(void *, const char *))(dospack_marshal);
		dospack->timetrack.marshal_data = dospack;
	}

	if (disable_host_time_sync) {
		dospack->timetrack.host_time_sync = DP_FALSE;
	}

	abort_handler.dospack = dospack;
}

struct dospack *dospack_main_entry(int argc, char *argv[], struct dp_platform_ops *ops, void *ptr)
{
	struct dospack *dospack = NULL;
	const char *logging_spec = NULL;
	const char *marshal_on_stop = NULL;
	const char *marshal_on_tick = NULL;
	const char *marshal_periodically_dirname = NULL;
	const char *marshal_periodically_time = NULL;
	const char *marshal_load_filename = NULL;
	const char *record_events_filename = NULL;
	const char *replay_events_filename = NULL;
	s64 tick_marshal = -1, tick_marshal_period = -1;
	int i, disable_host_time_sync = 0;
	int full_screen = 0;
	int marshal_periodically_events_cond = 0;

	dospack = (struct dospack *)malloc(sizeof(*dospack));
	if (!dospack) {
		fprintf(stderr, "dospack: Could not allocate memory\n");
		return NULL;
	}

	memset(dospack, 0, sizeof(*dospack));
	dospack->platform.ptr = ptr;
	dospack->platform.ops = ops;

	for (i = 1; i < argc; i++) {
		if (!strcmp("--dumpmem", argv[i]))
			abort_handler.dumpmem = 1;
		if (!strcmp("-T", argv[i]))
			disable_host_time_sync = 1;
		if (!strcmp("--full-screen", argv[i]))
			full_screen = 1;
		if (!strcmp("--halt-on-exit", argv[i]) || !strcmp("-H", argv[i]))
			abort_handler.halt_on_exit = 1;
		if (!strcmp("--marshal-periodically-events-cond", argv[i]))
			marshal_periodically_events_cond = 1;

		if (!(i < argc - 1))
			continue;

		if (!strcmp("--logging", argv[i]) || !strcmp("-l", argv[i]))
			logging_spec = argv[i + 1];
		if (!strcmp("--record-events", argv[i]) || !strcmp("-R", argv[i]))
			record_events_filename = argv[i + 1];
		if (!strcmp("--replay-events", argv[i]) || !strcmp("-r", argv[i]))
			replay_events_filename = argv[i + 1];
		if (!strcmp("--marshal-on-stop", argv[i]) || !strcmp("-S", argv[i]))
			marshal_on_stop = argv[i + 1];
		if (!strcmp("--marshal-on-tick", argv[i]) || !strcmp("-m", argv[i]))
			marshal_on_tick = argv[i + 1];
		if (!strcmp("--marshal-load", argv[i]) || !strcmp("-M", argv[i]))
			marshal_load_filename = argv[i + 1];

		if (!(i < argc - 2))
			continue;

		if (!strcmp("--marshal-periodically", argv[i]) || !strcmp("-P", argv[i])) {
			marshal_periodically_dirname = argv[i + 1];

			// Microseconds
			marshal_periodically_time = argv[i + 2];
		}
	}

	if (marshal_on_stop) {
		abort_handler.marshal_on_stop = marshal_on_stop;
	}
	if (marshal_on_tick) {
		tick_marshal = strtoull(marshal_on_tick, NULL, 10);
	}
	if (marshal_periodically_time) {
		tick_marshal_period = strtoull(marshal_periodically_time, NULL, 10);
	}

	dospack_main(dospack, logging_spec, &dp_game_dave, marshal_load_filename, tick_marshal,
		     disable_host_time_sync, full_screen, record_events_filename,
		     marshal_periodically_dirname, tick_marshal_period, marshal_periodically_events_cond,
		     replay_events_filename);

	return dospack;
}

void dospack_window_resize(struct dospack *dospack)
{
	dp_ui_reset(&dospack->ui);
}

void dospack_exit(struct dospack *dospack)
{
	dp_video_fini(&dospack->video);

	DP_INF("ended!");

	if (abort_handler.dumpmem)
		dp_mem_dump(&dospack->memory);

	free(dospack);
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
