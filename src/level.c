#include <stdlib.h>
#include <string.h>

#include "game.h"

enum { SEG_OPEN, SEG_WALL, SEG_ROOF, SEG_MAZE, SEG_MAX };

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

static void ground(struct level *lv, int type, int row, int n, int count)
{
	while (count-- > 0) {
		int c = 1 + rnd(MAP_W - 2);
		int r = row + rnd(n);

		if (lv->tile[r][c] == T_WALL)
			continue;
		/* towers stand on a block, out of the tank's reach */
		if (type == E_TURRET)
			lv->tile[r][c] = T_WALL;
		add(lv, type, c * TILE + TILE / 2, r * TILE + TILE / 2, 0);
	}
}

static void wave(struct level *lv, int type, int row, int count)
{
	int x = 40 + rnd(SCREEN_W - 80);

	for (int i = 0; i < count; i++) {
		if (type == E_DART)
			x = 24 + rnd(SCREEN_W - 48);
		add(lv, type, x, row * TILE, 1 + i * 25);
	}
}

static int by_scroll(const void *a, const void *b)
{
	const struct spawn *sa = a, *sb = b;

	return (sa->y < sb->y) - (sa->y > sb->y);
}

void level_generate(struct level *lv, int sector)
{
	int row, last = -1;

	memset(lv, 0, sizeof(*lv));
	lv->rows = SDL_min(MAP_H, 170 + sector * 40);
	row = lv->rows - 14;
	seed = 0x9e3779b9u * (sector + 1);

	while (row > ARENA_ROWS + 14) {
		int kind, n;

		do {
			kind = rnd(SEG_MAX);
		} while (kind == last);
		last = kind;

		switch (kind) {
		case SEG_OPEN:
			n = 6 + rnd(5);
			row -= n;
			for (int i = 2 + rnd(3); i > 0; i--) {
				int c = 1 + rnd(MAP_W - 3);
				int r = row + 1 + rnd(n - 3);

				lv->tile[r][c] = lv->tile[r][c + 1] = T_WALL;
				lv->tile[r + 1][c] = lv->tile[r + 1][c + 1] = T_WALL;
			}
			ground(lv, E_TURRET, row, n, 1 + rnd(2) + sector / 2);
			ground(lv, E_CRAWLER, row, n, rnd(2));
			ground(lv, E_ROVER, row, n, 1 + rnd(2));
			wave(lv, E_DRONE, row + n, 3 + rnd(2) + sector / 2);
			break;

		case SEG_WALL:
			n = 2 + rnd(2);
			row -= n;
			fill(lv, row, n, T_WALL);
			for (int i = 1 + rnd(2); i > 0; i--)
				add(lv, E_TURRET, (2 + rnd(MAP_W - 4)) * TILE + TILE / 2,
				    (row + rnd(n)) * TILE + TILE / 2, 0);
			wave(lv, E_DART, row + n, 3 + sector);
			break;

		case SEG_ROOF:
			n = 5 + rnd(4);
			row -= n;
			fill(lv, row, n, T_ROOF);
			for (int i = 3 + rnd(3); i > 0; i--)
				lv->tile[row + rnd(n)][rnd(MAP_W)] = T_WALL;
			ground(lv, E_ROVER, row, n, 2 + rnd(2) + sector / 2);
			ground(lv, E_CRAWLER, row, n, 1 + rnd(2));
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
			ground(lv, E_TURRET, row, n, 1 + sector / 2);
			ground(lv, E_ROVER, row, n, 1 + rnd(2));
			wave(lv, rnd(2) ? E_DRONE : E_DART, row + n, 3);
			break;
		}

		/* clear strip, room to morph between segments */
		row -= 4;
	}

	for (int r = 5; r <= 6; r++) {
		lv->tile[r][3] = lv->tile[r][4] = T_WALL;
		lv->tile[r][MAP_W - 5] = lv->tile[r][MAP_W - 4] = T_WALL;
	}
	add(lv, E_POD, 4 * TILE, 6 * TILE, 0);
	add(lv, E_POD, SCREEN_W - 4 * TILE, 6 * TILE, 0);
	add(lv, E_CORE, SCREEN_W / 2, 4 * TILE + 8, 0);

	qsort(lv->spawn, lv->nspawn, sizeof(lv->spawn[0]), by_scroll);
}
