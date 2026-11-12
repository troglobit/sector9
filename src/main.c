/* SPDX-License-Identifier: ISC */
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define STEP (1.0 / 60)

static SDL_GameController *pad;
static int mouse_speed = 6;	/* half game pixels per count */

/* The pointer is captured from the first click, and let go while paused. */
static void grab_mouse(int on)
{
	SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
	SDL_GetRelativeMouseState(NULL, NULL);
}

/* 0 is what an unset setting reads as */
static void set_mouse_speed(int speed)
{
	if (speed)
		mouse_speed = SDL_clamp(speed, 1, 20);
}

static void read_input(struct input *in)
{
	const Uint8 *k = SDL_GetKeyboardState(NULL);

	in->left  = k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A];
	in->right = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D];
	in->up    = k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W];
	in->down  = k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S];
	in->fire  = k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL];
	in->morph = k[SDL_SCANCODE_X] || k[SDL_SCANCODE_SPACE];
	in->start = k[SDL_SCANCODE_RETURN];
	in->mx = in->my = 0;

	if (SDL_GetRelativeMouseMode()) {
		int x, y;
		Uint32 b = SDL_GetRelativeMouseState(&x, &y);

		in->mx = x * mouse_speed * 0.5f / gfx_scale();
		in->my = y * mouse_speed * 0.5f / gfx_scale();
		in->fire  |= !!(b & SDL_BUTTON_LMASK);
		in->morph |= !!(b & SDL_BUTTON_RMASK);
	}

	if (pad) {
		int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
		int ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);

		in->left  |= ax < -12000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
		in->right |= ax >  12000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
		in->up    |= ay < -12000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
		in->down  |= ay >  12000 || SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
		in->fire  |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A);
		in->morph |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B);
		in->start |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START);
	}
}

static void screenshot(SDL_Renderer *ren, const char *file)
{
	SDL_Surface *s;
	int w, h;

	SDL_GetRendererOutputSize(ren, &w, &h);
	s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
	if (!s)
		return;
	if (!SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGBA32, s->pixels, s->pitch))
		SDL_SaveBMP(s, file);
	SDL_FreeSurface(s);
}

static int usage(int rc)
{
	fprintf(rc ? stderr : stdout,
		"Usage: sector9 [-fh]\n"
		"  -f, --fullscreen  Start in fullscreen\n"
		"  -h, --help        Show this help text\n");
	return rc;
}

/* Returns the number stored under name, or stores value if it is not zero. */
static int stored(const char *name, int value)
{
	char path[512], *dir = SDL_GetPrefPath("", "sector9");
	FILE *fp;

	if (!dir)
		return 0;
	snprintf(path, sizeof(path), "%s%s", dir, name);
	SDL_free(dir);

	fp = fopen(path, value ? "w" : "r");
	if (!fp)
		return 0;
	if (value)
		fprintf(fp, "%d\n", value);
	else if (fscanf(fp, "%d", &value) != 1)
		value = 0;
	fclose(fp);

	return value;
}

int main(int argc, char *argv[])
{
	/* the long-only options are test hooks: scripted player, frame limit, screenshot, start row and sector */
	enum { OPT_DEMO = 256, OPT_FRAMES, OPT_SHOT, OPT_ROW, OPT_SECTOR };
	static const struct option opts[] = {
		{ "fullscreen", no_argument,       NULL, 'f' },
		{ "help",       no_argument,       NULL, 'h' },
		{ "demo",       no_argument,       NULL, OPT_DEMO },
		{ "frames",     required_argument, NULL, OPT_FRAMES },
		{ "shot",       required_argument, NULL, OPT_SHOT },
		{ "row",        required_argument, NULL, OPT_ROW },
		{ "sector",     required_argument, NULL, OPT_SECTOR },
		{ 0 }
	};
	int fullscreen = 0, demo = 0, frames = 0, row = 0, sector = 1, paused = 0, mouse = 0, notice = 0, hi = 0, run = 1;
	int c;
	const char *shot = NULL;
	SDL_Window *win;
	SDL_Renderer *ren;
	Uint64 prev;
	double acc = 0;

	while ((c = getopt_long(argc, argv, "fh", opts, NULL)) != -1) {
		switch (c) {
		case 'f':
			fullscreen = 1;
			break;
		case 'h':
			return usage(0);
		case OPT_DEMO:
			demo = 1;
			break;
		case OPT_FRAMES:
			frames = atoi(optarg);
			break;
		case OPT_SHOT:
			shot = optarg;
			break;
		case OPT_ROW:
			row = atoi(optarg);
			break;
		case OPT_SECTOR:
			sector = atoi(optarg);
			break;
		default:
			return usage(1);
		}
	}
	if (optind < argc)
		return usage(1);

	if (SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);

	win = SDL_CreateWindow("Sector Nine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			       SCREEN_W * 3, SCREEN_H * 3,
			       SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
	if (!win) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return 1;
	}

	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren)
		ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
	if (!ren) {
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		return 1;
	}

	SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
	SDL_ShowCursor(SDL_DISABLE);

	gfx_init(ren);
	sfx_init();
	if (!demo) {
		hi = stored("hiscore", 0);
		set_mouse_speed(stored("mouse", 0));
	}
	game_init(row, sector - 1, hi);

	prev = SDL_GetPerformanceCounter();
	while (run) {
		struct input in;
		SDL_Event ev;
		Uint64 now;
		int steps = 1;

		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT)
				run = 0;
			if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad)
				pad = SDL_GameControllerOpen(ev.cdevice.which);
			if (ev.type == SDL_MOUSEBUTTONDOWN && !mouse && !demo) {
				mouse = 1;
				grab_mouse(!paused);
			}
			if (ev.type != SDL_KEYDOWN || ev.key.repeat)
				continue;

			switch (ev.key.keysym.sym) {
			case SDLK_ESCAPE:
				if (game_escape())
					run = 0;
				paused = 0;
				grab_mouse(mouse);
				break;
			case SDLK_p:
				paused = !paused;
				grab_mouse(mouse && !paused);
				break;
			case SDLK_MINUS:
			case SDLK_KP_MINUS:
				set_mouse_speed(SDL_max(mouse_speed - 1, 1));
				notice = 90;
				break;
			case SDLK_EQUALS:
			case SDLK_PLUS:
			case SDLK_KP_PLUS:
				set_mouse_speed(mouse_speed + 1);
				notice = 90;
				break;
			case SDLK_F11:
				fullscreen = !fullscreen;
				SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
				break;
			}
		}

		/* a frame limit means a test run: one step per loop, no clock */
		if (!frames) {
			now = SDL_GetPerformanceCounter();
			acc += (double)(now - prev) / SDL_GetPerformanceFrequency();
			prev = now;

			for (steps = 0; acc >= STEP; acc -= STEP)
				steps++;
			if (steps > 5) {
				steps = 5;
				acc = 0;
			}
		}

		for (int i = 0; i < steps && !paused; i++) {
			if (demo)
				game_autopilot(&in);
			else
				read_input(&in);
			game_update(&in);
		}

		if (!steps) {
			SDL_Delay(1);
			continue;
		}

		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);
		game_draw();
		if (paused)
			gfx_text(-1, 110, 3, (SDL_Color){ 255, 255, 255, 255 }, "PAUSED");
		if (notice) {
			notice--;
			gfx_text(-1, 14, 1, (SDL_Color){ 255, 255, 255, 255 }, "MOUSE SPEED %d", mouse_speed);
		}
		if (frames && --frames == 0) {
			if (shot)
				screenshot(ren, shot);
			run = 0;
		}

		SDL_RenderPresent(ren);
	}

	if (!demo) {
		stored("hiscore", game_hiscore());
		stored("mouse", mouse_speed);
	}

	SDL_Quit();
	return 0;
}
