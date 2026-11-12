/* SPDX-License-Identifier: ISC */
#include <math.h>
#include <string.h>

#include "game.h"

#define MAX_ENEMY  64
#define MAX_BULLET 160
#define MAX_PART   200
#define BLAST_LIFE 20
#define MAX_SHAKE  10
#define SCAN_GAP   60
#define SPIRAL_TURN 0.07f	/* radians per frame */
#define CORE_CYCLE 300	/* frames between swoops, and between rovers */
#define MAX_ITEM   8
#define MAX_HP     4
#define MAX_WEAPON 3
#define MAX_SPEED  2
#define MAX_BUDDY  2
#define RISE_FRAMES 40
#define START_CAM  (lv.rows * TILE - SCREEN_H)
#define MISSILE_LIFE 180
#define MISSILE_SPEED 3.2f
#define SCROLL_Y   110
#define WIGGLE_FRAMES 12
#define DIAG_FRAMES 8
#define SHIP_SCROLL 0.6f
#define SHIP_HURRY  2.0f
#define HALT_Y     100
#define MARK_NEAR  3
#define MARK_REACH 16
#define BOT_IDLE   480	/* frames without scoring before the demo tries the other shape */
#define LIFE_SCORE 20000
#define HOVER_Y    56
#define FUSE_FRAMES 480
#define FUSE_BURN  150
#define FUSE_AT    330

enum { ST_TITLE, ST_PLAY, ST_CLEAR, ST_FUSE, ST_OVER };
enum { IT_GUN, IT_SHIELD, IT_SPEED, IT_BUDDY, IT_MISSILE, IT_BOMB, IT_MAX };
enum { L_GROUND = 1, L_AIR = 2 };

struct enemy  { int alive, type, delay, hp, full, lifted, t, flash; float x, y, vx; };
struct bullet { int alive, friendly, level; float x, y, vx, vy; int homing; };
struct part   { int life; float x, y, vx, vy; SDL_Color c; float blast; };
struct item   { int alive, kind, rise; float x, y; };
struct player { float x, y; int ship, dir, diag, hp, weapon, speed, buddies, missiles, missile_cd, reload, morph_cd, invuln, dead, fused; };
struct axis   { int sign, seen, flip; };

static struct {
	int           state, sector, score, hiscore, lives, timer, frame;
	float         mx, my;
	int           mouse, direct, bot;
	int           pods, next_spawn, start_row, start_sector, flash, shake;
	struct axis   wx, wy;
	float         cam, fx, fy;
	unsigned      seed;
	struct input  prev;
	struct player p;
	struct enemy  enemy[MAX_ENEMY];
	struct bullet bullet[MAX_BULLET];
	struct part   part[MAX_PART];
	struct item   item[MAX_ITEM];
	float         bx[MAX_BUDDY], by[MAX_BUDDY];
} g;

static struct level lv;

static const SDL_Color WHITE  = { 236, 240, 244, 255 };
static const SDL_Color GREY   = { 120, 128, 144, 255 };
static const SDL_Color CYAN   = {  96, 220, 240, 255 };
static const SDL_Color MARK   = {  96, 220, 240, 170 };
static const SDL_Color YELLOW = { 250, 226,  90, 255 };
static const SDL_Color ORANGE = { 240, 140,  40, 255 };
static const SDL_Color RED    = { 230,  60,  70, 255 };
static const SDL_Color GREEN  = {  90, 200, 110, 255 };
static const SDL_Color DARK   = {   8,  10,  16, 255 };
static const SDL_Color SHADOW = {   0,   0,   0,  90 };

/* hit_by is the level a shot must be on to damage it, and the level of
 * its own shots.  Towers and pods stand on a block, out of the tank's
 * reach.  gun is the size of the barrel that tracks the player. */
static const struct etype_info {
	int   hp, score, radius, sprite, gun, hit_by, flies, boss, shots;
	float shot_speed, drive;
} et[] = {
	[E_TURRET]  = {  3,  100,  7, SPR_TURRET,  1, L_AIR,    0, 0, 1, 1.6f, 0 },
	[E_CRAWLER] = {  2,  150,  7, SPR_CRAWLER, 0, L_GROUND, 0, 0, 0, 0,    0 },
	[E_DRONE]   = {  2,  100,  7, SPR_DRONE,   0, L_AIR,    1, 0, 1, 1.8f, 0 },
	[E_DART]    = {  1,  150,  7, SPR_DART,    0, L_AIR,    1, 0, 0, 0,    0 },
	[E_POD]     = { 12, 1000, 13, SPR_POD,     2, L_AIR,    0, 1, 3, 1.8f, 0 },
	[E_CORE]    = { 30, 5000, 20, SPR_CORE,    0, L_GROUND, 0, 1, 8, 1.5f, 0 },
	[E_ROVER]   = {  3,  200,  7, SPR_ROVER,   0, L_GROUND, 0, 0, 1, 1.6f, 0.4f },
	[E_GUNSHIP] = {  8,  400,  9, SPR_GUNSHIP, 0, L_AIR,    1, 0, 3, 1.8f, 0 },
	[E_HEAVY]   = {  8,  400,  9, SPR_HEAVY,   0, L_GROUND, 0, 0, 2, 1.6f, 0.22f },
};

static const struct {
	const SDL_Color *tint;
	char             letter;
} item_look[IT_MAX] = {
	[IT_GUN]     = { &ORANGE, 'G' },
	[IT_SHIELD]  = { &CYAN,   'S' },
	[IT_SPEED]   = { &GREEN,  'F' },
	[IT_BUDDY]   = { &YELLOW, 'C' },
	[IT_MISSILE] = { &RED,    'H' },
	[IT_BOMB]    = { &WHITE,  'B' },
};

static int rnd(int n)
{
	g.seed ^= g.seed << 13;
	g.seed ^= g.seed >> 17;
	g.seed ^= g.seed << 5;

	return g.seed % n;
}

static float frand(void)
{
	return (rnd(2001) - 1000) / 1000.0f;
}

static int beyond(void)
{
	return g.sector >= SECTORS;
}

static int lap(void)
{
	return g.sector - SECTORS;
}

static int last_sector(void)
{
	return g.sector == SECTORS - 1;
}

/* Frames into the fusion scene. */
static int fuse_t(void)
{
	return FUSE_FRAMES - g.timer;
}

/* What the difficulty follows; it stops rising a few laps into the beyond. */
static const struct boss *boss(void)
{
	return level_boss(g.sector);
}

static int rank(void)
{
	return SDL_min(g.sector, 12);
}

/* Only the boss is left to spawn and no enemy is alive. */
static int boss_next(void)
{
	if (g.next_spawn >= lv.nspawn || !et[lv.spawn[g.next_spawn].type].boss)
		return 0;

	for (int i = 0; i < MAX_ENEMY; i++) {
		if (g.enemy[i].alive)
			return 0;
	}

	return 1;
}

static float scroll_pace(void)
{
	if (beyond())
		return SDL_min(1.2f, 0.75f + 0.15f * lap());

	return boss_next() ? SHIP_HURRY : SHIP_SCROLL;
}

/* The fused craft is on both levels at once. */
static int player_level(void)
{
	if (g.p.fused)
		return L_GROUND | L_AIR;

	return g.p.ship ? L_AIR : L_GROUND;
}

static int flies(const struct enemy *e)
{
	return et[e->type].flies || e->lifted;
}

static int level_of(const struct enemy *e)
{
	return e->lifted ? L_AIR : et[e->type].hit_by;
}

/* Angles run clockwise from straight up, matching SDL sprite rotation. */
static float player_angle(void)
{
	return g.p.ship ? 0 : g.p.dir * (float)M_PI / 4;
}

static float aim_at(const struct enemy *e)
{
	return atan2f(g.p.x - e->x, e->y - g.p.y);
}

static int onscreen(const struct enemy *e)
{
	return e->y > g.cam + 12 && e->y < g.cam + SCREEN_H;
}

static int shielded(const struct enemy *e)
{
	return e->type == E_CORE && g.pods > 0;
}

static int tile_at(float x, float y)
{
	int c = (int)x / TILE, r = (int)y / TILE;

	if (x < 0 || y < 0 || c >= MAP_W || r >= MAP_H)
		return T_FLOOR;

	return lv.tile[r][c];
}

static int box_hits(float x, float y, int t)
{
	return tile_at(x - 6, y - 6) == t || tile_at(x + 5, y - 6) == t ||
	       tile_at(x - 6, y + 5) == t || tile_at(x + 5, y + 5) == t;
}

static int ground_blocked(float x, float y)
{
	return box_hits(x, y, T_WALL) || box_hits(x, y, T_VOID);
}

static int row_has(float y, int t)
{
	int r = (int)y / TILE;

	return y >= 0 && r < MAP_H && memchr(lv.tile[r], t, MAP_W);
}

/* The ship flies at a steady pace and halts in front of a roof. */
static void ship_scroll(struct player *p)
{
	float pace = scroll_pace();
	float halt = g.cam + HALT_Y;
	float bottom = g.cam - pace + SCREEN_H - 8;

	if (halt < p->y && row_has(halt, T_ROOF))
		return;
	if (p->y > bottom) {
		if (box_hits(p->x, bottom, T_ROOF))
			return;
		p->y = bottom;
	}
	g.cam -= pace;
}

static struct part *emit(float x, float y, float vx, float vy, int life, SDL_Color c)
{
	for (int i = 0; i < MAX_PART; i++) {
		struct part *q = &g.part[i];

		if (q->life > 0)
			continue;
		*q = (struct part){ life, x, y, vx, vy, c, 0 };
		return q;
	}

	return NULL;
}

static void spark(float x, float y, int n, float speed, SDL_Color c)
{
	while (n-- > 0)
		emit(x, y, frand() * speed, frand() * speed, 15 + rnd(25), c);
}

/* A fireball of the given radius where the sparks fly from, and a jolt. */
static void explode(float x, float y, int size)
{
	struct part *q = emit(x, y, 0, 0, BLAST_LIFE, ORANGE);

	if (q)
		q->blast = 4 + size * 0.3f;
	g.shake = SDL_max(g.shake, SDL_min(size / 6, MAX_SHAKE));

	spark(x, y, size, 2.0f, ORANGE);
	spark(x, y, size / 2, 1.2f, YELLOW);
	spark(x, y, size / 3, 0.6f, WHITE);
	sfx_play(SFX_BOOM);
}

static struct bullet *add_bullet(float x, float y, float ang, float speed, int friendly, int level)
{
	for (int i = 0; i < MAX_BULLET; i++) {
		struct bullet *b = &g.bullet[i];

		if (b->alive)
			continue;
		*b = (struct bullet){ 1, friendly, level, x, y, sinf(ang) * speed, -cosf(ang) * speed, 0 };
		return b;
	}

	return NULL;
}

static void drop_item(float x, float y, int rise)
{
	const struct player *p = &g.p;
	static const int odds[] = {
		IT_GUN, IT_GUN, IT_SHIELD, IT_SHIELD, IT_SPEED, IT_SPEED, IT_BUDDY, IT_MISSILE, IT_MISSILE, IT_BOMB,
	};
	int kind = odds[rnd(SDL_arraysize(odds))];

	if ((kind == IT_GUN && p->weapon >= MAX_WEAPON) || (kind == IT_SPEED && p->speed >= MAX_SPEED) ||
	    (kind == IT_BUDDY && p->buddies >= MAX_BUDDY))
		kind = IT_SHIELD;

	for (int i = 0; i < MAX_ITEM; i++) {
		struct item *it = &g.item[i];

		if (it->alive)
			continue;
		*it = (struct item){ 1, kind, rise, x, y };
		return;
	}
}

static void spawn_enemy(const struct spawn *s)
{
	for (int i = 0; i < MAX_ENEMY; i++) {
		struct enemy *e = &g.enemy[i];

		if (e->alive)
			continue;

		*e = (struct enemy){ .alive = 1, .type = s->type, .delay = s->delay,
				     .x = s->x, .y = s->y };
		e->hp = et[s->type].hp;
		if (et[s->type].boss)
			e->hp += e->hp * rank() / 6;
		e->full = e->hp;
		if (!et[s->type].flies)
			e->t = rnd(60);
		if (s->type == E_CRAWLER)
			e->vx = rnd(2) ? 0.5f : -0.5f;
		if (s->type == E_DRONE)
			e->vx = s->x;
		if (s->type == E_POD)
			g.pods++;
		return;
	}
}

static void skip_spawns(void)
{
	g.next_spawn = 0;
	while (g.next_spawn < lv.nspawn && lv.spawn[g.next_spawn].y > g.cam + SCREEN_H + 24)
		g.next_spawn++;
}

static void start_sector(int sector)
{
	struct player *p = &g.p;

	g.sector = sector;
	g.mouse = 0;
	level_generate(&lv, sector);

	memset(g.enemy, 0, sizeof(g.enemy));
	memset(g.bullet, 0, sizeof(g.bullet));
	memset(g.item, 0, sizeof(g.item));
	g.pods = 0;
	g.cam = g.start_row ? g.start_row * TILE : START_CAM;
	g.start_row = 0;
	g.state = ST_PLAY;
	skip_spawns();

	p->x = SCREEN_W / 2;
	p->y = g.cam + SCREEN_H - 40;
	p->ship = p->fused;
	p->dir = 0;
	p->hp = MAX_HP;
	p->invuln = 90;
	p->dead = 0;
}

static void new_game(void)
{
	g.score = 0;
	g.lives = 3;
	g.p.missiles = 0;
	g.p.weapon = 1;
	g.p.speed = 0;
	g.p.buddies = 0;
	g.p.fused = g.start_sector >= SECTORS;
	sfx_music("sector");
	start_sector(g.start_sector);
}

/* Each lap of the beyond is worth more. */
static void add_score(int n)
{
	if (beyond())
		n *= 2 + lap();

	if ((g.score + n) / LIFE_SCORE > g.score / LIFE_SCORE) {
		g.lives++;
		sfx_play(SFX_PICKUP);
	}
	g.score += n;
	g.hiscore = SDL_max(g.hiscore, g.score);
}

static void game_over(void)
{
	g.state = ST_OVER;
	g.timer = 60;
}

static void hurt_player(void)
{
	struct player *p = &g.p;

	sfx_play(SFX_HURT);
	p->invuln = 60;
	g.shake = SDL_max(g.shake, 6);
	if (--p->hp > 0)
		return;

	explode(p->x, p->y, 40);
	p->dead = 90;
	g.lives--;
}

static void damage_enemy(struct enemy *e)
{
	if (shielded(e)) {
		spark(e->x, e->y + 16, 2, 1.5f, CYAN);
		return;
	}

	e->flash = 4;
	if (--e->hp > 0) {
		sfx_play(SFX_HIT);
		/* the last core takes off when half of it is gone */
		if (e->type == E_CORE && last_sector() && !e->lifted && e->hp <= e->full / 2) {
			e->lifted = 1;
			g.flash = 8;
			explode(e->x, e->y, 30);
		}
		return;
	}

	e->alive = 0;
	add_score(et[e->type].score);
	explode(e->x, e->y, et[e->type].radius * 4);

	if (e->type == E_POD) {
		g.pods--;
	} else if (e->type == E_CORE) {
		memset(g.bullet, 0, sizeof(g.bullet));
		if (beyond()) {
			/* the lap goes on from here, see next_lap() */
			g.flash = 8;
			drop_item(e->x - 12, e->y, 0);
			drop_item(e->x + 12, e->y, 0);
			return;
		}
		g.state = ST_CLEAR;
		g.timer = 240;
		if (last_sector()) {
			g.state = ST_FUSE;
			g.timer = FUSE_FRAMES;
			g.fx = e->x;
			g.fy = e->y;
		}
	} else if (e->type == E_TURRET) {
		/* a supply capsule may rise out of the wrecked tower's block */
		if (rnd(2) == 0)
			drop_item(e->x, e->y, RISE_FRAMES);
	} else if (rnd(4) == 0) {
		drop_item(e->x, e->y, 0);
	}
}

static void player_fire(void)
{
	struct player *p = &g.p;
	float ang = player_angle();
	float speed = p->ship ? 5.0f : 3.5f;
	float fx = sinf(ang), fy = -cosf(ang);
	float x = p->x + fx * 8, y = p->y + fy * 8;
	int level = player_level();

	if (p->weapon == 1) {
		add_bullet(x, y, ang, speed, 1, level);
	} else {
		/* twin shots, offset sideways from the barrel */
		add_bullet(x - fy * 4, y + fx * 4, ang, speed, 1, level);
		add_bullet(x + fy * 4, y - fx * 4, ang, speed, 1, level);
	}
	if (p->weapon == 3) {
		add_bullet(x, y, ang - 0.25f, speed, 1, level);
		add_bullet(x, y, ang + 0.25f, speed, 1, level);
	}

	if (p->fused) {
		add_bullet(p->x - 8, p->y, ang, speed, 1, level);
		add_bullet(p->x + 8, p->y, ang, speed, 1, level);
	}

	for (int i = 0; i < p->buddies; i++)
		add_bullet(g.bx[i], g.by[i], ang, speed, 1, level);

	p->reload = p->fused ? 7 : p->ship ? 8 : 12;
	sfx_play(SFX_SHOT);
}

/* The nearest of eight directions to a heading. */
static void octant(float x, float y, int *dx, int *dy)
{
	/* tan 22.5: an axis counts when within an octant of the heading */
	*dx = (fabsf(x) > 0.4142f * fabsf(y)) * (x > 0 ? 1 : -1);
	*dy = (fabsf(y) > 0.4142f * fabsf(x)) * (y > 0 ? 1 : -1);
}

static void update_player(const struct input *in, int morph)
{
	static const int facing[3][3] = { { 7, 0, 1 }, { 6, -1, 2 }, { 5, 4, 3 } };
	struct player *p = &g.p;
	int dx = in->right - in->left, dy = in->down - in->up;
	float speed = (p->ship ? 2.5f : 1.6f) * (1 + 0.2f * p->speed);
	int block = p->ship ? T_ROOF : T_WALL;
	float nx, ny, sy, vx = 0, vy = 0;

	if (p->dead) {
		if (--p->dead > 0)
			return;
		if (g.lives < 0) {
			game_over();
			return;
		}
		p->hp = MAX_HP;
		p->invuln = 120;
		if (p->weapon > 1)
			p->weapon--;
		if (p->buddies)
			p->buddies--;
		p->speed = 0;
	}

	if (p->invuln)
		p->invuln--;
	if (p->morph_cd)
		p->morph_cd--;
	if (p->reload)
		p->reload--;
	if (p->missile_cd)
		p->missile_cd--;

	/* the mouse moves a marker on the screen and the craft heads for it */
	if (dx || dy) {
		g.mouse = 0;
	} else if (!g.mouse && (in->mx || in->my)) {
		g.mouse = 1;
		g.mx = p->x;
		g.my = p->y - g.cam;
	}

	if (g.mouse) {
		float tx, ty, len;

		g.mx = SDL_clamp(g.mx + in->mx, 8, SCREEN_W - 8);
		g.my = SDL_clamp(g.my + in->my, 18, SCREEN_H - 8);
		tx = g.mx - p->x;
		ty = g.my - (p->y - g.cam);
		len = hypotf(tx, ty);

		/* direct steering keeps the marker on a short leash and out of sight */
		if (g.direct && len > MARK_REACH) {
			float k = MARK_REACH / len;

			tx *= k;
			ty *= k;
			len = MARK_REACH;
			g.mx = p->x + tx;
			g.my = p->y - g.cam + ty;
		}

		if (p->ship && len > 0) {
			vx = tx / len * SDL_min(len, speed);
			vy = ty / len * SDL_min(len, speed);
		} else if (!p->ship && len > MARK_NEAR) {
			octant(tx, ty, &dx, &dy);
		}
	}

	if (dx && dy)
		speed *= 0.7071f;
	if (dx || dy) {
		vx = dx * speed;
		vy = dy * speed;
	}

	nx = SDL_clamp(p->x + vx, 8, SCREEN_W - 8);
	if (!box_hits(nx, p->y, block))
		p->x = nx;
	ny = SDL_clamp(p->y + vy, g.cam + 18, g.cam + SCREEN_H - 8);
	if (!box_hits(p->x, ny, block))
		p->y = ny;

	if (!p->ship && (dx || dy)) {
		int d = facing[dy + 1][dx + 1];
		int part = (p->dir & 1) && (d == (p->dir + 1) % 8 || d == (p->dir + 7) % 8);

		/* keep a diagonal facing while the two directions are let go one by one */
		if (d & 1) {
			p->dir = d;
			p->diag = DIAG_FRAMES;
		} else if (!part || p->diag-- <= 0) {
			p->dir = d;
		}
	}

	sy = p->y - g.cam;
	if (p->ship)
		ship_scroll(p);
	else if (sy < SCROLL_Y)
		g.cam -= (SCROLL_Y - sy) * 0.08f;
	if (g.cam < 0)
		g.cam = 0;

	if (morph && !p->morph_cd && !p->fused) {
		if (box_hits(p->x, p->y, p->ship ? T_WALL : T_ROOF)) {
			sfx_play(SFX_DENY);
		} else {
			p->ship = !p->ship;
			p->morph_cd = 20;
			spark(p->x, p->y, 14, 1.5f, CYAN);
			sfx_play(SFX_MORPH);
		}
	}

	for (int i = 0; i < p->buddies; i++) {
		g.bx[i] += (p->x + (i ? 14 : -14) - g.bx[i]) * 0.2f;
		g.by[i] += (p->y + 8 - g.by[i]) * 0.2f;
	}

	if (in->fire && p->missiles && !p->missile_cd) {
		struct bullet *b = add_bullet(p->x, p->y, player_angle(), MISSILE_SPEED, 1, L_GROUND | L_AIR);

		if (b) {
			b->homing = MISSILE_LIFE;
			p->missiles--;
			p->missile_cd = 30;
		}
	}

	if (in->fire && !p->reload)
		player_fire();
}

/* Fires n shots fanned out around ang, gap apart. */
static void enemy_fire(const struct enemy *e, float ang, int n, float gap)
{
	const struct etype_info *t = &et[e->type];
	float speed = t->shot_speed * (0.85f + 0.04f * rank());

	for (int i = 0; i < n; i++)
		add_bullet(e->x, e->y, ang + (i - (n - 1) / 2.0f) * gap, speed, 0, level_of(e));
}

static int count(int type)
{
	int n = 0;

	for (int i = 0; i < MAX_ENEMY; i++)
		n += g.enemy[i].alive && g.enemy[i].type == type;

	return n;
}

static void core_move(struct enemy *e)
{
	int dive = e->t % CORE_CYCLE - 200;

	e->x = SCREEN_W / 2 + 46 * sinf(e->t * 0.02f);
	e->y = CORE_Y;

	switch (boss()->move) {
	case MOVE_EIGHT:
		e->y += 16 * sinf(e->t * 0.04f);
		break;
	case MOVE_SWOOP:
		/* every five seconds it comes down the deck and goes back */
		if (dive > 0)
			e->y += 80 * powf(sinf(dive * (float)M_PI / 100), 2);
		break;
	}
}

static void core_fire(struct enemy *e, float aim)
{
	int ring = et[E_CORE].shots + rank() / 2;
	int every = 100 - rank() * 3;

	switch (boss()->fire) {
	case FIRE_RING:
		if (e->t % (90 - rank() * 3) == 0)
			enemy_fire(e, e->t, ring, 2 * (float)M_PI / ring);
		break;
	case FIRE_SPIRAL:
		/* two arms that turn as they fire */
		if (e->t % (9 - rank() / 3) == 0)
			enemy_fire(e, e->t * SPIRAL_TURN, 2, (float)M_PI);
		break;
	case FIRE_FAN:
		/* two volleys at the player, close together */
		if (e->t % every == 0 || e->t % every == 14)
			enemy_fire(e, aim, 5, 0.2f);
		break;
	}

	if (boss()->brood && e->t % CORE_CYCLE == CORE_CYCLE / 2 && count(E_ROVER) < 3) {
		spawn_enemy(&(struct spawn){ E_ROVER, e->x, e->y + 28, 0 });
		spark(e->x, e->y + 28, 10, 1.2f, CYAN);
	}
}

static void update_enemies(void)
{
	struct player *p = &g.p;
	int period = 150 - rank() * 10;

	while (g.next_spawn < lv.nspawn && lv.spawn[g.next_spawn].y >= g.cam - 24)
		spawn_enemy(&lv.spawn[g.next_spawn++]);

	for (int i = 0; i < MAX_ENEMY; i++) {
		struct enemy *e = &g.enemy[i];
		const struct etype_info *t = &et[e->type];
		float aim;
		int vis, r;

		if (!e->alive)
			continue;
		if (e->delay > 0) {
			if (--e->delay > 0)
				continue;
			e->y = g.cam - 12;
		}

		e->t++;
		if (e->flash)
			e->flash--;

		vis = onscreen(e);
		aim = aim_at(e);

		switch (e->type) {
		case E_TURRET:
			if (vis && e->t % period == 0)
				enemy_fire(e, aim, t->shots, 0);
			break;

		case E_CRAWLER:
			e->x += e->vx;
			if (e->x < 8 || e->x > SCREEN_W - 8 || ground_blocked(e->x, e->y)) {
				e->vx = -e->vx;
				e->x += e->vx * 2;
			}
			break;

		case E_DRONE:
			e->y += 0.5f;
			e->x = e->vx + 40 * sinf(e->t * 0.05f);
			if (vis && e->t % 100 == 0)
				enemy_fire(e, aim, t->shots, 0);
			break;

		case E_DART:
			e->y += 1.4f;
			e->x += p->x > e->x ? 0.5f : -0.5f;
			break;

		case E_GUNSHIP:
			/* drops in, hovers ahead of the ship, then leaves */
			e->y += e->t > 600 || e->y < g.cam + HOVER_Y ? 0.8f : -scroll_pace();
			e->x += SDL_clamp(p->x - e->x, -0.3f, 0.3f);
			if (vis && e->t % 90 == 0)
				enemy_fire(e, aim, t->shots, 0.3f);
			break;

		case E_POD:
			if (vis && e->t % 90 == 0)
				enemy_fire(e, aim, g.sector < 2 ? 1 : t->shots, 0.3f);
			break;

		case E_CORE:
			core_move(e);
			if (!vis || shielded(e))
				break;
			if (e->lifted && e->t % 50 == 0)
				enemy_fire(e, aim, 5, 0.25f);
			core_fire(e, aim);
			break;

		case E_ROVER:
		case E_HEAVY:
			if (!vis)
				break;
			if (fabsf(p->x - e->x) > 2) {
				float nx = e->x + (p->x > e->x ? t->drive : -t->drive);

				if (!ground_blocked(nx, e->y))
					e->x = nx;
			}
			if (fabsf(p->y - e->y) > 2) {
				float ny = e->y + (p->y > e->y ? t->drive : -t->drive);

				if (!ground_blocked(e->x, ny))
					e->y = ny;
			}
			if (e->t % period == 0)
				enemy_fire(e, aim, t->shots, 0.24f);
			break;
		}

		if (e->y > g.cam + SCREEN_H + 24) {
			e->alive = 0;
			continue;
		}

		r = t->radius + 5;
		if (!p->dead && !p->invuln && flies(e) == p->ship &&
		    fabsf(p->x - e->x) < r && fabsf(p->y - e->y) < r) {
			hurt_player();
			if (!t->boss)
				damage_enemy(e);
		}
	}
}

/* Turns a missile towards the nearest enemy on either level, false when it burns out. */
static int steer(struct bullet *b)
{
	const struct enemy *best = NULL;
	float bd = 1e9f, len;

	if (--b->homing <= 0)
		return 0;

	for (int n = 0; n < MAX_ENEMY; n++) {
		const struct enemy *e = &g.enemy[n];
		float d = fabsf(e->x - b->x) + fabsf(e->y - b->y);

		if (!e->alive || e->delay || e->y < g.cam || e->y > g.cam + SCREEN_H)
			continue;
		if (shielded(e))
			continue;
		if (d < bd) {
			bd = d;
			best = e;
		}
	}

	if (best) {
		float dx = best->x - b->x, dy = best->y - b->y;

		len = sqrtf(dx * dx + dy * dy) + 0.01f;
		b->vx += dx / len * 0.45f;
		b->vy += dy / len * 0.45f;
	}
	len = sqrtf(b->vx * b->vx + b->vy * b->vy) + 0.01f;
	b->vx *= MISSILE_SPEED / len;
	b->vy *= MISSILE_SPEED / len;

	if (g.frame & 1)
		spark(b->x, b->y, 1, 0.2f, GREY);

	return 1;
}

/* Hits everything on screen, on both levels, and wipes enemy shots. */
static void smart_bomb(void)
{
	for (int n = 0; n < MAX_ENEMY; n++) {
		struct enemy *e = &g.enemy[n];

		if (e->delay || e->y < g.cam - 16 || e->y > g.cam + SCREEN_H)
			continue;
		for (int k = 0; k < 4 && e->alive; k++)
			damage_enemy(e);
	}
	for (int i = 0; i < MAX_BULLET; i++) {
		if (!g.bullet[i].friendly)
			g.bullet[i].alive = 0;
	}

	g.flash = 20;
	sfx_play(SFX_BOOM);
}

static void update_bullets(void)
{
	struct player *p = &g.p;

	for (int i = 0; i < MAX_BULLET; i++) {
		struct bullet *b = &g.bullet[i];

		if (!b->alive)
			continue;

		if (b->homing && !steer(b)) {
			b->alive = 0;
			spark(b->x, b->y, 4, 1.0f, GREY);
			continue;
		}

		b->x += b->vx;
		b->y += b->vy;
		if (b->x < -4 || b->x > SCREEN_W + 4 || b->y < g.cam - 4 || b->y > g.cam + SCREEN_H + 4) {
			b->alive = 0;
			continue;
		}

		if (!(b->level & L_AIR) && tile_at(b->x, b->y) == T_WALL) {
			b->alive = 0;
			spark(b->x, b->y, 2, 0.8f, b->friendly ? GREY : ORANGE);
			continue;
		}

		/* shots only hit what is on their own level */
		if (!b->friendly) {
			if ((b->level & player_level()) && fabsf(b->x - p->x) < 5 && fabsf(b->y - p->y) < 5 &&
			    !p->dead && !p->invuln) {
				b->alive = 0;
				hurt_player();
			}
			continue;
		}

		for (int n = 0; n < MAX_ENEMY; n++) {
			struct enemy *e = &g.enemy[n];
			int r = et[e->type].radius + 2;

			if (!e->alive || e->delay || !(b->level & level_of(e)))
				continue;
			if (fabsf(b->x - e->x) > r || fabsf(b->y - e->y) > r)
				continue;

			b->alive = 0;
			damage_enemy(e);
			break;
		}
	}
}

static void update_items(void)
{
	struct player *p = &g.p;

	for (int i = 0; i < MAX_ITEM; i++) {
		struct item *it = &g.item[i];

		if (!it->alive)
			continue;
		if (it->y > g.cam + SCREEN_H + 8) {
			it->alive = 0;
			continue;
		}
		if (it->rise) {
			it->rise--;
			continue;
		}
		if (p->dead || fabsf(it->x - p->x) > 11 || fabsf(it->y - p->y) > 11)
			continue;

		it->alive = 0;
		add_score(50);
		switch (it->kind) {
		case IT_GUN:
			p->weapon = SDL_min(MAX_WEAPON, p->weapon + 1);
			break;
		case IT_SPEED:
			p->speed = SDL_min(MAX_SPEED, p->speed + 1);
			break;
		case IT_MISSILE:
			p->missiles = SDL_min(30, p->missiles + 10);
			break;
		case IT_BOMB:
			smart_bomb();
			break;
		case IT_BUDDY:
			if (p->buddies < MAX_BUDDY) {
				g.bx[p->buddies] = p->x;
				g.by[p->buddies] = p->y;
				p->buddies++;
			}
			break;
		default:
			p->hp = SDL_min(MAX_HP, p->hp + 2);
			break;
		}
		sfx_play(SFX_PICKUP);
	}
}

static void update_particles(void)
{
	for (int i = 0; i < MAX_PART; i++) {
		struct part *q = &g.part[i];

		if (q->life <= 0)
			continue;
		q->life--;
		q->x += q->vx;
		q->y += q->vy;
	}
}

/* True on the second of two quick reversals, e.g. left-right-left. */
static int wiggle(struct axis *a, int v)
{
	int hit = 0;

	if (!v)
		return 0;

	if (v == -a->sign && g.frame - a->seen <= WIGGLE_FRAMES) {
		hit = g.frame - a->flip <= WIGGLE_FRAMES;
		a->flip = hit ? -1000 : g.frame;
	}
	a->sign = v;
	a->seen = g.frame;

	return hit;
}

/* The camera wraps to the bottom of a fresh stretch, taking everything along. */
static void next_lap(void)
{
	float shift;

	level_generate(&lv, ++g.sector);
	shift = START_CAM - g.cam;
	g.cam += shift;
	g.p.y += shift;
	for (int i = 0; i < MAX_ENEMY; i++)
		g.enemy[i].y += shift;
	for (int i = 0; i < MAX_BULLET; i++)
		g.bullet[i].y += shift;
	for (int i = 0; i < MAX_ITEM; i++)
		g.item[i].y += shift;
	for (int i = 0; i < MAX_PART; i++)
		g.part[i].y += shift;
	for (int i = 0; i < MAX_BUDDY; i++)
		g.by[i] += shift;
	skip_spawns();
}

/* The wreck burns, the two shapes pull together, and the fused craft leaves. */
static void update_fuse(void)
{
	struct player *p = &g.p;
	int t = fuse_t();

	g.timer--;

	if (t == 0) {
		p->dead = p->invuln = 0;
		p->hp = MAX_HP;
		g.lives = SDL_max(g.lives, 0);
	}

	if (t < FUSE_BURN) {
		if (t % 12 == 0)
			explode(g.fx + frand() * 30, g.fy + frand() * 30, 24);
	} else if (t < FUSE_AT) {
		float a = frand() * (float)M_PI;

		p->x += (SCREEN_W / 2 - p->x) * 0.04f;
		p->y += (g.cam + 150 - p->y) * 0.04f;
		emit(p->x + cosf(a) * 44, p->y + sinf(a) * 44, -cosf(a) * 2, -sinf(a) * 2, 20, CYAN);
	} else if (t == FUSE_AT) {
		p->fused = p->ship = 1;
		g.flash = 25;
		spark(p->x, p->y, 60, 3.0f, CYAN);
		spark(p->x, p->y, 30, 1.5f, WHITE);
		sfx_play(SFX_MORPH);
		sfx_play(SFX_BOOM);
	}

	if (g.timer <= 0)
		start_sector(SECTORS);
}

static void show_title(void)
{
	g.sector = 0;
	level_generate(&lv, 0);
	g.cam = START_CAM;
	g.state = ST_TITLE;
	sfx_music("title");
}

void game_init(int start_row, int start_sector, int hiscore)
{
	memset(&g, 0, sizeof(g));
	g.hiscore = hiscore;
	g.seed = 0x2545f491u;
	g.start_row = start_row;
	g.start_sector = SDL_clamp(start_sector, 0, 2 * SECTORS - 1);
	g.wx.flip = g.wy.flip = -1000;
	show_title();
}

int game_hiscore(void)
{
	return g.hiscore;
}

void game_mouse(int direct)
{
	g.direct = direct;
}

int game_escape(void)
{
	if (g.state == ST_TITLE)
		return 1;
	show_title();

	return 0;
}

void game_update(const struct input *in)
{
	int morph = in->morph && !g.prev.morph;
	int start = (in->fire && !g.prev.fire) || (in->start && !g.prev.start);

	g.prev = *in;
	g.frame++;
	if (g.flash)
		g.flash--;
	if (g.shake)
		g.shake--;

	/* one-button joystick: a quick wiggle on either axis morphs */
	if (!g.bot) {
		morph |= wiggle(&g.wx, in->right - in->left);
		morph |= wiggle(&g.wy, in->down - in->up);
	}

	switch (g.state) {
	case ST_TITLE:
		g.cam -= 0.4f;
		if (g.cam < 0)
			g.cam = START_CAM;
		if (start)
			new_game();
		break;

	case ST_PLAY:
		update_player(in, morph);
		update_enemies();
		update_bullets();
		update_items();
		update_particles();
		if (beyond() && g.cam <= 0 && !count(E_CORE))
			next_lap();
		break;

	case ST_CLEAR:
		update_particles();
		if (--g.timer == 0)
			start_sector(g.sector + 1);
		break;

	case ST_FUSE:
		update_fuse();
		update_particles();
		break;

	case ST_OVER:
		update_particles();
		if (g.timer) {
			g.timer--;
		} else if (start) {
			show_title();
		}
		break;
	}
}

/* The player of --demo.  It takes the shape that has something to shoot,
 * lines up with the nearest target, steps aside from shots, picks up
 * capsules, and changes shape when the way ahead is shut. */
void game_autopilot(struct input *in)
{
	static float last;
	static int stuck, cool, idle, seen, side = 1;
	const struct player *p = &g.p;
	const struct enemy *aim = NULL;
	const struct item *item = NULL;
	float sy = p->y - g.cam, tx = SCREEN_W / 2, ty = SCREEN_H - 60, best = 1e9f;
	int other = 0, want = 0, shut = 0, dx, dy;

	memset(in, 0, sizeof(*in));
	g.bot = 1;
	if (g.state != ST_PLAY) {
		in->fire = g.frame & 1;
		return;
	}

	stuck = g.cam == last ? stuck + 1 : 0;
	last = g.cam;
	idle = g.score == seen ? idle + 1 : 0;
	seen = g.score;
	if (cool)
		cool--;
	in->fire = 1;

	for (int i = 0; i < MAX_ENEMY; i++) {
		const struct enemy *e = &g.enemy[i];
		float y = e->y - g.cam, d = fabsf(e->x - p->x) + fabsf(y - sy) * 0.5f;

		if (!e->alive || e->delay || shielded(e) || !onscreen(e))
			continue;
		if (!(level_of(e) & player_level())) {
			other = 1;
		} else if (d < best) {
			best = d;
			aim = e;
			/* the ship fires up from below */
			tx = e->x;
			ty = SDL_max(y + 70, SCREEN_H - 80);
		}
	}

	for (int i = 0; i < MAX_ITEM && !aim; i++) {
		if (g.item[i].alive && !g.item[i].rise)
			item = &g.item[i];
	}
	if (item) {
		tx = item->x;
		ty = item->y - g.cam;
	} else if (!aim && !p->ship) {
		/* nothing to do here: the tank drives on, the ship lets the scroll carry it */
		ty = 0;
	}

	dx = fabsf(tx - p->x) > MARK_NEAR ? (tx > p->x ? 1 : -1) : 0;
	dy = fabsf(ty - sy) > MARK_NEAR ? (ty > sy ? 1 : -1) : 0;

	/* the tank fires the way it faces, so it drives at its target and stops short */
	if (aim && !p->ship) {
		dx = dy = 0;
		if (hypotf(aim->x - p->x, aim->y - p->y) > et[aim->type].radius + 24)
			octant(aim->x - p->x, aim->y - p->y, &dx, &dy);
	}

	for (int i = 0; i < MAX_BULLET; i++) {
		const struct bullet *b = &g.bullet[i];
		float bx = b->x - p->x, by = b->y - p->y;

		if (!b->alive || b->friendly || !(b->level & player_level()))
			continue;
		/* on its way here and close: step off its line, away from the edges */
		if (fabsf(bx) < 14 && fabsf(by) < 44 && bx * b->vx + by * b->vy < 0) {
			if (p->x < 24)
				dx = 1;
			else if (p->x > SCREEN_W - 24)
				dx = -1;
			else
				dx = bx > 0 ? -1 : 1;
		}
	}

	/* a tank that drives into a wall slides along it, to and fro */
	if (!p->ship && dy < 0 && box_hits(p->x, p->y - 3, T_WALL)) {
		if (p->x < 12 || p->x > SCREEN_W - 12 || box_hits(p->x + side * 3, p->y, T_WALL))
			side = -side;
		dx = side;
	}

	in->left = dx < 0;
	in->right = dx > 0;
	in->up = dy < 0;
	in->down = dy > 0;

	/* change shape for targets only the other one can hit, for the towers of
	 * a fortress, which are the ship's, and when getting nowhere */
	if ((!aim && other) || (g.pods && !p->ship))
		want = 1;
	if (idle > BOT_IDLE) {
		want = 1;
		idle = 0;
	}
	/* the way is shut: a roof halts the ship, a wall the tank */
	if (g.cam > 0 && stuck > (p->ship ? 40 : 240))
		want = shut = 1;
	if (!want || cool || p->fused)
		return;
	/* a halted ship first flies up to the roof, past any wall, and lands there */
	if (box_hits(p->x, p->y, p->ship ? T_WALL : T_ROOF) || (shut && p->ship && sy > HALT_Y + 22)) {
		in->up = 1;
		in->down = 0;
		return;
	}
	/* stand still for it, a step could end over a tile that refuses the change */
	in->left = in->right = in->up = in->down = 0;
	in->morph = 1;
	/* having taken off at a wall, stay up long enough to clear it */
	cool = shut && !p->ship ? 300 : 90;
	stuck = 0;
}

static void draw_stars(void)
{
	for (unsigned i = 1; i <= 70; i++) {
		unsigned h = i * 2654435761u;
		int depth = h >> 8 & 3;
		float y = fmodf((h >> 12 & 255) * SCREEN_H / 255.0f + g.frame * (0.2f + depth * 0.25f), SCREEN_H);

		gfx_rect(h % SCREEN_W, y, 0.6f + depth * 0.2f, 0.6f + depth * 0.2f,
			 (SDL_Color){ 200, 220, 255, 90 + depth * 50 });
	}
}

static void draw_tiles(int roof, float cam)
{
	static const SDL_Color tints[] = {
		{ 255, 255, 255, 255 }, { 222, 255, 240, 255 }, { 255, 238, 222, 255 },
		{ 240, 226, 255, 255 }, { 255, 250, 200, 255 }, { 205, 240, 255, 255 },
		{ 255, 214, 226, 255 }, { 226, 255, 210, 255 }, { 255, 255, 255, 255 },
	};
	/* never the colours of a shot */
	static const SDL_Color lamps[THEME_MAX][3] = {
		{ { 90, 255, 140, 0 }, { 80, 200, 255, 0 }, { 200, 255, 220, 0 } },
		{ { 120, 200, 255, 0 }, { 200, 230, 255, 0 }, { 90, 160, 255, 0 } },
	};
	SDL_Color tint = tints[g.sector % SDL_arraysize(tints)];
	int theme = last_sector() ? THEME_EMBER : THEME_STEEL;
	int r0 = (int)cam / TILE;
	int glow = 50 + (int)(12 * sinf(g.frame * 0.1f));

	for (int r = r0; r <= r0 + SCREEN_H / TILE && r < MAP_H; r++) {
		/* lines of light run up through the roof */
		float scan = SCAN_GAP - fmodf(r * TILE + g.frame * 0.5f, SCAN_GAP);

		for (int c = 0; c < MAP_W; c++) {
			int t = lv.tile[r][c];
			float x = c * TILE, y = r * TILE - cam;
			unsigned h = (r * 73856093u) ^ (c * 19349663u);

			if (t == T_VOID)
				continue;

			if (roof) {
				if (t != T_ROOF)
					continue;
				gfx_rect(x, y, TILE, TILE, (SDL_Color){ 40, 200, 220, glow });
				gfx_rect(x, y, TILE, 0.5f, (SDL_Color){ 130, 240, 255, 110 });
				gfx_rect(x, y, 0.5f, TILE, (SDL_Color){ 130, 240, 255, 110 });
				if (scan < TILE)
					gfx_rect(x, y + scan, TILE, 0.8f, (SDL_Color){ 190, 250, 255, 70 });
			} else if (t == T_WALL) {
				gfx_plate(PLATE_WALL, theme, x, y, tint);
			} else {
				gfx_plate(h % 11 == 0 ? PLATE_VENT : h % 5 == 0 ? PLATE_SEAM : PLATE_FLOOR, theme, x, y, tint);
				if (r > 0 && lv.tile[r - 1][c] == T_WALL)
					gfx_rect(x, y, TILE, 5, (SDL_Color){ 0, 0, 0, 90 });
				if (c > 0 && lv.tile[r][c - 1] == T_WALL)
					gfx_rect(x, y, 4, TILE, (SDL_Color){ 0, 0, 0, 70 });
				if (h % 13 == 3) {
					SDL_Color lamp = lamps[theme][(h >> 5) % 3];

					lamp.a = 150 + 100 * sinf(g.frame * 0.07f + h % 64);
					gfx_glow(x + 3.5f, y + 3.5f, 5, lamp);
					gfx_disc(x + 3.5f, y + 3.5f, 0.9f, lamp);
				}
			}
		}
	}
}

static void draw_enemies(int air, float cam)
{
	for (int i = 0; i < MAX_ENEMY; i++) {
		const struct enemy *e = &g.enemy[i];
		const struct etype_info *t = &et[e->type];
		float y = e->y - cam, aim = aim_at(e) * 180 / (float)M_PI, ang = 0;
		int mode = e->flash ? DRAW_FLASH : DRAW_NORMAL, spr = t->sprite;

		if (!e->alive || e->delay || flies(e) != air)
			continue;

		if (e->type == E_CRAWLER) {
			ang = e->vx > 0 ? 90 : -90;
			y += (e->t >> 3) & 1;
		} else if (t->drive) {
			ang = aim;
		}

		if (e->type == E_POD)
			spr = boss()->pod;
		if (e->type == E_CORE) {
			spr = boss()->core;
			/* the ones that fire a spiral turn with it */
			if (boss()->fire == FIRE_SPIRAL)
				ang = e->t * SPIRAL_TURN * 180 / (float)M_PI;
		}

		/* whatever the ship can hit is up high and casts a shadow */
		if (flies(e))
			gfx_tinted(spr, e->x + 4, y + 8, 1, SHADOW);
		else if (level_of(e) & L_AIR)
			gfx_tinted(spr, e->x + t->radius * 0.4f, y + t->radius * 0.7f, 1, SHADOW);
		gfx_sprite(spr, e->x, y, ang, 1, mode);
		if (t->gun)
			gfx_sprite(SPR_GUN, e->x, y, aim, t->gun, mode);

		if (shielded(e) && (g.frame & 4))
			gfx_rect(e->x - 26, y + 26, 52, 2, CYAN);
	}
}

static void draw_player(float cam)
{
	const struct player *p = &g.p;
	float y = p->y - cam;
	int ship = p->ship;

	if (p->dead || (p->invuln & 2))
		return;

	for (int i = 0; i < p->buddies; i++)
		gfx_sprite(SPR_BUDDY, g.bx[i], g.by[i] - cam, 0, 1, DRAW_NORMAL);

	/* the two shapes flicker faster and faster before they fuse */
	if (g.state == ST_FUSE && !p->fused && fuse_t() > FUSE_BURN)
		ship = fuse_t() * fuse_t() / 900 & 1;

	if (ship) {
		int id = p->fused ? SPR_FUSED : SPR_SHIP;

		float burn = 2.2f + ((g.frame >> 1) & 1) + 0.6f * ((g.frame >> 2) & 1);

		gfx_tinted(id, p->x + 4, y + 8, 1, SHADOW);
		for (int i = -1; i <= 1; i += 2) {
			gfx_glow(p->x + i * 1.2f, y + 8.5f, burn * 2.4f, ORANGE);
			gfx_disc(p->x + i * 1.2f, y + 7.6f + burn * 0.4f, burn * 0.35f, YELLOW);
		}
		gfx_sprite(id, p->x, y, 0, 1, DRAW_NORMAL);
	} else {
		gfx_sprite(SPR_TANK, p->x, y, player_angle() * 180 / (float)M_PI, 1, DRAW_NORMAL);
	}
}

static void draw_items(float cam, int blink)
{
	for (int i = 0; i < MAX_ITEM; i++) {
		const struct item *it = &g.item[i];
		float y = it->y - cam;
		float size = 1 - 0.8f * it->rise / RISE_FRAMES;
		SDL_Color c = *item_look[it->kind].tint;

		if (!it->alive)
			continue;
		if (blink && !it->rise)
			c = (SDL_Color){ (c.r + 255) / 2, (c.g + 255) / 2, (c.b + 255) / 2, 255 };
		gfx_glow(it->x, y, 11 * size, (SDL_Color){ c.r, c.g, c.b, blink ? 150 : 90 });
		gfx_tinted(SPR_CAPSULE, it->x, y, size, c);
		if (!it->rise)
			gfx_text(it->x - 1.5f, y - 2.5f, 1, DARK, "%c", item_look[it->kind].letter);
	}
}

static SDL_Color faint(SDL_Color c)
{
	c.a /= 2;

	return c;
}

static void draw_bullets(float cam)
{
	for (int i = 0; i < MAX_BULLET; i++) {
		const struct bullet *b = &g.bullet[i];
		float y = b->y - cam;

		if (!b->alive)
			continue;
		if (!b->friendly) {
			/* shots on the other level cannot hit, draw them faint */
			SDL_Color c = (b->level & L_AIR) ? RED : ORANGE, mid = (g.frame & 4) ? YELLOW : WHITE;
			float r = (b->level & L_AIR) ? 3 : 2;

			c.a = mid.a = (b->level & player_level()) ? 255 : 70;
			gfx_glow(b->x, y, r * 3, faint(c));
			gfx_disc(b->x, y, r, c);
			gfx_disc(b->x, y, r / 2, mid);
		} else if (b->homing) {
			gfx_glow(b->x, y, 8, ORANGE);
			gfx_disc(b->x, y, 2.4f, WHITE);
			gfx_disc(b->x, y, 1.3f, RED);
		} else if (b->level & L_AIR) {
			gfx_glow(b->x, y, 6, faint(CYAN));
			gfx_rect(b->x - 1, y - 3, 2, 6, CYAN);
			gfx_rect(b->x - 0.5f, y - 3, 1, 6, WHITE);
		} else {
			gfx_glow(b->x, y, 6, faint(YELLOW));
			gfx_disc(b->x, y, 1.8f, YELLOW);
		}
	}
}

/* The fireball swells fast, then burns down from the outside in. */
static void draw_blast(const struct part *q, float cam)
{
	float k = 1 - (float)q->life / BLAST_LIFE, y = q->y - cam;
	float r = q->blast * (k < 0.25f ? 0.5f + 2 * k : 1 - (k - 0.25f) * 0.6f);
	SDL_Color hot = ORANGE;

	hot.a = 255 * (1 - k);
	gfx_glow(q->x, y, q->blast * (1.6f + k), hot);
	gfx_disc(q->x, y, r, (SDL_Color){ 200, 60, 40, 220 * (1 - k * k) });
	gfx_disc(q->x, y, r * (0.8f - 0.5f * k), ORANGE);
	if (k < 0.7f)
		gfx_disc(q->x, y, r * (0.55f - 0.7f * k), YELLOW);
	if (k < 0.35f)
		gfx_disc(q->x, y, r * (0.3f - 0.8f * k), WHITE);
}

static void draw_hud(void)
{
	const struct player *p = &g.p;

	gfx_rect(0, 0, SCREEN_W, 10, DARK);
	gfx_text(3, 2, 1, WHITE, "%06d", g.score);

	for (int i = 0; i < MAX_HP; i++)
		gfx_rect(40 + i * 7, 2, 6, 5, i < p->hp ? CYAN : (SDL_Color){ 40, 48, 64, 255 });

	gfx_text(80, 2, 1, p->fused ? YELLOW : p->ship ? CYAN : GREEN, p->fused ? "FUSED" : p->ship ? "SHIP" : "TANK");
	gfx_text(108, 2, 1, YELLOW, "GUN %d", p->weapon);
	if (p->missiles)
		gfx_text(196, 2, 1, RED, "H %d", p->missiles);
	gfx_text(-1, 2, 1, GREY, "HI %06d", g.hiscore);
	if (beyond())
		gfx_text(SCREEN_W - 80, 2, 1, YELLOW, "BEYOND X%d", 2 + lap());
	else
		gfx_text(SCREEN_W - 76, 2, 1, WHITE, "SECTOR %d", g.sector + 1);
	gfx_text(SCREEN_W - 30, 2, 1, WHITE, "LIFE %d", SDL_max(g.lives, 0));

	for (int i = 0; i < MAX_ENEMY; i++) {
		const struct enemy *e = &g.enemy[i];

		if (!e->alive || e->type != E_CORE)
			continue;
		gfx_rect(SCREEN_W / 2 - 51, 12, 102, 4, DARK);
		gfx_rect(SCREEN_W / 2 - 50, 13, 100.0f * e->hp / e->full, 2, shielded(e) ? CYAN : RED);
		if (e->t < 240 && (e->t & 16))
			gfx_text(-1, 20, 2, YELLOW, "%s", boss()->name);
	}
}

void game_draw(void)
{
	float cam = g.cam + ((g.frame & 2) ? 0.4f : -0.4f) * g.shake;
	int blink = (g.frame / 30) & 1;

	if (beyond())
		draw_stars();
	draw_tiles(0, cam);

	if (g.state == ST_TITLE) {
		draw_tiles(1, cam);
		gfx_rect(0, 0, SCREEN_W, SCREEN_H, (SDL_Color){ 0, 0, 0, 130 });
		gfx_logo(14, g.frame);
		if (blink)
			gfx_text(-1, 124, 2, WHITE, "PRESS FIRE");
		gfx_text(-1, 170, 1, GREY, "ARROWS MOVE - Z FIRES - SPACE MORPHS");
		gfx_text(-1, 182, 1, GREY, "MOUSE: LEFT FIRES - RIGHT MORPHS");
		gfx_text(-1, 194, 1, GREY, "JOYSTICK: WIGGLE TO MORPH");
		gfx_text(-1, 206, 1, GREY, "M: MOUSE STEERING - ESC: QUIT");
		if (g.hiscore)
			gfx_text(-1, 220, 1, YELLOW, "HI %06d", g.hiscore);
		return;
	}

	draw_items(cam, blink);
	draw_enemies(0, cam);
	if (!g.p.ship)
		draw_player(cam);
	draw_tiles(1, cam);
	draw_enemies(1, cam);
	if (g.p.ship)
		draw_player(cam);
	draw_bullets(cam);

	for (int i = 0; i < MAX_PART; i++) {
		const struct part *q = &g.part[i];
		float sz = q->life > 12 ? 1.6f : 1;

		if (q->life <= 0)
			continue;
		if (q->blast)
			draw_blast(q, cam);
		else
			gfx_rect(q->x, q->y - cam, sz, sz, q->c);
	}

	if (g.mouse && !g.direct && g.state == ST_PLAY && !g.p.dead) {
		for (int i = -1; i <= 1; i += 2) {
			gfx_rect(g.mx + i * 4 - 1.5f, g.my - 0.5f, 3, 1, MARK);
			gfx_rect(g.mx - 0.5f, g.my + i * 4 - 1.5f, 1, 3, MARK);
		}
	}

	if (g.flash)
		gfx_rect(0, 0, SCREEN_W, SCREEN_H, (SDL_Color){ 255, 255, 255, g.flash * 10 });

	draw_hud();

	if (g.state == ST_CLEAR) {
		gfx_text(-1, 90, 3, YELLOW, "SECTOR %d CLEAR", g.sector + 1);
	} else if (g.state == ST_FUSE) {
		if (fuse_t() < FUSE_BURN)
			gfx_text(-1, 60, 3, YELLOW, "SECTOR NINE CLEARED");
		else if (g.p.fused)
			gfx_text(-1, 60, 4, CYAN, "FUSED");
		if (g.p.fused && blink)
			gfx_text(-1, 88, 1, WHITE, "ONE CRAFT - BOTH LEVELS - GO BEYOND");
	} else if (g.state == ST_OVER) {
		gfx_text(-1, 90, 4, RED, "GAME OVER");
		if (beyond())
			gfx_text(-1, 76, 1, YELLOW, "YOU WENT BEYOND SECTOR NINE");
		gfx_text(-1, 124, 1, WHITE, "SCORE %06d", g.score);
		if (!g.timer && blink)
			gfx_text(-1, 140, 1, GREY, "PRESS FIRE");
	}
}
