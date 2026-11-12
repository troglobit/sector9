/* SPDX-License-Identifier: ISC */
#ifndef GAME_H
#define GAME_H

#include <SDL.h>

#define SCREEN_W   320
#define SCREEN_H   240
#define TILE       20
#define MAP_W      (SCREEN_W / TILE)
#define MAP_H      400
#define ARENA_ROWS 8
#define MAX_SPAWN  512
#define SECTORS    9

enum tile   { T_FLOOR, T_WALL, T_ROOF, T_VOID };
enum etype  { E_TURRET, E_CRAWLER, E_DRONE, E_DART, E_POD, E_CORE, E_ROVER, E_GUNSHIP, E_HEAVY };
enum sprite {
	SPR_SHIP, SPR_TANK, SPR_TURRET, SPR_GUN, SPR_POD, SPR_CRAWLER, SPR_DRONE, SPR_DART, SPR_CORE, SPR_ROVER,
	SPR_GUNSHIP, SPR_HEAVY, SPR_FUSED, SPR_BUDDY, SPR_CAPSULE, SPR_ORB, SPR_MAX
};
enum sfx    { SFX_SHOT, SFX_HIT, SFX_BOOM, SFX_MORPH, SFX_DENY, SFX_PICKUP, SFX_HURT, SFX_MAX };
enum draw   { DRAW_NORMAL, DRAW_FLASH };
enum plate  { PLATE_FLOOR, PLATE_SEAM, PLATE_VENT, PLATE_WALL, PLATE_MAX };
enum theme  { THEME_STEEL, THEME_EMBER, THEME_MAX };

struct input {
	int   left, right, up, down, fire, morph, start;
	float mx, my;		/* mouse motion in game pixels */
};

/* Ground enemies appear at x,y.  Air enemies use y as the scroll trigger
 * and enter from the top of the screen after delay frames. */
struct spawn {
	int   type;
	float x, y;
	int   delay;
};

struct level {
	unsigned char tile[MAP_H][MAP_W];
	int           rows;
	struct spawn  spawn[MAX_SPAWN];
	int           nspawn;
};

/* Sectors from SECTORS on are laps of the open space beyond the last one. */
void level_generate(struct level *lv, int sector);

void gfx_init(SDL_Renderer *r);
/* Window pixels per game pixel. */
float gfx_scale(void);
/* Coordinates are game pixels, drawn at the resolution of the window. */
void gfx_sprite(int id, float x, float y, double angle, float scale, int mode);
void gfx_tinted(int id, float x, float y, float scale, SDL_Color c);
void gfx_disc(float x, float y, float r, SDL_Color c);
/* Soft light added on top of what is there. */
void gfx_glow(float x, float y, float r, SDL_Color c);
void gfx_plate(int id, int theme, float x, float y, SDL_Color tint);
void gfx_rect(float x, float y, float w, float h, SDL_Color c);
void gfx_logo(float y, int frame);
/* x < 0 centers the text */
void gfx_text(float x, float y, int scale, SDL_Color c, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));

void sfx_init(void);
void sfx_play(int id);
void sfx_music(const char *name);

void game_init(int start_row, int start_sector, int hiscore);
int  game_hiscore(void);
void game_update(const struct input *in);
void game_draw(void);
void game_autopilot(struct input *in);

#endif
