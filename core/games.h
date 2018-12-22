#ifndef _DOSPACK_GAMES_H__
#define _DOSPACK_GAMES_H__

#include "common.h"
#include "files.h"

struct dp_game_env;
struct dp_user_event;

struct dp_game_ops {
	void (*init)(struct dp_game_env *game_env);
	void (*after_exe)(struct dp_game_env *game_env);
	int (*user_event)(struct dp_game_env *game_env, struct dp_user_event *event);
};

struct dp_game {
	char name[0x20]; /* length FIXME */
	char command_line[0x100]; /* length FIXME */

	struct dp_game_ops *ops;
	struct dp_fdirectory *vfs_root;
};

#endif
