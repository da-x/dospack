#ifndef _DOSPACK_GAME_ENV_H__
#define _DOSPACK_GAME_ENV_H__

#include "common.h"
#include "logging.h"

struct dp_game_env {
	struct dp_logging *logging;
	struct dospack *dospack;
	struct dp_game *game_desc;
	void *game_data;
};

#endif
