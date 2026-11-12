#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define STEP (1.0 / 60)

static SDL_GameController *pad;

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
		"  -f  Start in fullscreen\n"
		"  -h  Show this help text\n");
	return rc;
}

int main(int argc, char *argv[])
{
	int fullscreen = 0, demo = 0, frames = 0, row = 0, paused = 0, run = 1;
	const char *shot = NULL;
	SDL_Window *win;
	SDL_Renderer *ren;
	Uint64 prev;
	double acc = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-f"))
			fullscreen = 1;
		else if (!strcmp(argv[i], "-h"))
			return usage(0);
		/* test hooks: scripted player, frame limit, screenshot, start row */
		else if (!strcmp(argv[i], "--demo"))
			demo = 1;
		else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
			frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
			shot = argv[++i];
		else if (!strcmp(argv[i], "--row") && i + 1 < argc)
			row = atoi(argv[++i]);
		else
			return usage(1);
	}

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
	game_init(row);

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
			if (ev.type != SDL_KEYDOWN || ev.key.repeat)
				continue;

			switch (ev.key.keysym.sym) {
			case SDLK_ESCAPE:
				run = 0;
				break;
			case SDLK_p:
				paused = !paused;
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
		if (frames && --frames == 0) {
			if (shot)
				screenshot(ren, shot);
			run = 0;
		}

		SDL_RenderPresent(ren);
	}

	SDL_Quit();
	return 0;
}
