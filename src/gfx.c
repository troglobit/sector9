/* SPDX-License-Identifier: ISC */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"

#define RES       6	/* sprite texels per game pixel */
#define PLATE_RES 4
#define PLATE_PX  (TILE * PLATE_RES)
#define LOGO_RES  5
#define LOGO_W    208
#define LOGO_H    76
#define FONTS     8
#define FONT_W    3
#define FONT_H    5
#define FONT_ADV  4
#define ORB_R     3

struct rgb { float r, g, b; };

/* 'c' circle:  cx, cy, r
 * 'b' box:     cx, cy, half w, half h, corner r
 * 'l' line:    x0, y0, x1, y1, r
 * 'p' polygon: n, then n convex points, clockwise on screen
 * mirror repeats the shape on the other side of x = 0. */
struct shape {
	char  kind, col, mirror;
	float v[11];
};

struct model {
	float               half, line;
	int                 flat;
	const struct shape *shape;
};

struct font {
	SDL_Texture *tex;
	int          key, cw, ch;
};

static SDL_Renderer *ren;
static SDL_Texture  *tex[SPR_MAX];
static SDL_Texture  *plate[THEME_MAX][PLATE_MAX];
static SDL_Texture  *logo;
static struct font   font[FONTS];

/* All craft face up, except the dart, which dives. */
static const struct shape ship[] = {
	{ 'p', 'g', 1, { 4, 1, -1.5f, 7.4f, 4.6f, 7.4f, 6.4f, 1, 5 } },
	{ 'l', 'd', 1, { 6.8f, 2.6f, 6.8f, 6.4f, 0.7f } },
	{ 'p', 'w', 0, { 5, 0, -7.6f, 2.2f, -1, 2.2f, 5.6f, -2.2f, 5.6f, -2.2f, -1 } },
	{ 'l', 'b', 0, { 0, -3, 0, 0.2f, 1.1f } },
	{ 'c', 'c', 0, { -0.3f, -2.9f, 0.45f } },
	{ 'b', 'o', 1, { 1.2f, 6.1f, 0.8f, 0.9f, 0.3f } },
	{ 'c', 'y', 1, { 1.2f, 6.5f, 0.4f } },
	{ 0 }
};

static const struct shape tank[] = {
	{ 'b', 'd', 1, { 5.6f, 0.5f, 1.7f, 6.6f, 0.8f } },
	{ 'l', 'k', 1, { 4.6f, -4, 6.6f, -4, 0.2f } },
	{ 'l', 'k', 1, { 4.6f, -1.5f, 6.6f, -1.5f, 0.2f } },
	{ 'l', 'k', 1, { 4.6f, 1, 6.6f, 1, 0.2f } },
	{ 'l', 'k', 1, { 4.6f, 3.5f, 6.6f, 3.5f, 0.2f } },
	{ 'b', 'G', 0, { 0, 0.8f, 4.1f, 5.2f, 1 } },
	{ 'b', 'H', 0, { 0, 4.2f, 2.6f, 1, 0.4f } },
	{ 'l', 'g', 0, { 0, -1, 0, -7, 0.85f } },
	{ 'c', 'g', 0, { 0, 0, 2.7f } },
	{ 'c', 'd', 0, { 0, 0.3f, 1.1f } },
	{ 0 }
};

static const struct shape turret[] = {
	{ 'b', 'd', 0, { 0, 0, 6.6f, 6.6f, 2.6f } },
	{ 'c', 'g', 1, { 4.6f, -4.6f, 0.6f } },
	{ 'c', 'g', 1, { 4.6f, 4.6f, 0.6f } },
	{ 'c', 'r', 0, { 0, 0, 4.9f } },
	{ 'c', 'd', 0, { 0, 0, 3.4f } },
	{ 'c', 'o', 0, { 0, 0, 2.3f } },
	{ 0 }
};

static const struct shape gun[] = {
	{ 'l', 'w', 0, { 0, 0, 0, -6.6f, 0.9f } },
	{ 'b', 'g', 0, { 0, -6.6f, 1.4f, 0.7f, 0.2f } },
	{ 'c', 'y', 0, { 0, 0, 1.5f } },
	{ 0 }
};

static const struct shape pod[] = {
	{ 'b', 'd', 0, { 0, 0, 13.4f, 13.4f, 4.5f } },
	{ 'b', 'P', 0, { 0, 0, 11, 11, 3.2f } },
	{ 'c', 'g', 1, { 8.6f, -8.6f, 1 } },
	{ 'c', 'g', 1, { 8.6f, 8.6f, 1 } },
	{ 'c', 'r', 0, { 0, 0, 8.6f } },
	{ 'c', 'd', 0, { 0, 0, 6.6f } },
	{ 'c', 'o', 0, { 0, 0, 4.8f } },
	{ 0 }
};

static const struct shape crawler[] = {
	{ 'l', 'd', 1, { 2.5f, -3, 6.6f, -5.6f, 0.45f } },
	{ 'l', 'd', 1, { 3, 0.2f, 7.2f, 0.2f, 0.45f } },
	{ 'l', 'd', 1, { 2.5f, 3.2f, 6.6f, 5.8f, 0.45f } },
	{ 'b', 'p', 0, { 0, 0.8f, 3.7f, 4.8f, 3.4f } },
	{ 'b', 'P', 0, { 0, 1.4f, 2.2f, 0.45f, 0.3f } },
	{ 'b', 'P', 0, { 0, 3.2f, 1.6f, 0.45f, 0.3f } },
	{ 'c', 'P', 0, { 0, -4.2f, 2.3f } },
	{ 'c', 'y', 1, { 1, -5, 0.6f } },
	{ 0 }
};

static const struct shape drone[] = {
	{ 'c', 'b', 0, { 0, 0, 6.8f } },
	{ 'c', 'd', 0, { 0, 0, 4.6f } },
	{ 'c', 'c', 0, { 0, 0, 3 } },
	{ 'c', 'w', 0, { -0.9f, -0.9f, 0.8f } },
	{ 'c', 'y', 1, { 5.7f, 0, 0.6f } },
	{ 'c', 'y', 0, { 0, -5.7f, 0.6f } },
	{ 'c', 'y', 0, { 0, 5.7f, 0.6f } },
	{ 0 }
};

static const struct shape dart[] = {
	{ 'p', 'R', 1, { 4, 1.5f, -3.5f, 6.6f, -7.4f, 6, -2.6f, 2.6f, 2 } },
	{ 'p', 'r', 0, { 4, 0, 7.5f, -3, -2.5f, 0, -4.6f, 3, -2.5f } },
	{ 'l', 'o', 0, { 0, -0.5f, 0, 2.6f, 0.9f } },
	{ 'c', 'y', 0, { 0, 2.6f, 0.45f } },
	{ 0 }
};

static const struct shape core[] = {
	{ 'b', 'P', 1, { 18.4f, 0, 4, 7.5f, 1.6f } },
	{ 'b', 'P', 0, { 0, -18.4f, 7.5f, 4, 1.6f } },
	{ 'b', 'P', 0, { 0, 18.4f, 7.5f, 4, 1.6f } },
	{ 'c', 'p', 0, { 0, 0, 19 } },
	{ 'c', 'g', 1, { 11.8f, -11.8f, 1.1f } },
	{ 'c', 'g', 1, { 11.8f, 11.8f, 1.1f } },
	{ 'c', 'd', 0, { 0, 0, 14.4f } },
	{ 'c', 'r', 0, { 0, 0, 12.4f } },
	{ 'c', 'o', 0, { 0, 0, 8.4f } },
	{ 'c', 'y', 0, { 0, 0, 5 } },
	{ 'c', 'w', 0, { 0, 0, 2.2f } },
	{ 0 }
};

static const struct shape rover[] = {
	{ 'b', 'd', 1, { 4.9f, -4, 1.7f, 2.1f, 0.7f } },
	{ 'b', 'd', 1, { 4.9f, 4.2f, 1.7f, 2.1f, 0.7f } },
	{ 'b', 'o', 0, { 0, 0.2f, 3.9f, 5.6f, 1.2f } },
	{ 'b', 'r', 0, { 0, 1.6f, 2.5f, 2.6f, 0.7f } },
	{ 'l', 'd', 0, { 0, 0.6f, 0, -6.8f, 0.65f } },
	{ 'c', 'd', 0, { 0, 1.2f, 1.2f } },
	{ 0 }
};

static const struct shape gunship[] = {
	{ 'p', 'd', 1, { 4, 2, -7, 11.6f, -4, 11.6f, 1, 2, 4 } },
	{ 'l', 'R', 1, { 9.4f, -5.4f, 9.4f, 2.6f, 1.5f } },
	{ 'c', 'o', 1, { 9.4f, -5.6f, 0.8f } },
	{ 'b', 'g', 0, { 0, -0.5f, 3.6f, 9.4f, 2.6f } },
	{ 'l', 'd', 1, { 1.8f, 7, 1.8f, 11, 0.6f } },
	{ 'l', 'o', 0, { 0, 2, 0, 6, 1.5f } },
	{ 'c', 'y', 0, { 0, 5.8f, 0.7f } },
	{ 'b', 'd', 0, { 0, -6, 2.2f, 1.2f, 0.5f } },
	{ 0 }
};

static const struct shape heavy[] = {
	{ 'b', 'd', 1, { 6.9f, 0.4f, 2.1f, 8.6f, 1 } },
	{ 'l', 'k', 1, { 5.6f, -5, 8.2f, -5, 0.25f } },
	{ 'l', 'k', 1, { 5.6f, -1.8f, 8.2f, -1.8f, 0.25f } },
	{ 'l', 'k', 1, { 5.6f, 1.4f, 8.2f, 1.4f, 0.25f } },
	{ 'l', 'k', 1, { 5.6f, 4.6f, 8.2f, 4.6f, 0.25f } },
	{ 'b', 'R', 0, { 0, 0.6f, 5.2f, 7, 1.3f } },
	{ 'l', 'd', 1, { 1.4f, -1, 1.4f, -9.4f, 0.7f } },
	{ 'b', 'o', 0, { 0, 1, 3.4f, 3.6f, 1.2f } },
	{ 'c', 'y', 0, { 0, 1.4f, 1.2f } },
	{ 0 }
};

static const struct shape fused[] = {
	{ 'b', 'd', 1, { 8.2f, 2.4f, 1.6f, 5.6f, 0.8f } },
	{ 'l', 'k', 1, { 7.2f, 0, 9.2f, 0, 0.2f } },
	{ 'l', 'k', 1, { 7.2f, 2.4f, 9.2f, 2.4f, 0.2f } },
	{ 'l', 'k', 1, { 7.2f, 4.8f, 9.2f, 4.8f, 0.2f } },
	{ 'p', 'G', 1, { 4, 1, -3.4f, 7.6f, 2.6f, 7.6f, 6, 1, 5.4f } },
	{ 'l', 'g', 1, { 4.4f, 1, 4.4f, -6, 0.6f } },
	{ 'p', 'w', 0, { 5, 0, -10, 2.7f, -2, 2.7f, 7.2f, -2.7f, 7.2f, -2.7f, -2 } },
	{ 'l', 'b', 0, { 0, -4.4f, 0, -0.6f, 1.3f } },
	{ 'c', 'c', 0, { -0.3f, -4.3f, 0.5f } },
	{ 'c', 'y', 0, { 0, 3.6f, 1.3f } },
	{ 'b', 'o', 1, { 1.4f, 7.8f, 0.9f, 1, 0.3f } },
	{ 'c', 'y', 1, { 1.4f, 8.3f, 0.45f } },
	{ 0 }
};

static const struct shape buddy[] = {
	{ 'l', 'g', 1, { 1.6f, 0.2f, 3.3f, 1.6f, 0.45f } },
	{ 'c', 'c', 0, { 0, 0, 2.4f } },
	{ 'c', 'w', 0, { 0, -0.3f, 1 } },
	{ 0 }
};

static const struct shape capsule[] = {
	{ 'b', 'g', 0, { 0, 0, 5.2f, 5.2f, 1.9f } },
	{ 'b', 'W', 0, { 0, 0, 4.1f, 4.1f, 1.3f } },
	{ 0 }
};

static const struct shape orb[] = {
	{ 'c', 'W', 0, { 0, 0, ORB_R } },
	{ 0 }
};

static const struct model model[SPR_MAX] = {
	[SPR_SHIP]    = {  9, 0.5f, 0, ship },
	[SPR_TANK]    = {  9, 0.5f, 0, tank },
	[SPR_TURRET]  = {  8, 0.5f, 0, turret },
	[SPR_GUN]     = {  8, 0.4f, 0, gun },
	[SPR_POD]     = { 15, 0.6f, 0, pod },
	[SPR_CRAWLER] = {  9, 0.5f, 0, crawler },
	[SPR_DRONE]   = {  8, 0.5f, 0, drone },
	[SPR_DART]    = {  9, 0.5f, 0, dart },
	[SPR_CORE]    = { 24, 0.7f, 0, core },
	[SPR_ROVER]   = {  8, 0.5f, 0, rover },
	[SPR_GUNSHIP] = { 13, 0.5f, 0, gunship },
	[SPR_HEAVY]   = { 11, 0.5f, 0, heavy },
	[SPR_FUSED]   = { 12, 0.5f, 0, fused },
	[SPR_BUDDY]   = {  5, 0.4f, 0, buddy },
	[SPR_CAPSULE] = {  6, 0.4f, 0, capsule },
	[SPR_ORB]     = {  4, 0,    1, orb },
};

static struct rgb pal(char ch)
{
	switch (ch) {
	case 'W': return (struct rgb){ 255, 255, 255 };
	case 'w': return (struct rgb){ 226, 232, 240 };
	case 'g': return (struct rgb){ 150, 160, 178 };
	case 'd': return (struct rgb){  76,  84, 102 };
	case 'b': return (struct rgb){  52,  96, 208 };
	case 'c': return (struct rgb){  96, 220, 240 };
	case 'r': return (struct rgb){ 208,  52,  60 };
	case 'R': return (struct rgb){ 150,  36,  52 };
	case 'o': return (struct rgb){ 240, 140,  40 };
	case 'y': return (struct rgb){ 250, 226,  90 };
	case 'G': return (struct rgb){  70, 160,  84 };
	case 'H': return (struct rgb){  44, 112,  64 };
	case 'p': return (struct rgb){ 150,  84, 196 };
	case 'P': return (struct rgb){ 100,  56, 140 };
	default:  return (struct rgb){  14,  16,  24 };
	}
}

/* Strokes on a grid 4 wide and 8 tall, two digits per point, a space lifts the pen. */
static const char *glyph(int ch)
{
	switch (ch) {
	case 'A': return "0803204348 0545";
	case 'B': return "00083847453404 3443413000";
	case 'C': return "4130100107183847";
	case 'D': return "00082846422000";
	case 'E': return "40000848 0434";
	case 'F': return "400008 0434";
	case 'G': return "41301001071838474424";
	case 'H': return "0008 4048 0444";
	case 'I': return "1030 2028 1838";
	case 'J': return "4047381807";
	case 'K': return "0008 400448";
	case 'L': return "000848";
	case 'M': return "0800244048";
	case 'N': return "08004840";
	case 'O': return "103041473818070110";
	case 'P': return "08003041433404";
	case 'Q': return "103041473818070110 2648";
	case 'R': return "08003041433404 2448";
	case 'S': return "413010010314344547381807";
	case 'T': return "0040 2028";
	case 'U': return "000718384740";
	case 'V': return "002840";
	case 'W': return "0008244840";
	case 'X': return "0048 4008";
	case 'Y': return "002440 2428";
	case 'Z': return "00400848";
	case '0': return "103041473818070110 1632";
	case '1': return "122028 1838";
	case '2': return "01103041430848";
	case '3': return "01103041433414 344547381807";
	case '4': return "300545 3038";
	case '5': return "400004344547381807";
	case '6': return "4130100107183847453404";
	case '7': return "004018";
	case '8': return "103041433414030110 1405071838474534";
	case '9': return "4414030110304147381807";
	case ':': return "2223 2526";
	case '-': return "1434";
	case '+': return "2226 0444";
	case '!': return "2025 2728";
	case '.': return "2728";
	case '/': return "4008";
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

static float clamp01(float v)
{
	return SDL_clamp(v, 0.0f, 1.0f);
}

static float mix(float a, float b, float t)
{
	return a + (b - a) * t;
}

static float seg_dist(float x, float y, float ax, float ay, float bx, float by)
{
	float dx = bx - ax, dy = by - ay;
	float len = dx * dx + dy * dy;
	float t = len > 0 ? clamp01(((x - ax) * dx + (y - ay) * dy) / len) : 0;

	return hypotf(x - ax - dx * t, y - ay - dy * t);
}

/* Distance from x,y to the nearest stroke of a glyph laid out with cell size sx,sy. */
static float glyph_dist(const char *g, float x, float y, float sx, float sy)
{
	float d = 1e9f, px = 0, py = 0;
	int pen = 0;

	for (; *g; g += 2) {
		float qx, qy;

		if (*g == ' ') {
			pen = 0;
			g--;
			continue;
		}
		qx = (g[0] - '0') * sx;
		qy = (g[1] - '0') * sy;
		if (pen)
			d = fminf(d, seg_dist(x, y, px, py, qx, qy));
		px = qx;
		py = qy;
		pen = 1;
	}

	return d;
}

static float shape_dist(const struct shape *s, float x, float y)
{
	const float *v = s->v;
	float qx, qy, d = -1e9f;
	int n;

	switch (s->kind) {
	case 'c':
		return hypotf(x - v[0], y - v[1]) - v[2];
	case 'b':
		qx = fabsf(x - v[0]) - v[2] + v[4];
		qy = fabsf(y - v[1]) - v[3] + v[4];
		return hypotf(fmaxf(qx, 0), fmaxf(qy, 0)) + fminf(fmaxf(qx, qy), 0) - v[4];
	case 'l':
		return seg_dist(x, y, v[0], v[1], v[2], v[3]) - v[4];
	}

	n = (int)v[0];
	v++;
	for (int i = 0; i < n; i++) {
		const float *a = &v[i * 2], *b = &v[(i + 1) % n * 2];
		float ex = b[0] - a[0], ey = b[1] - a[1];

		d = fmaxf(d, ((x - a[0]) * ey - (y - a[1]) * ex) / hypotf(ex, ey));
	}

	return d;
}

static float sdf(const struct shape *s, float x, float y)
{
	float d = shape_dist(s, x, y);

	return s->mirror ? fminf(d, shape_dist(s, -x, y)) : d;
}

/* Brightness of a point d inside an edge, lit from the top left.  near is the
 * distance at x - off, y - off. */
static float bevel(float d, float near, float off, float band)
{
	return 1 + 0.38f * (near - d) / (off * 1.4f) * clamp01(1 + d / band);
}

static struct rgb shade(struct rgb c, float k)
{
	return (struct rgb){ c.r * k, c.g * k, c.b * k };
}

static struct rgb ramp(struct rgb a, struct rgb b, float t)
{
	t = clamp01(t);

	return (struct rgb){ mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t) };
}

/* Frees px. */
static SDL_Texture *upload(Uint8 *px, int w, int h, SDL_ScaleMode mode)
{
	SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);

	if (t) {
		SDL_UpdateTexture(t, NULL, px, w * 4);
		SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(t, mode);
	}
	free(px);

	return t;
}

static void put(Uint8 *px, struct rgb c, float a)
{
	px[0] = SDL_clamp(c.r, 0, 255);
	px[1] = SDL_clamp(c.g, 0, 255);
	px[2] = SDL_clamp(c.b, 0, 255);
	px[3] = clamp01(a) * 255;
}

static SDL_Texture *make_sprite(const struct model *m)
{
	int size = m->half * 2 * RES;
	Uint8 *px = malloc(size * size * 4);

	if (!px)
		return NULL;

	for (int i = 0; i < size * size; i++) {
		float x = (i % size + 0.5f) / RES - m->half;
		float y = (i / size + 0.5f) / RES - m->half;
		struct rgb c = pal(m->flat ? 'W' : 0);
		float all = 1e9f;

		for (const struct shape *s = m->shape; s->kind; s++) {
			float d = sdf(s, x, y);
			float cov = clamp01(0.5f - d * RES);
			float k;

			all = fminf(all, d);
			if (cov <= 0 || m->flat)
				continue;
			k = bevel(d, sdf(s, x - 0.25f, y - 0.25f), 0.25f, 1.3f) - 0.1f * (x + y) / m->half;
			c = ramp(c, shade(pal(s->col), k), cov);
		}

		put(&px[i * 4], c, 0.5f - (all - m->line) * RES);
	}

	return upload(px, size, size, SDL_ScaleModeLinear);
}

/* 1 on a thin line at d = 0, fading out to either side. */
static float ridge(float d)
{
	return clamp01(1 - fabsf(d) * 2.5f);
}

/* A slot or panel edge: dark on the lit side, bright on the far side. */
static float groove(float d, float lit)
{
	return ridge(d) * (lit > 0 ? -34 : 24);
}

/* Plates lit from the top left.  l is brightness before the theme's tint;
 * ember plates are dark and their slots and seams glow. */
static SDL_Texture *make_plate(int id, int theme)
{
	int wall = id == PLATE_WALL;
	float edge = wall ? 1.5f : 0.6f;
	float inset = wall ? 3.4f : 2.6f;
	Uint8 *px = malloc(PLATE_PX * PLATE_PX * 4);

	if (!px)
		return NULL;

	for (int i = 0; i < PLATE_PX * PLATE_PX; i++) {
		int ix = i % PLATE_PX, iy = i / PLATE_PX;
		float x = (ix + 0.5f) / PLATE_RES, y = (iy + 0.5f) / PLATE_RES;
		float brushed = (int)(hash(iy * 31 + id * 7) % 9) - 4 + (int)(hash(i + id * 977) % 5) - 2;
		float l = (wall ? 128 : 64) + brushed + (TILE - x - y) / (wall ? 1 : 3);
		float bx = fabsf(x - TILE / 2) - (TILE / 2 - inset);
		float by = fabsf(y - TILE / 2) - (TILE / 2 - inset);
		float bolt = hypotf(bx, by), glow = 0;

		if (x < edge || y < edge)
			l += wall ? 44 : 18;
		if (x >= TILE - edge || y >= TILE - edge)
			l -= wall ? 50 : 24;

		/* bolt head, round and lit on one side, with the shadow it casts */
		if (bolt < 1) {
			float sx = x < TILE / 2 ? bx : -bx, sy = y < TILE / 2 ? by : -by;

			l += 46 + 34 * (sx + sy);
		} else if (hypotf(bx - (x < TILE / 2 ? 0.5f : -0.5f), by - (y < TILE / 2 ? 0.5f : -0.5f)) < 1.2f) {
			l -= 30;
		}

		if (wall) {
			float d = fmaxf(fabsf(x - TILE / 2), fabsf(y - TILE / 2)) - 4;

			l += groove(d, (TILE - x - y));
			glow = ridge(d) * 0.6f;
			if (d < 0)
				l -= 10;
		} else if (id == PLATE_SEAM) {
			if (x > 1 && x < TILE - 1) {
				l += groove(y - TILE / 2 + 0.2f, 1) + groove(y - TILE / 2 - 0.3f, -1);
				glow = ridge(y - TILE / 2 + 0.2f) * 0.7f;
			}
		} else if (id == PLATE_VENT) {
			for (int n = 0; n < 4; n++) {
				float sy = 6.5f + n * 2.4f;
				float d = seg_dist(x, y, 6, sy, TILE - 6, sy) - 0.6f;

				if (d < 0) {
					l -= 40;
					glow = 1;
				} else if (d < 0.45f && y > sy) {
					l += 16;
				}
			}
		}

		if (theme == THEME_EMBER)
			put(&px[i * 4], (struct rgb){ l * 0.8f + glow * 215, l * 0.56f + glow * 90, l * 0.56f }, 1);
		else
			put(&px[i * 4], (struct rgb){ l * 0.62f, l * 0.84f, l * 1.26f }, 1);
	}

	return upload(px, PLATE_PX, PLATE_PX, SDL_ScaleModeLinear);
}

/* Blends colour c with coverage a on top of a premultiplied pixel. */
static void over(float *p, struct rgb c, float a)
{
	a = clamp01(a);
	p[0] = c.r * a + p[0] * (1 - a);
	p[1] = c.g * a + p[1] * (1 - a);
	p[2] = c.b * a + p[2] * (1 - a);
	p[3] = a + p[3] * (1 - a);
}

static float word_dist(float x, float y)
{
	static const char word[] = "SECTOR";
	float d = 1e9f;

	for (int i = 0; word[i]; i++) {
		float gx = x - 12 - i * 21.5f;

		if (gx > -6 && gx < 20)
			d = fminf(d, glyph_dist(glyph(word[i]), gx, y - 30, 3.4f, 3));
	}

	return d - 1.9f;
}

static float nine_dist(float x, float y)
{
	static const float tail[][2] = { { 185, 31 }, { 185, 41 }, { 181, 51 }, { 172, 58 }, { 160, 61 } };
	float d = fabsf(hypotf(x - 172, y - 31) - 13);

	for (int i = 0; i + 1 < (int)SDL_arraysize(tail); i++)
		d = fminf(d, seg_dist(x, y, tail[i][0], tail[i][1], tail[i + 1][0], tail[i + 1][1]));

	return d - 4.6f;
}

static float streak_dist(float x, float y)
{
	return fminf(seg_dist(x, y, 8, 61, 146, 61) - 0.7f, seg_dist(x, y, 34, 65.5f, 132, 65.5f) - 0.5f);
}

/* Chrome word and a hot numeral, both leaning forward. */
static SDL_Texture *make_logo(void)
{
	static const struct rgb ink = { 6, 10, 26 };
	int w = LOGO_W * LOGO_RES, h = LOGO_H * LOGO_RES;
	Uint8 *px = malloc(w * h * 4);

	if (!px)
		return NULL;

	for (int i = 0; i < w * h; i++) {
		float y = (i / w + 0.5f) / LOGO_RES;
		float x = (i % w + 0.5f) / LOGO_RES - (54 - y) * 0.2f;
		float word = word_dist(x, y), nine = nine_dist(x, y), line = streak_dist(x, y);
		float p[4] = { 0 };
		struct rgb c;
		float v;

		over(p, (struct rgb){ 60, 190, 255 }, 0.4f * expf(-fmaxf(word, 0) / 2.5f));
		over(p, (struct rgb){ 255, 120, 30 }, 0.45f * expf(-fmaxf(nine, 0) / 3.5f));
		over(p, (struct rgb){ 96, 220, 240 }, 0.5f - line * LOGO_RES);
		over(p, ink, 0.5f - (fminf(word, nine) - 1.1f) * LOGO_RES);

		v = (y - 28) / 28;
		c = v < 0.5f ? ramp((struct rgb){ 236, 250, 255 }, (struct rgb){ 84, 150, 232 }, v * 2.2f)
			     : ramp((struct rgb){ 28, 40, 84 }, (struct rgb){ 190, 206, 228 }, v * 2.6f - 1.3f);
		over(p, shade(c, bevel(word, word_dist(x - 0.3f, y - 0.3f), 0.3f, 1)), 0.5f - word * LOGO_RES);

		v = (y - 14) / 52;
		c = v < 0.5f ? ramp((struct rgb){ 255, 246, 176 }, (struct rgb){ 255, 164, 40 }, v * 2)
			     : ramp((struct rgb){ 255, 164, 40 }, (struct rgb){ 214, 52, 44 }, v * 2 - 1);
		over(p, shade(c, bevel(nine, nine_dist(x - 0.4f, y - 0.4f), 0.4f, 2.2f)), 0.5f - nine * LOGO_RES);

		put(&px[i * 4], shade((struct rgb){ p[0], p[1], p[2] }, p[3] > 0 ? 1 / p[3] : 0), p[3]);
	}

	return upload(px, w, h, SDL_ScaleModeLinear);
}

/* Glyphs drawn for one size on screen, k real pixels per font pixel. */
static const struct font *get_font(float k)
{
	static int next;
	int key = (int)(k * 16 + 0.5f);
	float r = fmaxf(0.3f, 0.62f / k);
	struct font *f;
	Uint8 *px;
	int w, h;

	for (int i = 0; i < FONTS; i++) {
		if (font[i].key == key)
			return &font[i];
	}

	f = &font[next++ % FONTS];
	if (f->tex)
		SDL_DestroyTexture(f->tex);
	f->key = key;
	f->cw = (int)ceilf(FONT_W * k);
	f->ch = (int)ceilf(FONT_H * k);
	w = f->cw * 8;
	h = f->ch * 8;

	px = calloc(w * h, 4);
	if (!px) {
		f->tex = NULL;
		return f;
	}

	for (int ch = 32; ch < 96; ch++) {
		const char *g = glyph(ch);
		int ox = (ch - 32) % 8 * f->cw, oy = (ch - 32) / 8 * f->ch;

		for (int n = 0; g && n < f->cw * f->ch; n++) {
			int x = n % f->cw, y = n / f->cw;
			float d = glyph_dist(g, (x + 0.5f) / k - 0.35f, (y + 0.5f) / k - 0.35f,
					     (FONT_W - 0.7f) / 4, (FONT_H - 0.7f) / 8);

			put(&px[((oy + y) * w + ox + x) * 4], pal('W'), 0.5f - (d - r) * k);
		}
	}

	f->tex = upload(px, w, h, SDL_ScaleModeNearest);

	return f;
}

float gfx_scale(void)
{
	float sx, sy;

	SDL_RenderGetScale(ren, &sx, &sy);

	return sy > 0 ? sy : 1;
}

void gfx_init(SDL_Renderer *r)
{
	ren = r;
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

	for (int theme = 0; theme < THEME_MAX; theme++) {
		for (int i = 0; i < PLATE_MAX; i++)
			plate[theme][i] = make_plate(i, theme);
	}
	for (int i = 0; i < SPR_MAX; i++)
		tex[i] = make_sprite(&model[i]);
	logo = make_logo();
}

static void draw(int id, float x, float y, double angle, float scale)
{
	float sz = model[id].half * 2 * scale;
	SDL_FRect dst = { x - sz / 2, y - sz / 2, sz, sz };

	SDL_RenderCopyExF(ren, tex[id], NULL, &dst, angle, NULL, SDL_FLIP_NONE);
}

void gfx_sprite(int id, float x, float y, double angle, float scale, int mode)
{
	draw(id, x, y, angle, scale);

	if (mode == DRAW_FLASH) {
		SDL_SetTextureBlendMode(tex[id], SDL_BLENDMODE_ADD);
		draw(id, x, y, angle, scale);
		SDL_SetTextureBlendMode(tex[id], SDL_BLENDMODE_BLEND);
	}
}

void gfx_tinted(int id, float x, float y, float scale, SDL_Color c)
{
	SDL_SetTextureColorMod(tex[id], c.r, c.g, c.b);
	SDL_SetTextureAlphaMod(tex[id], c.a);
	draw(id, x, y, 0, scale);
	SDL_SetTextureColorMod(tex[id], 255, 255, 255);
	SDL_SetTextureAlphaMod(tex[id], 255);
}

void gfx_disc(float x, float y, float r, SDL_Color c)
{
	gfx_tinted(SPR_ORB, x, y, r / ORB_R, c);
}

void gfx_plate(int id, int theme, float x, float y, SDL_Color tint)
{
	float s = gfx_scale();
	float x0 = roundf(x * s) / s, y0 = roundf(y * s) / s;
	SDL_FRect dst = { x0, y0, roundf((x + TILE) * s) / s - x0, roundf((y + TILE) * s) / s - y0 };

	SDL_SetTextureColorMod(plate[theme][id], tint.r, tint.g, tint.b);
	SDL_RenderCopyF(ren, plate[theme][id], NULL, &dst);
}

void gfx_rect(float x, float y, float w, float h, SDL_Color c)
{
	SDL_FRect r = { x, y, w, h };

	SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
	SDL_RenderFillRectF(ren, &r);
}

void gfx_logo(float y)
{
	SDL_FRect dst = { (SCREEN_W - LOGO_W) / 2, y, LOGO_W, LOGO_H };

	SDL_RenderCopyF(ren, logo, NULL, &dst);
}

void gfx_text(float x, float y, int scale, SDL_Color c, const char *fmt, ...)
{
	float s = gfx_scale();
	const struct font *f = get_font(scale * s);
	char buf[64];
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (len >= (int)sizeof(buf))
		len = sizeof(buf) - 1;

	if (!f->tex)
		return;
	if (x < 0)
		x = (SCREEN_W - (len * FONT_ADV - 1) * scale) / 2;
	y = roundf(y * s) / s;

	SDL_SetTextureColorMod(f->tex, c.r, c.g, c.b);
	SDL_SetTextureAlphaMod(f->tex, c.a);

	for (int i = 0; i < len; i++, x += FONT_ADV * scale) {
		int ch = toupper((unsigned char)buf[i]) - 32;
		SDL_Rect src = { ch % 8 * f->cw, ch / 8 * f->ch, f->cw, f->ch };
		SDL_FRect dst = { roundf(x * s) / s, y, f->cw / s, f->ch / s };

		if (ch > 0 && ch < 64)
			SDL_RenderCopyF(ren, f->tex, &src, &dst);
	}
}
