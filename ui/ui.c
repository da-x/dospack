#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_UI
#define DP_LOGGING           (ui->logging)

#if DP_PLATFORM_DESKTOP
#include <GL/gl.h>
#endif

#include <stdlib.h>
#include <memory.h>
#include <string.h>

#include <core/logging.h>
#include <core/events.h>
#include <core/platform.h>

#include "ui.h"

void dp_ui_reset(struct dp_ui *ui)
{
	int vp_height = 0;
	int vp_width = 0;
	int i;

	ui->platform->ops->get_viewport_size(ui->platform->ptr, &vp_width, &vp_height);

	ui->height = vp_height;
	ui->width = vp_width;
	ui->rotate = (vp_height > vp_width);

	glViewport(0, 0, vp_width, vp_height);

	glClearColor(0, 0, 0, 0);

	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();

	if (ui->rotate) {
		glOrtho( 0.0f, 1.0f, ((float)vp_height)/vp_width, 0.0f, 0.0f, 1.0f );
	} else {
		glOrtho( 0.0f, (vp_width/(float)vp_height), 1.0f, 0.0f, 0.0f, 1.0f );
	}

	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DITHER);
	glDisable(GL_MULTISAMPLE);

	glEnable(GL_TEXTURE_2D);

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	for (i = 0; i < ui->num_sprites; i++) {
		ui->sprites[i]->ops->init(ui->sprites[i]->data);
	}

	glFinish();
}

void dp_ui_render(struct dp_ui *ui)
{
	int i;

	glClear(GL_COLOR_BUFFER_BIT);

	glPushMatrix();

	if (ui->rotate) {
		glTranslatef(1, 0, 0);
		glRotatef(90, 0, 0, 1);
	}

	for (i = 0; i < ui->num_sprites; i++) {
		ui->sprites[i]->ops->draw(ui->sprites[i]->data);
	}

	glPopMatrix();

	glFinish();
}

void dp_ui_init(struct dp_ui *ui, struct dp_logging *logging, struct dp_platform_context *platform)
{
	memset(ui, 0, sizeof(*ui));

	ui->logging = logging;
	ui->platform = platform;
}

enum dp_bool dp_ui_hit(struct dp_ui *ui, int x, int y, struct dp_ui_hit_result *result)
{
	float f_x, f_y, r;
	int i;

	if (ui->rotate) {
		int t = x;
		x = y;
		y = t;

		y = ui->width - y;
		r = ui->width;
	} else {
		r = ui->height;
	}

	f_x = x / r;
	f_y = y / r;

	for (i = 0; i < ui->num_sprites; i++) {
		struct dp_ui_sprite *sprite = ui->sprites[i];

		if (f_x >= sprite->pos.x  &&  f_x < sprite->pos.x + sprite->size.x  &&
		    f_y >= sprite->pos.y  &&  f_y < sprite->pos.y + sprite->size.y) {
			result->sprite = sprite;
			result->rel_x = (f_x - sprite->pos.x) / sprite->size.x;
			result->rel_y = (f_y - sprite->pos.y) / sprite->size.y;
			return DP_TRUE;
		}
	}

	return DP_FALSE;
}

void dp_ui_add_sprite(struct dp_ui *ui, struct dp_ui_sprite *sprite)
{
	ui->sprites[ui->num_sprites++] = sprite;
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
