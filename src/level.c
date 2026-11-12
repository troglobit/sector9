/* SPDX-License-Identifier: ISC */
#include <stdlib.h>
#include <string.h>

#include "game.h"

enum { SEG_OPEN, SEG_WALL, SEG_ROOF, SEG_MAZE, SEG_MAX };

/* The laps beyond the last sector go through them again. */
static const struct boss bosses[SECTORS] = {
	{ "WARDEN",   2, MOVE_SWAY,  FIRE_RING,   0, { 255, 255, 255, 255 } },
	{ "PINWHEEL", 2, MOVE_SWAY,  FIRE_SPIRAL, 0, { 150, 255, 200, 255 } },
	{ "TRIDENT",  3, MOVE_EIGHT, FIRE_FAN,    0, { 255, 215, 130, 255 } },
	{ "RAM",      2, MOVE_SWOOP, FIRE_RING,   0, { 255, 150, 150, 255 } },
	{ "BROOD",    3, MOVE_SWAY,  FIRE_RING,   1, { 200, 255, 120, 255 } },
	{ "CYCLONE",  4, MOVE_EIGHT, FIRE_SPIRAL, 0, { 140, 215, 255, 255 } },
	{ "LANCER",   3, MOVE_SWOOP, FIRE_FAN,    0, { 255, 170, 255, 255 } },
	{ "SWARM",    4, MOVE_SWAY,  FIRE_SPIRAL, 1, { 255, 255, 130, 255 } },
	{ "OVERSEER", 4, MOVE_EIGHT, FIRE_RING,   0, { 255, 255, 255, 255 } },
};

static unsigned seed;

static int rnd(int n)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;

	return seed % n;
}

static void add(struct level *lv, int type, float x, float y, int delay)
{
	if (lv->nspawn < MAX_SPAWN)
		lv->spawn[lv->nspawn++] = (struct spawn){ type, x, y, delay };
}

static void fill(struct level *lv, int row, int n, int t)
{
	memset(lv->tile[row], t, n * MAP_W);
}

/* Puts up to count ground enemies in the w by n cells at column c0, row. */
static void place(struct level *lv, int type, int c0, int w, int row, int n, int count)
{
	while (count-- > 0) {
		int c = c0 + rnd(w);
		int r = row + rnd(n);

		if (lv->tile[r][c] == T_WALL)
			continue;
		/* towers stand on a block, out of the tank's reach */
		if (type == E_TURRET)
			lv->tile[r][c] = T_WALL;
		add(lv, type, c * TILE + TILE / 2, r * TILE + TILE / 2, 0);
	}
}

static void ground(struct level *lv, int type, int row, int n, int count)
{
	place(lv, type, 1, MAP_W - 2, row, n, count);
}

static void heavies(struct level *lv, int row, int n, int sector)
{
	if (sector >= 4)
		ground(lv, E_HEAVY, row, n, rnd(2) + (sector >= 7));
}

static void wave(struct level *lv, int type, int row, int count)
{
	int x = 40 + rnd(SCREEN_W - 80);

	for (int i = 0; i < count; i++) {
		if (type != E_DRONE)
			x = 24 + rnd(SCREEN_W - 48);
		add(lv, type, x, row * TILE, 1 + i * 25);
	}
}

static int by_scroll(const void *a, const void *b)
{
	const struct spawn *sa = a, *sb = b;

	return (sa->y < sb->y) - (sa->y > sb->y);
}

static void block(struct level *lv, int row, int col)
{
	for (int r = row; r < row + 2; r++)
		lv->tile[r][col] = lv->tile[r][col + 1] = T_WALL;
}

/* A tower on its block. */
static void pod(struct level *lv, int row, int col, int armed)
{
	block(lv, row, col);
	if (armed)
		add(lv, E_POD, (col + 1) * TILE, (row + 1) * TILE, 0);
}

/* The blocks of a fortress from row down.  An armed one, at the top of the
 * map, also gets its towers and core. */
static void fortress(struct level *lv, int sector, int row, int armed)
{
	int pods = level_boss(sector)->pods;

	pod(lv, row + 5, 3, armed);
	pod(lv, row + 5, MAP_W - 5, armed);
	if (pods == 3)
		pod(lv, row + 9, MAP_W / 2 - 1, armed);
	if (pods == 4) {
		pod(lv, row + 9, 6, armed);
		pod(lv, row + 9, MAP_W - 8, armed);
	}
	if (armed)
		add(lv, E_CORE, SCREEN_W / 2, CORE_Y, 0);
}

/* Islands of deck in open space, no fortress at the end. */
static void open_space(struct level *lv, int lap)
{
	int row;

	lv->rows = 150;
	row = lv->rows - 16;
	memset(lv->tile, T_VOID, sizeof(lv->tile));

	while (row > 24) {
		int w = 4 + rnd(6), n = 3 + rnd(4), c = rnd(MAP_W - w + 1);

		row -= n;
		for (int r = row; r < row + n; r++)
			memset(&lv->tile[r][c], T_FLOOR, w);
		place(lv, E_TURRET, c, w, row, n, 1 + rnd(2));
		place(lv, E_ROVER, c, w, row, n, rnd(2) + (lap > 0));
		place(lv, E_HEAVY, c, w, row, n, rnd(2));

		wave(lv, E_DRONE, row + n, 3 + rnd(3));
		wave(lv, E_DART, row, 3 + SDL_min(lap, 5));
		if (rnd(2))
			wave(lv, E_GUNSHIP, row + n / 2, 1 + (lap > 1));

		row -= 3 + rnd(4);
	}
}

/* Segments of deck with a fortress at the end. */
static void station(struct level *lv, int sector)
{
	int row, last = -1;
	int lag = 0;
	int more = sector / 3, few = sector ? 2 : 1;

	lv->rows = 110 + sector * 12;
	row = lv->rows - 14;

	while (row > ARENA_ROWS + 14) {
		int kind, n;

		/* the first sectors leave out the harder parts */
		do {
			kind = rnd(SEG_MAX);
		} while (kind == last || (kind == SEG_MAZE && sector < 2));
		/* after a roof the tank needs time to get out and morph before aircraft arrive */
		lag = last == SEG_ROOF ? 5 : 0;
		last = kind;

		switch (kind) {
		case SEG_OPEN:
			n = 6 + rnd(5);
			row -= n;
			for (int i = 2 + rnd(3); i > 0; i--) {
				int c = 1 + rnd(MAP_W - 3);
				int r = row + 1 + rnd(n - 3);

				block(lv, r, c);
			}
			ground(lv, E_TURRET, row, n, 1 + rnd(few) + more);
			ground(lv, E_CRAWLER, row, n, rnd(few));
			ground(lv, E_ROVER, row, n, 1 + rnd(few));
			heavies(lv, row, n, sector);
			wave(lv, E_DRONE, row + n - lag, 3 + rnd(2) + more);
			if (sector >= 3 && rnd(2))
				wave(lv, E_GUNSHIP, row + SDL_min(n / 2, n - lag), 1 + (sector >= 6));
			break;

		case SEG_WALL:
			n = 2 + rnd(2);
			row -= n;
			fill(lv, row, n, T_WALL);
			for (int i = 1 + rnd(few); i > 0; i--)
				add(lv, E_TURRET, (2 + rnd(MAP_W - 4)) * TILE + TILE / 2,
				    (row + rnd(n)) * TILE + TILE / 2, 0);
			wave(lv, sector ? E_DART : E_DRONE, row + n - lag, 3 + sector / 2);
			break;

		case SEG_ROOF:
			n = 5 + rnd(4);
			row -= n;
			fill(lv, row, n, T_ROOF);
			for (int i = 3 + rnd(3); i > 0; i--)
				lv->tile[row + rnd(n)][rnd(MAP_W)] = T_WALL;
			ground(lv, E_ROVER, row, n, 1 + rnd(2) + more);
			ground(lv, E_CRAWLER, row, n, sector ? 1 + rnd(2) : 0);
			heavies(lv, row, n, sector);
			break;

		case SEG_MAZE:
			n = 10;
			row -= n;
			for (int k = 0; k < 3; k++) {
				int r = row + 1 + k * 3;
				int gap = 1 + rnd(MAP_W - 5);

				fill(lv, r, 1, T_WALL);
				memset(&lv->tile[r][gap], T_FLOOR, 3);
			}
			ground(lv, E_TURRET, row, n, 1 + more);
			ground(lv, E_ROVER, row, n, 1 + rnd(2));
			wave(lv, rnd(2) ? E_DRONE : E_DART, row + n - lag, 3);
			break;
		}

		/* clear strip, room to morph between segments */
		row -= 4;
	}

	fortress(lv, sector, 0, 1);
}

const struct boss *level_boss(int sector)
{
	return &bosses[sector % SECTORS];
}

void level_generate(struct level *lv, int sector)
{
	memset(lv, 0, sizeof(*lv));
	seed = 0x9e3779b9u * (sector + 1);

	if (sector >= SECTORS)
		open_space(lv, sector - SECTORS);
	else
		station(lv, sector);

	qsort(lv->spawn, lv->nspawn, sizeof(lv->spawn[0]), by_scroll);
}
