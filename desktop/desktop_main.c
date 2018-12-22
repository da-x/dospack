#include <pthread.h>
#include <unistd.h>

#include <GL/gl.h>
#include <SDL/SDL.h>

#include <core/dospack.h>

struct desktop_platform {
	struct program_params {
		int argc;
		char **argv;
	} program_params;

	int viewport_x;
	int viewport_y;
	int frame_ready;
	struct dospack *dospack;
};

static void desktop_frame_ready(void *ptr)
{
	struct desktop_platform *dp = ptr;

	dp->frame_ready = 1;
}

static int desktop_get_events(void *ptr, struct dp_user_event *events, int max_events)
{
	SDL_Event event;
	int ret, num_events = 0;

	while (max_events > 0) {
		ret = SDL_PollEvent(&event);
		if (!ret)
			break;

		switch (event.type) {
		case SDL_KEYUP:
		case SDL_KEYDOWN:
		case SDL_QUIT:
		case SDL_MOUSEMOTION:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			break;

		case SDL_ACTIVEEVENT:
			continue;
		default:
			// DP_INF("unprocessed SDL event %d", event.type);
			continue;
		}

		switch (event.type) {
		case SDL_QUIT:
			events->type = DP_USER_EVENT_TYPE_QUIT;
			break;

		case SDL_MOUSEMOTION:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			events->type = DP_USER_EVENT_TYPE_POINTER;
			events->pointer.x = event.motion.x;
			events->pointer.y = event.motion.y;
			events->pointer.index = 0;

			switch (event.type) {
			case SDL_MOUSEMOTION: events->pointer.type = DP_POINTER_EVENT_TYPE_MOVE; break;
			case SDL_MOUSEBUTTONDOWN: events->pointer.type = DP_POINTER_EVENT_TYPE_DOWN; break;
			case SDL_MOUSEBUTTONUP: events->pointer.type = DP_POINTER_EVENT_TYPE_UP; break;
			}

			break;

		case SDL_KEYUP:
		case SDL_KEYDOWN:
			events->type = DP_USER_EVENT_TYPE_KEYBOARD;
			switch (event.key.type) {
			case SDL_KEYUP:
				events->keyboard.is_up = 1;
				break;
			case SDL_KEYDOWN:
				events->keyboard.is_up = 0;
				if (((event.key.keysym.mod & (KMOD_RCTRL | KMOD_MODE)) == (KMOD_RCTRL | KMOD_MODE) ||
				     (event.key.keysym.mod & (KMOD_RCTRL | KMOD_RALT)) == (KMOD_RCTRL | KMOD_RALT))
				    ) {
					if (event.key.keysym.sym == SDLK_ESCAPE) {
						events->type = DP_USER_EVENT_TYPE_QUIT;
						goto next;
					} else if (event.key.keysym.sym == SDLK_PAUSE) {
						events->type = DP_USER_EVENT_TYPE_QUIT; // PAUSE
						goto next;
					}
				}
				if (((event.key.keysym.mod & KMOD_RCTRL) == KMOD_RCTRL) &&
				    event.key.keysym.sym == SDLK_BACKSPACE) {
					events->type = DP_USER_EVENT_TYPE_REWIND;
					goto next;
				}
				break;
			}

			switch (event.key.keysym.sym) {
			case SDLK_SPACE: events->keyboard.key = DP_KEY_SPACE; break;
			case SDLK_LALT: events->keyboard.key = DP_KEY_LEFTALT; break;
			case SDLK_RALT: events->keyboard.key = DP_KEY_RIGHTALT; break;
			case SDLK_LCTRL: events->keyboard.key = DP_KEY_LEFTCTRL; break;
			case SDLK_RCTRL: events->keyboard.key = DP_KEY_RIGHTCTRL; break;
			case SDLK_RETURN: events->keyboard.key = DP_KEY_ENTER; break;
			case SDLK_KP_ENTER: events->keyboard.key = DP_KEY_KPENTER; break;
			case SDLK_UP: events->keyboard.key = DP_KEY_UP; break;
			case SDLK_DOWN: events->keyboard.key = DP_KEY_DOWN; break;
			case SDLK_LEFT: events->keyboard.key = DP_KEY_LEFT; break;
			case SDLK_RIGHT: events->keyboard.key = DP_KEY_RIGHT; break;
			default:
				// DP_INF("unprocessed SDL key %d, 0x%x", event.key.keysym.sym, event.key.keysym.mod);
				continue;
			}
		}

	next:
		num_events++;
		events++;
		max_events--;
	}

	return num_events;
}

void desktop_get_viewport_size(void *ptr, int *x, int *y)
{
	struct desktop_platform *dp = ptr;

	*x = dp->viewport_x;
	*y = dp->viewport_y;
}

static struct dp_platform_ops ops = {
	.frame_ready = desktop_frame_ready,
	.get_events = desktop_get_events,
	.get_viewport_size = desktop_get_viewport_size,
};

void desktop_main_loop(struct desktop_platform *dp)
{
	struct dospack *dospack;
	int ret;

	dospack = dospack_main_entry(dp->program_params.argc, dp->program_params.argv, &ops, dp);
	dp->dospack = dospack;

	while (1) {
		ret = dospack_loop(dospack);

		if (ret == DOSPACK_LOOP_RET_EXIT)
			break;
		if (ret == DOSPACK_LOOP_RET_ERROR)
			break;

		if (dp->frame_ready) {
			dospack_draw(dospack);

			SDL_GL_SwapBuffers();
			dp->frame_ready = 0;
		}
	}

	dospack_exit(dospack);
}

int main(int argc, char *argv[])
{
	struct desktop_platform dp;

	int ret;
	// int x = 480, y = 800;
	int x = 800, y = 480;
	//int x = 320, y = 200;

	dp.program_params.argc = argc;
	dp.program_params.argv = argv;
	dp.viewport_x = x;
	dp.viewport_y = y;

	ret = SDL_Init(SDL_INIT_VIDEO);
	if (ret < 0)
		return -1;

	SDL_SetVideoMode(x, y, 16, SDL_OPENGL);
	desktop_main_loop(&dp);

	SDL_Quit();
	return 0;
}
