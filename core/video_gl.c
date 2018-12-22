#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_VIDEO
#define DP_LOGGING           (gl->video->logging)

#if DP_PLATFORM_DESKTOP
#include <GL/gl.h>
#endif

#include <stdlib.h>
#include <memory.h>
#include <string.h>

#include "video_gl.h"
#include "logging.h"

struct dp_video_gl {
	struct dp_video *video;
	struct dp_ui_sprite sprite;

	GLuint texture;
	GLushort *texture_memory;
	GLushort color_map[0x100];
	u8 *video_memory;

	GLfloat vertices[8];
	GLfloat texcoords[8];

	int memx;
	int memy;
	int colors;

	int textx;
	int texty;

	int height;
	int width;

	int rotate;
};

static void dp_video_ui_sprite_free(void *data)
{
	struct dp_video_gl *gl = data;

	if (gl->texture_memory) {
		glDeleteTextures(1, &gl->texture);
		free(gl->texture_memory);
		gl->texture_memory = NULL;
	}
}

static void dp_video_ui_sprite_init(void *data)
{
	struct dp_video_gl *gl = data;
	int vp_height = 0;
	int vp_width = 0;

	gl->video->platform->ops->get_viewport_size(gl->video->platform->ptr, &vp_width, &vp_height);

	gl->vertices[0] = 0;
	gl->vertices[1] = 0;
	gl->vertices[2] = 1;
	gl->vertices[3] = 0;
	gl->vertices[4] = 0;
	gl->vertices[5] = 1;
	gl->vertices[6] = 1;
	gl->vertices[7] = 1;


 	gl->texcoords[0] = 0;
	gl->texcoords[1] = 0;
	gl->texcoords[2] = 0.8;
	gl->texcoords[3] = 0;
	gl->texcoords[4] = 0;
	gl->texcoords[5] = 0.8;
	gl->texcoords[6] = 0.8;
	gl->texcoords[7] = 0.8;

	gl->height = vp_height;
	gl->width = vp_width;
	gl->rotate = (vp_height > vp_width);

	if (!gl->memx)
		return;

	gl->texty = gl->memy;
	gl->textx = gl->memx;

	if (gl->textx <= 256)
		gl->textx = 256;
	else if( gl->textx <= 512)
		gl->textx = 512;
	else
		gl->textx = 1024;

	if (gl->texty <= 256 )
		gl->texty = 256;
	else if( gl->texty <= 512)
		gl->texty = 512;
	else
		gl->texty = 1024;

	dp_video_ui_sprite_free(gl);

	gl->texture_memory = malloc(gl->textx * gl->texty * sizeof(GLushort));

	gl->texcoords[2] = gl->memx/(float)gl->textx;
	gl->texcoords[6] = gl->texcoords[2];
	gl->texcoords[5] = gl->memy/(float)gl->texty;
	gl->texcoords[7] = gl->texcoords[5];

	gl->vertices[2] = gl->memx/(float)gl->memy;
	gl->vertices[6] = gl->vertices[2];
	gl->sprite.size.x = gl->vertices[2];

	glGenTextures(1, &gl->texture);
	glBindTexture(GL_TEXTURE_2D, gl->texture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	memset(gl->texture_memory, 77, gl->textx * gl->texty * sizeof(GLushort));

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, gl->textx, gl->texty, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, gl->texture_memory);
}

static void dp_video_ui_sprite_draw(void *data)
{
	struct dp_video_gl *gl = data;

	if (gl->memx != 0) {
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);

		glBindTexture(GL_TEXTURE_2D, gl->texture);
		glVertexPointer(2, GL_FLOAT, 0, gl->vertices);
		glTexCoordPointer(2, GL_FLOAT, 0, gl->texcoords);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
	}
}

enum dp_bool dp_gl_set_video_mode(void *ptr, int width, int height, int colors)
{
	struct dp_video_gl *gl = ptr;

	gl->memx = width;
	gl->memy = height;
	gl->colors = colors;

	if (gl->video_memory)
		free(gl->video_memory);
	gl->video_memory = malloc(width * height * colors / 8);

	dp_video_ui_sprite_init(gl);

	return DP_TRUE;
}

void *dp_gl_get_video_surface(void *ptr)
{
	struct dp_video_gl *gl = ptr;

	return gl->video_memory;
}

void dp_gl_put_video_surface(void *ptr, int upd_x, int upd_y, int upd_w, int upd_h)
{
	struct dp_video_gl *gl = ptr;
	int x, y;
	u8 *video_mem_ptr;
	GLushort *texture_mem_ptr;

	video_mem_ptr = gl->video_memory;
	texture_mem_ptr = gl->texture_memory;

	if (!gl->texture_memory)
		return;

	for (y = 0; y < gl->memy; y++) {
		for (x = 0; x < gl->memx; x++) {
			*texture_mem_ptr = gl->color_map[*video_mem_ptr];

			video_mem_ptr++;
			texture_mem_ptr++;
		}
	}

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gl->memx, gl->memy, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, gl->texture_memory);
}

void dp_gl_set_colors(void *ptr, void *array, int index, int colors)
{
	struct dp_video_gl *gl = ptr;
	int i;

	for (i = 0; i < colors; i++) {
		unsigned int r = i % 32, g = 0, b = 0;
		r = ((unsigned char *)array)[i*3] >> 1;
		g = ((unsigned char *)array)[i*3+1] >> 1;
		b = ((unsigned char *)array)[i*3+2] >> 1;

		gl->color_map[index + i] = ((b << 0) | (g << 6) | (r << 11));
	}
}

static void gl_exit(void *ptr)
{
}

static struct dp_video_ops ops = {
	.set_video_mode = dp_gl_set_video_mode,
	.get_video_surface = dp_gl_get_video_surface,
	.put_video_surface = dp_gl_put_video_surface,
	.set_colors = dp_gl_set_colors,
	.exit = gl_exit,
};

static struct dp_ui_sprite_ops dp_ui_sprite_ops_vtable = {
	.init = dp_video_ui_sprite_init,
	.free = dp_video_ui_sprite_free,
	.draw = dp_video_ui_sprite_draw,
};

enum dp_bool dp_video_gl_init(struct dp_video *video)
{
	struct dp_video_gl *gl = (struct dp_video_gl *)video->engine_data;

	if (sizeof(video->engine_data) < sizeof(*gl))
		return DP_FALSE;

	gl->video = video;
	video->ops = &ops;
	gl->sprite.class = DP_UI_SPRITE_CLASS_VIDEO;
	gl->sprite.pos.x = 0;
	gl->sprite.pos.y = 0;
	gl->sprite.size.x = 1.0;
	gl->sprite.size.y = 1.0;
	gl->sprite.data = gl;
	gl->sprite.ops = &dp_ui_sprite_ops_vtable;

	dp_ui_add_sprite(video->ui, &gl->sprite);

	return DP_TRUE;
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
