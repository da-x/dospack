#ifndef _DOSPACK_UI_H__
#define _DOSPACK_UI_H__

#include <core/common.h>

struct dp_ui_sprite_ops {
	void (*init)(void *data);
	void (*free)(void *data);
	void (*draw)(void *data);
};

enum dp_ui_sprite_class {
	DP_UI_SPRITE_CLASS_VIDEO,
};

struct dp_ui_sprite {
	enum dp_ui_sprite_class class;

	struct {
		float x, y;
	} pos;
	struct {
		float x, y;
	} size;

	void *data;
	struct dp_ui_sprite_ops *ops;
};

#define DP_UI_MAX_SPRITES  0x10

struct dp_ui {
	struct dp_platform_context *platform;
	struct dp_logging *logging;

	int height, width, rotate;

	struct dp_ui_sprite *sprites[DP_UI_MAX_SPRITES];
	int num_sprites;
};

void dp_ui_reset(struct dp_ui *ui);
void dp_ui_render(struct dp_ui *ui);
void dp_ui_init(struct dp_ui *ui, struct dp_logging *logging, struct dp_platform_context *platform);

struct dp_ui_hit_result {
	struct dp_ui_sprite *sprite;
	float rel_x, rel_y;
};

enum dp_bool dp_ui_hit(struct dp_ui *ui, int x, int y, struct dp_ui_hit_result *result);

void dp_ui_add_sprite(struct dp_ui *ui, struct dp_ui_sprite *sprite);

#endif
