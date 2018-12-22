#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_GAME
#define DP_LOGGING           (game_env->logging)

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include "dave.h"

#include <core/logging.h>
#include <core/cpu_inlines.h>
#include <core/dospack.h>
#include <core/io.h>
#include <core/video.h>
#include <ui/ui.h>

static u8 exe_data[] = {
#include "dave_exe.cdata"
};

#define MAX_ACTIVE_POINTERS 5

struct dp_game_dave_data {
	struct {
		struct dp_io_port *port;
		void *user_ptr;
	} vga_sync;

	enum dp_key last_key_down;

	struct dp_active_pointer {
		int x, y;
	} pointers[MAX_ACTIVE_POINTERS];
	int num_pointers;
	char keys_pressed[DP_KEY_LAST];
	float last_angle;
	float last_rel_x;
	float last_rel_y;

	int player_x;
	int player_y;
};

static struct dp_fregfile_const exe_file_const = {
	.data = exe_data,
	.size = sizeof(exe_data),
};

static struct dp_fregfile exe_file = {
	.class = DP_FREGFILE_CLASS_CONST,
	.u = {
		.fconst = &exe_file_const,
	},
};

static struct dp_fnode root_files[] = {
	{
		.str_name = "DAVE.EXE",
		.type = DP_FNODE_TYPE_REGFILE,
		.u = {
			.regfile = &exe_file,
		},
	},
	{
		.type = DP_FNODE_TYPE_NONE,
	},
};

static struct dp_fdirectory root_dir = {
	.nodes = root_files
};

static u8 vga_write_feature_ctl_read8_hook(void *user_ptr, u32 port)
{
	struct dp_game_env *game_env = user_ptr;
	struct dospack *dospack = game_env->dospack;
	struct dp_memory *memory = &dospack->memory;
	struct dp_cpu *cpu = &dospack->cpu;
	struct dp_game_dave_data *game_data = game_env->game_data;
	phys_addr_t cseip = dospack->cpu.decoder.core.cseip;

	if (/* in  al, dx */
	    dp_memv_readb(memory, cseip-1) == 0xec  &&

	    /* test al, 08 */
	    dp_memv_readb(memory, cseip+0) == 0xa8  &&
	    dp_memv_readb(memory, cseip+1) == 0x08  &&

	    /* je $-5 */
	    dp_memv_readb(memory, cseip+2) == 0x74  &&
	    dp_memv_readb(memory, cseip+3) == 0xfb)
	{
		hwticks_t time_to_spin;

		/* Pretend that we did all the cycles until the next vblank */
		time_to_spin = dp_video_get_ticks_until_vblank(&dospack->video);
		if (time_to_spin > 3) {
			time_to_spin -= 3;
			DP_DBG("OPTIMIZATION: skipping %lld cycles for next vblank", time_to_spin);

			dp_timetrack_charge_ticks(&dospack->timetrack, time_to_spin);
			cpu->decode_count = 0;

			/* Redo this instruction, but run events and IRQs before that */
			longjmp(dospack->main_loop_unwind, 1);

			/*
			 * Optionally we can jump directly outside of the loop:
			 * dcr_reg_eip = cseip + 4 - dp_cpu_seg_phys(dp_seg_cs);
			 */
		}
	}

	return game_data->vga_sync.port->read8(game_data->vga_sync.user_ptr, port);
}

struct dp_io_port vga_loop_io_interception = {
	.read8 = vga_write_feature_ctl_read8_hook,
};


static int update_keypress(struct dp_game_env *game_env, enum dp_key key, enum dp_bool is_pressed)
{
	struct dp_game_dave_data *game_data = game_env->game_data;
	struct dp_user_event kevent = {.type = DP_USER_EVENT_TYPE_KEYBOARD};

	kevent.keyboard.key = key;

	if (game_data->keys_pressed[key]  &&  !is_pressed) {
		/* Release key */
		kevent.keyboard.is_up = 1;
	} else if (!game_data->keys_pressed[key]  &&  is_pressed) {
		/* Press key */
		kevent.keyboard.is_up = 0;
	} else {
		return 0;
	}

	dp_events_inject_now(&game_env->dospack->events, &kevent);
	game_data->keys_pressed[key] = is_pressed;
	return 1;
}

float safe_atanfr(float x, float y)
{
	if (fabs(y) * 10000 < fabs(x)) {
		if (y * x < 0)
			return -M_PI/2;
		return M_PI/2;
	}

	return atanf(x/y);
}

float calc_angle(float x, float y)
{
	float a = safe_atanfr(x, y)/M_PI*180;

	if (x >= 0) {
		if (y < 0) {
			a += 180.0;
		}
	} else {
		if (y < 0) {
			a += 180.0;
		} else {
			a += 360.0;
		}
	}

	return a;
}

static void dp_game_dave_update_keys(struct dp_game_env *game_env)
{
	struct dp_game_dave_data *game_data = game_env->game_data;
	float rel_x = 0, player_x, d_x;
	float rel_y = 0, player_y, d_y;
	float d, a, z = 25;

	rel_x = game_data->last_rel_x;
	rel_y = game_data->last_rel_y;
	player_x = game_data->player_x / 320.0;
	player_y = game_data->player_y / 200.0;
	d_x = (rel_x - player_x)*(320/200.0);
	d_y = (rel_y - player_y);
	d = sqrt(d_x * d_x + d_y * d_y);

	if (d < 0.04) {
		a = game_data->last_angle;
	} else {
		a = calc_angle(d_x, d_y);
		game_data->last_angle = a;
	}

	DP_INF("X: %2.4f, %2.4f -> %2.4f", d_x, d_y, a);

	update_keypress(game_env, DP_KEY_RIGHT, a >= z  &&  a < 180 - z);
	update_keypress(game_env, DP_KEY_LEFT, a >= 180 + z &&  a < 360 - z);
	update_keypress(game_env, DP_KEY_UP, a >= 90 + z  &&  a < 270 - z);
	update_keypress(game_env, DP_KEY_DOWN, a >= 270 - z ||  a < 90 + z);
}

static void dp_game_cpu_instruction_hook_f(void *hook_data)
{
	struct dp_game_env *game_env = hook_data;
	struct dospack *dospack = game_env->dospack;
	struct dp_memory *memory = &dospack->memory;
	struct dp_cpu *cpu = &dospack->cpu;
	phys_addr_t cseip = dospack->cpu.decoder.core.cseip;
	u32 dw = dp_memv_readd(memory, cseip);

	if (dw == 0x7abf8b2e) {
		dw = dp_memv_readd(memory, cseip + 4);
		if (dw == 0x8bf80381) {
			int player_x = dcr_reg_ax + 10;
			int player_y = (dcr_reg_bx >> 1) + 10;
			struct dp_game_dave_data *game_data = game_env->game_data;

			game_data->player_x = player_x;
			game_data->player_y = player_y;

			if (game_data->num_pointers > 0) {
				dp_game_dave_update_keys(game_env);
			}
		}
	}
}

static void dp_game_dave_init(struct dp_game_env *game_env)
{
	dp_marshal_register_pointee(&game_env->dospack->marshal, &vga_loop_io_interception, "vga_loop_io");
}

static void dp_game_dave_after_exe(struct dp_game_env *game_env)
{
	if (1) {
		/*
		 * Disable game data tracking for now, because its
		 * incompleteness breaks marshalling
		 */
		return;
	}

	struct dp_game_dave_data *game_data;

	game_data = malloc(sizeof(struct dp_game_dave_data));
	game_data->last_angle = 0;

	memset(game_data, 0, sizeof(*game_data));
	game_env->game_data = game_data;

	game_data->vga_sync.port = game_env->dospack->io.port[VGAREG_VGA_WRITE_FEATURE_CTL];
	game_env->dospack->io.port[VGAREG_VGA_WRITE_FEATURE_CTL] = &vga_loop_io_interception;

	game_data->vga_sync.user_ptr = game_env->dospack->io.user_ptrs[VGAREG_VGA_WRITE_FEATURE_CTL];
	game_env->dospack->io.user_ptrs[VGAREG_VGA_WRITE_FEATURE_CTL] = game_env;
	game_env->dospack->cpu.decoder.inst_hook_func = dp_game_cpu_instruction_hook_f;
	game_env->dospack->cpu.decoder.inst_hook_data = game_env;
}

static void update_pointers(struct dp_game_env *game_env, struct dp_user_event *event)
{
	struct dp_game_dave_data *game_data = game_env->game_data;

	if (event->pointer.type == DP_POINTER_EVENT_TYPE_UP) {
		if (event->pointer.index >= game_data->num_pointers)
			return;

		DP_INF("pointer %d up on %d %d", event->pointer.index, event->pointer.x, event->pointer.y);
		memmove(&game_data->pointers[event->pointer.index],
			&game_data->pointers[event->pointer.index+1],
			sizeof(game_data->pointers[0]) * (game_data->num_pointers - event->pointer.index - 1));
		game_data->num_pointers--;
		return;
	}

	if (event->pointer.type == DP_POINTER_EVENT_TYPE_DOWN) {
		if (event->pointer.index > game_data->num_pointers ||
		    event->pointer.index >= MAX_ACTIVE_POINTERS)
			return;

		DP_INF("pointer %d down on %d %d", event->pointer.index, event->pointer.x, event->pointer.y);

		game_data->pointers[event->pointer.index].x = event->pointer.x;
		game_data->pointers[event->pointer.index].y = event->pointer.y;
		game_data->num_pointers++;
		return;
	}

	if (event->pointer.type == DP_POINTER_EVENT_TYPE_MOVE) {
		// DP_INF("pointer %d move on %d %d", event->pointer.index, event->pointer.x, event->pointer.y);

		game_data->pointers[event->pointer.index].x = event->pointer.x;
		game_data->pointers[event->pointer.index].y = event->pointer.y;
		return;
	}
}

static void dp_game_dave_handle_pointer_to_key_control(struct dp_game_env *game_env, struct dp_user_event *event)
{
	struct dp_game_dave_data *game_data = game_env->game_data;
	struct dp_ui_hit_result result;
	enum dp_bool ret;
	float rel_x = 0;
	float rel_y = 0;
	int pointers = 0, i, c;

	update_pointers(game_env, event);

	for (i = 0; i < game_data->num_pointers; i++) {
		struct dp_active_pointer *ap = &game_data->pointers[i];

		ret = dp_ui_hit(&game_env->dospack->ui, ap->x, ap->y, &result);
		if (ret) {
			DP_DBG("HIT on %.7f %7f with index %d", result.rel_x, result.rel_y, i);

			if (result.sprite->class == DP_UI_SPRITE_CLASS_VIDEO) {
				rel_x += result.rel_x;
				rel_y += result.rel_y;
				pointers++;
			}
		}
	}

	if (pointers == 0)
		goto release_all_keys;

	game_data->last_rel_x = rel_x / pointers;
	game_data->last_rel_y = rel_y / pointers;
	dp_game_dave_update_keys(game_env);
	return;

 release_all_keys:
	/* Release all keys */
	c = 0;
	c += update_keypress(game_env, DP_KEY_RIGHT, DP_FALSE);
	c += update_keypress(game_env, DP_KEY_LEFT, DP_FALSE);
	c += update_keypress(game_env, DP_KEY_UP, DP_FALSE);
	c += update_keypress(game_env, DP_KEY_DOWN, DP_FALSE);

	if (c > 0) {
		DP_INF("releasing %d keys", c);
	}
	return;
}

static int dp_game_dave_user_event(struct dp_game_env *game_env, struct dp_user_event *event)
{
	switch (event->type) {
	case DP_USER_EVENT_TYPE_KEYBOARD:
		dp_events_inject_now(&game_env->dospack->events, event);
		return 0;

	case DP_USER_EVENT_TYPE_POINTER: {
		dp_game_dave_handle_pointer_to_key_control(game_env, event);
		return 0;
	}

	default:
		DP_INF("got event of type %d", event->type);
		break;
	}

	return -1;
}

static struct dp_game_ops game_dave_ops = {
	.init = dp_game_dave_init,
	.after_exe = dp_game_dave_after_exe,
	.user_event = dp_game_dave_user_event,
};

struct dp_game dp_game_dave = {
	.name = "Dave",
	.command_line = "DAVE.EXE",
	.vfs_root = &root_dir,
	.ops = &game_dave_ops,
};
