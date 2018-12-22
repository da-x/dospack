#ifndef _DOSPACK_KEYBOARD_H__
#define _DOSPACK_KEYBOARD_H__

#include "common.h"
#include "logging.h"
#include "marshal.h"
#include "int_callback.h"
#include "cpu.h"
#include "io.h"
#include "timetrack.h"
#include "hwtimer.h"

enum dp_key {
	DP_KEY_NONE,

	DP_KEY_1, DP_KEY_2, DP_KEY_3, DP_KEY_4, DP_KEY_5, DP_KEY_6, DP_KEY_7, DP_KEY_8, DP_KEY_9, DP_KEY_0,
	DP_KEY_Q, DP_KEY_W, DP_KEY_E, DP_KEY_R, DP_KEY_T, DP_KEY_Y, DP_KEY_U, DP_KEY_I, DP_KEY_O, DP_KEY_P,
	DP_KEY_A, DP_KEY_S, DP_KEY_D, DP_KEY_F, DP_KEY_G, DP_KEY_H, DP_KEY_J, DP_KEY_K, DP_KEY_L, DP_KEY_Z,
	DP_KEY_X, DP_KEY_C, DP_KEY_V, DP_KEY_B, DP_KEY_N, DP_KEY_M,
	DP_KEY_F1, DP_KEY_F2, DP_KEY_F3, DP_KEY_F4, DP_KEY_F5, DP_KEY_F6, DP_KEY_F7, DP_KEY_F8, DP_KEY_F9, DP_KEY_F10, DP_KEY_F11, DP_KEY_F12,

	DP_KEY_ESC, DP_KEY_TAB, DP_KEY_BACKSPACE, DP_KEY_ENTER, DP_KEY_SPACE,
	DP_KEY_LEFTALT, DP_KEY_RIGHTALT, DP_KEY_LEFTCTRL, DP_KEY_RIGHTCTRL, DP_KEY_LEFTSHIFT, DP_KEY_RIGHTSHIFT,
	DP_KEY_CAPSLOCK, DP_KEY_SCROLLLOCK, DP_KEY_NUMLOCK,

	DP_KEY_GRAVE, DP_KEY_MINUS, DP_KEY_EQUALS, DP_KEY_BACKSLASH, DP_KEY_LEFTBRACKET, DP_KEY_RIGHTBRACKET,
	DP_KEY_SEMICOLON, DP_KEY_QUOTE, DP_KEY_PERIOD, DP_KEY_COMMA, DP_KEY_SLASH, DP_KEY_EXTRA_LT_GT,

	DP_KEY_PRINTSCREEN, DP_KEY_PAUSE,
	DP_KEY_INSERT, DP_KEY_HOME, DP_KEY_PAGEUP, DP_KEY_DELETE, DP_KEY_END, DP_KEY_PAGEDOWN,
	DP_KEY_LEFT, DP_KEY_UP, DP_KEY_DOWN, DP_KEY_RIGHT,

	DP_KEY_KP1, DP_KEY_KP2, DP_KEY_KP3, DP_KEY_KP4, DP_KEY_KP5, DP_KEY_KP6, DP_KEY_KP7, DP_KEY_KP8, DP_KEY_KP9, DP_KEY_KP0,
	DP_KEY_KPDIVIDE, DP_KEY_KPMULTIPLY, DP_KEY_KPMINUS, DP_KEY_KPPLUS, DP_KEY_KPENTER, DP_KEY_KPPERIOD,

	DP_KEY_LAST
};

enum dp_key_commands {
	DP_KBD_CMD_NONE,
	DP_KBD_CMD_SETLEDS,
	DP_KBD_CMD_SETTYPERATE,
	DP_KBD_CMD_SETOUTPORT
};

#define DP_KEYBUFSIZE 32

struct dp_keyboard {
	u8 buffer[DP_KEYBUFSIZE];
	u32 used;
	u32 pos;
	struct {
		enum dp_key key;
		u32 wait;
		u32 pause, rate;
	} repeat;
	enum dp_key_commands command;
	u8 p60data;
	enum dp_bool p60changed;
	enum dp_bool active;
	enum dp_bool scanning;
	enum dp_bool scheduled;
	u8 port_61_data;

	char _marshal_sep[0];

	struct dp_logging *logging;
	struct dp_cpu *cpu;
	struct dp_timetrack *timetrack;
	struct dp_hwtimer *hwtimer;
	struct dp_pic *pic;
};

void dp_keyboard_init(struct dp_keyboard *keyboard, struct dp_logging *logging, struct dp_marshal *marshal,
		      struct dp_cpu *cpu, struct dp_int_callback *int_callback, struct dp_io *io,
		      struct dp_timetrack *timetrack, struct dp_pic *pic, struct dp_hwtimer *hwtimer);
void dp_keyboard_marshal(struct dp_keyboard *keyboard, struct dp_marshal *marshal);
void dp_keyboard_unmarshal(struct dp_keyboard *keyboard, struct dp_marshal *marshal);
void dp_keyboard_add_key(struct dp_keyboard *keyboard, enum dp_key keytype, enum dp_bool pressed);

#endif
