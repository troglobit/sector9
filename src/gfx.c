#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "game.h"

#define SPR 16

static SDL_Renderer *ren;
static SDL_Texture  *tex[SPR_MAX];
static SDL_Texture  *plate[PLATE_MAX];

/* Placeholder art, all sprites face up.  See pal() for the colour keys. */
static const char *art[SPR_MAX][SPR] = {
	[SPR_SHIP] = {
		".......cc.......",
		".......cc.......",
		"......#ww#......",
		"......#ww#......",
		".....#wbbw#.....",
		".....#wbbw#.....",
		"....#wwbbww#....",
		"...#gwwwwwwg#...",
		"..#ggwwwwwwgg#..",
		".#gggwwggwwggg#.",
		"#ggg#wwggww#ggg#",
		"#gg#.#wggw#.#gg#",
		"#g#..#wggw#..#g#",
		"##...#oooo#...##",
		"......oyyo......",
		".......yy.......",
	},
	[SPR_TANK] = {
		"......#gg#......",
		"......#gg#......",
		".##...#gg#...##.",
		"#dd#..#gg#..#dd#",
		"#dd#GGGggGGG#dd#",
		"#dd#GGGggGGG#dd#",
		"#dd#GG#gg#GG#dd#",
		"#dd#G#gggg#G#dd#",
		"#dd#G#gggg#G#dd#",
		"#dd#GG####GG#dd#",
		"#dd#GGGGGGGG#dd#",
		"#dd#GGGGGGGG#dd#",
		"#dd#GGGGGGGG#dd#",
		"#dd#.######.#dd#",
		".##..........##.",
		"................",
	},
	[SPR_TURRET] = {
		"................",
		"....########....",
		"...#dddddddd#...",
		"..#ddrrrrrrdd#..",
		".#ddrr####rrdd#.",
		".#drr#oooo#rrd#.",
		".#dr#oyyyyo#rd#.",
		".#dr#oyyyyo#rd#.",
		".#dr#oyyyyo#rd#.",
		".#dr#oyyyyo#rd#.",
		".#drr#oooo#rrd#.",
		".#ddrr####rrdd#.",
		"..#ddrrrrrrdd#..",
		"...#dddddddd#...",
		"....########....",
		"................",
	},
	[SPR_CRAWLER] = {
		"................",
		"..#..........#..",
		"...#..####..#...",
		"....##pppp##....",
		"...#pppppppp#...",
		"#.#ppyppppypp#.#",
		".##pppppppppp##.",
		"..#pppppppppp#..",
		".##pppppppppp##.",
		"#.#pppppppppp#.#",
		"...#pppppppp#...",
		"....##pppp##....",
		"...#..####..#...",
		"..#..........#..",
		"................",
		"................",
	},
	[SPR_DRONE] = {
		"................",
		"................",
		".....######.....",
		"...##cccccc##...",
		"..#cwwccccccc#..",
		".#cccccccccccc#.",
		"#bbbbbbbbbbbbbb#",
		"#byybbyybbyybbb#",
		"#bbbbbbbbbbbbbb#",
		".#dddddddddddd#.",
		"..##dddddddd##..",
		"....########....",
		"................",
		"................",
		"................",
		"................",
	},
	[SPR_DART] = {
		"................",
		"..#..........#..",
		"..##........##..",
		"..#r#......#r#..",
		"..#rr######rr#..",
		"..#rrroooorrr#..",
		"...#rroyyorr#...",
		"...#rroyyorr#...",
		"....#rroorr#....",
		"....#rrrrrr#....",
		".....#rrrr#.....",
		".....#rrrr#.....",
		"......#rr#......",
		"......#rr#......",
		".......##.......",
		"................",
	},
	[SPR_CORE] = {
		"....########....",
		"..##pppppppp##..",
		".#pppp####pppp#.",
		".#pp##rrrr##pp#.",
		"#pp#rrrrrrrr#pp#",
		"#pp#rroooorr#pp#",
		"#p#rrooyyoorr#p#",
		"#p#rroywwyorr#p#",
		"#p#rroywwyorr#p#",
		"#p#rrooyyoorr#p#",
		"#pp#rroooorr#pp#",
		"#pp#rrrrrrrr#pp#",
		".#pp##rrrr##pp#.",
		".#pppp####pppp#.",
		"..##pppppppp##..",
		"....########....",
	},
	[SPR_ROVER] = {
		"................",
		"..##........##..",
		".#dd#.####.#dd#.",
		".#dd##oooo##dd#.",
		".#dd#oooooo#dd#.",
		"..##oorrrroo##..",
		"...#oorrrroo#...",
		"...#oor##roo#...",
		"...#oor##roo#...",
		"...#oorrrroo#...",
		"..##oorrrroo##..",
		".#dd#oooooo#dd#.",
		".#dd##oooo##dd#.",
		".#dd#.####.#dd#.",
		"..##........##..",
		"................",
	},
};

static SDL_Color pal(char ch)
{
	switch (ch) {
	case 'w': return (SDL_Color){ 236, 240, 244, 255 };
	case 'g': return (SDL_Color){ 150, 158, 172, 255 };
	case 'd': return (SDL_Color){  78,  84,  98, 255 };
	case 'b': return (SDL_Color){  52,  96, 208, 255 };
	case 'c': return (SDL_Color){  96, 220, 240, 255 };
	case 'r': return (SDL_Color){ 208,  52,  60, 255 };
	case 'o': return (SDL_Color){ 240, 140,  40, 255 };
	case 'y': return (SDL_Color){ 250, 226,  90, 255 };
	case 'G': return (SDL_Color){  70, 160,  84, 255 };
	case 'p': return (SDL_Color){ 150,  84, 196, 255 };
	default:  return (SDL_Color){  16,  18,  26, 255 };
	}
}

static const char *glyph(int ch)
{
	switch (ch) {
	case 'A': return ".#." "#.#" "###" "#.#" "#.#";
	case 'B': return "##." "#.#" "##." "#.#" "##.";
	case 'C': return ".##" "#.." "#.." "#.." ".##";
	case 'D': return "##." "#.#" "#.#" "#.#" "##.";
	case 'E': return "###" "#.." "##." "#.." "###";
	case 'F': return "###" "#.." "##." "#.." "#..";
	case 'G': return ".##" "#.." "#.#" "#.#" ".##";
	case 'H': return "#.#" "#.#" "###" "#.#" "#.#";
	case 'I': return "###" ".#." ".#." ".#." "###";
	case 'J': return "..#" "..#" "..#" "#.#" ".#.";
	case 'K': return "#.#" "#.#" "##." "#.#" "#.#";
	case 'L': return "#.." "#.." "#.." "#.." "###";
	case 'M': return "#.#" "###" "###" "#.#" "#.#";
	case 'N': return "##." "#.#" "#.#" "#.#" "#.#";
	case 'O': return ".#." "#.#" "#.#" "#.#" ".#.";
	case 'P': return "##." "#.#" "##." "#.." "#..";
	case 'Q': return ".#." "#.#" "#.#" "###" ".##";
	case 'R': return "##." "#.#" "##." "#.#" "#.#";
	case 'S': return ".##" "#.." ".#." "..#" "##.";
	case 'T': return "###" ".#." ".#." ".#." ".#.";
	case 'U': return "#.#" "#.#" "#.#" "#.#" "###";
	case 'V': return "#.#" "#.#" "#.#" "#.#" ".#.";
	case 'W': return "#.#" "#.#" "###" "###" "#.#";
	case 'X': return "#.#" "#.#" ".#." "#.#" "#.#";
	case 'Y': return "#.#" "#.#" ".#." ".#." ".#.";
	case 'Z': return "###" "..#" ".#." "#.." "###";
	case '0': return "###" "#.#" "#.#" "#.#" "###";
	case '1': return ".#." "##." ".#." ".#." "###";
	case '2': return "##." "..#" ".#." "#.." "###";
	case '3': return "##." "..#" ".#." "..#" "##.";
	case '4': return "#.#" "#.#" "###" "..#" "..#";
	case '5': return "###" "#.." "##." "..#" "##.";
	case '6': return ".##" "#.." "###" "#.#" "###";
	case '7': return "###" "..#" ".#." ".#." ".#.";
	case '8': return "###" "#.#" "###" "#.#" "###";
	case '9': return "###" "#.#" "###" "..#" "##.";
	case ':': return "..." ".#." "..." ".#." "...";
	case '-': return "..." "..." "###" "..." "...";
	case '!': return ".#." ".#." ".#." "..." ".#.";
	case '.': return "..." "..." "..." "..." ".#.";
	case '/': return "..#" "..#" ".#." "#.." "#..";
	default:  return NULL;
	}
}

static unsigned hash(unsigned n)
{
	n = (n ^ 61) ^ (n >> 16);
	n *= 9;
	n ^= n >> 4;
	n *= 0x27d4eb2d;

	return n ^ (n >> 15);
}

/* Steel plates, lit from the top left.  lum[] is brightness before the blue tint. */
static SDL_Texture *make_plate(int id)
{
	static const int corner[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
	int wall = id == PLATE_WALL;
	int edge = wall ? 2 : 1;
	int lum[TILE][TILE];
	Uint8 px[TILE * TILE * 4];
	SDL_Texture *t;

	for (int y = 0; y < TILE; y++) {
		int streak = (int)(hash(y * 31 + id * 7) % 9) - 4;

		for (int x = 0; x < TILE; x++) {
			int grain = (int)(hash(x + y * TILE + id * 977) % 5) - 2;
			int l = (wall ? 128 : 64) + streak + grain + (TILE - 1 - x - y) / (wall ? 1 : 3);

			if (x < edge || y < edge)
				l += wall ? 44 : 18;
			if (x >= TILE - edge || y >= TILE - edge)
				l -= wall ? 50 : 24;
			lum[y][x] = l;
		}
	}

	for (int i = 0; i < 4; i++) {
		int far = TILE - (wall ? 4 : 3);
		int x = corner[i][0] ? far : 2, y = corner[i][1] ? far : 2;

		/* bolt head with a highlight and the shadow it casts */
		lum[y][x] += 80;
		lum[y + 1][x + 1] -= 40;
		if (wall) {
			lum[y][x + 1] += 30;
			lum[y + 1][x] += 30;
		}
	}

	if (wall) {
		int lo = 6, hi = TILE - 7;

		for (int n = lo; n <= hi; n++) {
			lum[lo][n] -= 34;
			lum[n][lo] -= 34;
			lum[hi][n] += 22;
			lum[n][hi] += 22;
		}
	} else if (id == PLATE_SEAM) {
		for (int x = 1; x < TILE - 1; x++) {
			lum[TILE / 2 - 1][x] -= 26;
			lum[TILE / 2][x] += 14;
		}
	} else if (id == PLATE_VENT) {
		for (int y = 5; y <= TILE - 7; y += 2) {
			for (int x = 5; x <= TILE - 6; x++) {
				lum[y][x] -= 38;
				lum[y + 1][x] += 10;
			}
		}
	}

	for (int i = 0; i < TILE * TILE; i++) {
		int l = lum[i / TILE][i % TILE];

		px[i * 4 + 0] = SDL_clamp(l * 0.62f, 0, 255);
		px[i * 4 + 1] = SDL_clamp(l * 0.84f, 0, 255);
		px[i * 4 + 2] = SDL_clamp(l * 1.26f, 0, 255);
		px[i * 4 + 3] = 255;
	}

	t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, TILE, TILE);
	if (t)
		SDL_UpdateTexture(t, NULL, px, TILE * 4);

	return t;
}

void gfx_init(SDL_Renderer *r)
{
	ren = r;
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

	for (int i = 0; i < PLATE_MAX; i++)
		plate[i] = make_plate(i);

	for (int i = 0; i < SPR_MAX; i++) {
		SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SPR, SPR, 32, SDL_PIXELFORMAT_RGBA32);
		Uint32 *px;

		if (!s)
			continue;
		SDL_FillRect(s, NULL, 0);
		px = s->pixels;

		for (int y = 0; y < SPR; y++) {
			const char *row = art[i][y];

			if (!row || strlen(row) != SPR) {
				SDL_Log("sprite %d row %d: expected %d columns", i, y, SPR);
				continue;
			}
			for (int x = 0; x < SPR; x++) {
				SDL_Color c = pal(row[x]);

				if (row[x] != '.')
					px[y * (s->pitch / 4) + x] = SDL_MapRGBA(s->format, c.r, c.g, c.b, 255);
			}
		}

		tex[i] = SDL_CreateTextureFromSurface(ren, s);
		SDL_SetTextureBlendMode(tex[i], SDL_BLENDMODE_BLEND);
		SDL_FreeSurface(s);
	}
}

void gfx_sprite(int id, float x, float y, double angle, int scale, int mode)
{
	int sz = SPR * scale;
	SDL_Rect dst = { (int)floorf(x) - sz / 2, (int)floorf(y) - sz / 2, sz, sz };
	SDL_Texture *t = tex[id];

	if (mode == DRAW_SHADOW) {
		SDL_SetTextureColorMod(t, 0, 0, 0);
		SDL_SetTextureAlphaMod(t, 90);
	}

	SDL_RenderCopyEx(ren, t, NULL, &dst, angle, NULL, SDL_FLIP_NONE);

	if (mode == DRAW_FLASH) {
		SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
		SDL_RenderCopyEx(ren, t, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
		SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	} else if (mode == DRAW_SHADOW) {
		SDL_SetTextureColorMod(t, 255, 255, 255);
		SDL_SetTextureAlphaMod(t, 255);
	}
}

void gfx_plate(int id, int x, int y, SDL_Color tint)
{
	SDL_Rect dst = { x, y, TILE, TILE };

	SDL_SetTextureColorMod(plate[id], tint.r, tint.g, tint.b);
	SDL_RenderCopy(ren, plate[id], NULL, &dst);
}

void gfx_rect(int x, int y, int w, int h, SDL_Color c)
{
	SDL_Rect r = { x, y, w, h };

	SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
	SDL_RenderFillRect(ren, &r);
}

void gfx_text(int x, int y, int scale, SDL_Color c, const char *fmt, ...)
{
	char buf[64];
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (len >= (int)sizeof(buf))
		len = sizeof(buf) - 1;

	if (x < 0)
		x = (SCREEN_W - (len * 4 - 1) * scale) / 2;

	for (int i = 0; i < len; i++, x += 4 * scale) {
		const char *g = glyph(toupper((unsigned char)buf[i]));

		if (!g)
			continue;
		for (int n = 0; n < 15; n++) {
			if (g[n] == '#')
				gfx_rect(x + (n % 3) * scale, y + (n / 3) * scale, scale, scale, c);
		}
	}
}
