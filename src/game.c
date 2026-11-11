#include <math.h>
#include <string.h>

#include "game.h"

#define MAX_ENEMY  64
#define MAX_BULLET 160
#define MAX_PART   200
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
#define HALT_Y     48

enum { ST_TITLE, ST_PLAY, ST_CLEAR, ST_OVER };
enum { IT_GUN, IT_SHIELD, IT_SPEED, IT_BUDDY, IT_MISSILE, IT_BOMB, IT_MAX };
enum { L_GROUND = 1, L_AIR = 2 };

struct enemy  { int alive, type, delay, hp, t, flash; float x, y, vx; };
struct bullet { int alive, friendly, level; float x, y, vx, vy; int homing; };
struct part   { int life; float x, y, vx, vy; SDL_Color c; };
struct item   { int alive, kind, rise; float x, y; };
struct player { float x, y; int ship, dir, diag, hp, weapon, speed, buddies, missiles, missile_cd, reload, morph_cd, invuln, dead; };
struct axis   { int sign, seen, flip; };

static struct {
	int           state, sector, score, hiscore, lives, timer, frame;
	int           pods, next_spawn, start_row, flash;
	struct axis   wx, wy;
	float         cam;
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
static const SDL_Color YELLOW = { 250, 226,  90, 255 };
static const SDL_Color ORANGE = { 240, 140,  40, 255 };
static const SDL_Color RED    = { 230,  60,  70, 255 };
static const SDL_Color GREEN  = {  90, 200, 110, 255 };
static const SDL_Color DARK   = {   8,  10,  16, 255 };

/* hit_by is the level a shot must be on to damage it.  Towers and pods
 * stand on a block, out of the tank's reach. */
static const struct etype_info {
	int hp, score, radius, sprite, scale, hit_by, flies, boss;
} et[] = {
	[E_TURRET]  = {  3,  100,  7, SPR_TURRET,  1, L_AIR,    0, 0 },
	[E_CRAWLER] = {  2,  150,  7, SPR_CRAWLER, 1, L_GROUND, 0, 0 },
	[E_DRONE]   = {  2,  100,  7, SPR_DRONE,   1, L_AIR,    1, 0 },
	[E_DART]    = {  1,  150,  7, SPR_DART,    1, L_AIR,    1, 0 },
	[E_POD]     = { 16, 1000, 13, SPR_TURRET,  2, L_AIR,    0, 1 },
	[E_CORE]    = { 40, 5000, 20, SPR_CORE,    3, L_GROUND, 0, 1 },
	[E_ROVER]   = {  3,  200,  7, SPR_ROVER,   1, L_GROUND, 0, 0 },
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

static int player_level(void)
{
	return g.p.ship ? L_AIR : L_GROUND;
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

static int row_has(float y, int t)
{
	int r = (int)y / TILE;

	return y >= 0 && r < MAP_H && memchr(lv.tile[r], t, MAP_W);
}

/* The ship flies at a steady pace and halts in front of a roof. */
static void ship_scroll(struct player *p)
{
	float halt = g.cam + HALT_Y;
	float bottom = g.cam - SHIP_SCROLL + SCREEN_H - 8;

	if (halt < p->y && row_has(halt, T_ROOF))
		return;
	if (p->y > bottom) {
		if (box_hits(p->x, bottom, T_ROOF))
			return;
		p->y = bottom;
	}
	g.cam -= SHIP_SCROLL;
}

static void spark(float x, float y, int n, float speed, SDL_Color c)
{
	for (int i = 0; i < MAX_PART && n > 0; i++) {
		struct part *q = &g.part[i];

		if (q->life > 0)
			continue;
		*q = (struct part){ 15 + rnd(25), x, y, frand() * speed, frand() * speed, c };
		n--;
	}
}

static void explode(float x, float y, int size)
{
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
		IT_GUN, IT_GUN, IT_SHIELD, IT_SHIELD, IT_SPEED, IT_BUDDY, IT_MISSILE, IT_MISSILE, IT_BOMB,
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
			e->hp += e->hp * g.sector / 4;
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

static void start_sector(int sector)
{
	struct player *p = &g.p;

	g.sector = sector;
	level_generate(&lv, sector);

	memset(g.enemy, 0, sizeof(g.enemy));
	memset(g.bullet, 0, sizeof(g.bullet));
	memset(g.item, 0, sizeof(g.item));
	g.pods = 0;
	g.next_spawn = 0;
	g.cam = g.start_row ? g.start_row * TILE : START_CAM;
	g.state = ST_PLAY;
	while (g.next_spawn < lv.nspawn && lv.spawn[g.next_spawn].y > g.cam + SCREEN_H + 24)
		g.next_spawn++;

	p->x = SCREEN_W / 2;
	p->y = g.cam + SCREEN_H - 40;
	p->ship = 0;
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
	sfx_music("sector");
	start_sector(0);
}

static void hurt_player(void)
{
	struct player *p = &g.p;

	sfx_play(SFX_HURT);
	p->invuln = 60;
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
		return;
	}

	e->alive = 0;
	g.score += et[e->type].score;
	explode(e->x, e->y, et[e->type].radius * 4);

	if (e->type == E_POD) {
		g.pods--;
	} else if (e->type == E_CORE) {
		memset(g.bullet, 0, sizeof(g.bullet));
		g.state = ST_CLEAR;
		g.timer = 240;
	} else if (e->type == E_TURRET) {
		/* a supply capsule may rise out of the wrecked tower's block */
		if (rnd(3) == 0)
			drop_item(e->x, e->y, RISE_FRAMES);
	} else if (rnd(6) == 0) {
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

	for (int i = 0; i < p->buddies; i++)
		add_bullet(g.bx[i], g.by[i], ang, speed, 1, level);

	p->reload = p->ship ? 8 : 12;
	sfx_play(SFX_SHOT);
}

static void update_player(const struct input *in, int morph)
{
	static const int facing[3][3] = { { 7, 0, 1 }, { 6, -1, 2 }, { 5, 4, 3 } };
	struct player *p = &g.p;
	int dx = in->right - in->left, dy = in->down - in->up;
	float speed = (p->ship ? 2.0f : 1.2f) * (1 + 0.2f * p->speed);
	int block = p->ship ? T_ROOF : T_WALL;
	float nx, ny, sy;

	if (p->dead) {
		if (--p->dead > 0)
			return;
		if (g.lives < 0) {
			if (g.score > g.hiscore)
				g.hiscore = g.score;
			g.state = ST_OVER;
			g.timer = 60;
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

	if (dx && dy)
		speed *= 0.7071f;

	nx = SDL_clamp(p->x + dx * speed, 8, SCREEN_W - 8);
	if (!box_hits(nx, p->y, block))
		p->x = nx;
	ny = SDL_clamp(p->y + dy * speed, g.cam + 18, g.cam + SCREEN_H - 8);
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

	if (morph && !p->morph_cd) {
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

static void update_enemies(void)
{
	struct player *p = &g.p;
	int period = SDL_max(60, 120 - g.sector * 10);

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

		vis = e->y > g.cam + 12 && e->y < g.cam + SCREEN_H;
		aim = aim_at(e);

		switch (e->type) {
		case E_TURRET:
			if (vis && e->t % period == 0)
				add_bullet(e->x, e->y, aim, 1.6f, 0, L_AIR);
			break;

		case E_CRAWLER:
			e->x += e->vx;
			if (e->x < 8 || e->x > SCREEN_W - 8 || box_hits(e->x, e->y, T_WALL)) {
				e->vx = -e->vx;
				e->x += e->vx * 2;
			}
			break;

		case E_DRONE:
			e->y += 0.5f;
			e->x = e->vx + 40 * sinf(e->t * 0.05f);
			if (vis && e->t % 100 == 0)
				add_bullet(e->x, e->y, aim, 1.8f, 0, L_AIR);
			break;

		case E_DART:
			e->y += 1.4f;
			e->x += p->x > e->x ? 0.5f : -0.5f;
			break;

		case E_POD:
			if (vis && e->t % 80 == 0) {
				for (int n = -1; n <= 1; n++)
					add_bullet(e->x, e->y, aim + n * 0.3f, 1.8f, 0, L_AIR);
			}
			break;

		case E_CORE:
			e->x = SCREEN_W / 2 + 70 * sinf(e->t * 0.02f);
			if (vis && !shielded(e) && e->t % 70 == 0) {
				for (int n = 0; n < 10; n++)
					add_bullet(e->x, e->y, n * (float)M_PI / 5 + e->t, 1.5f, 0, L_GROUND);
			}
			break;

		case E_ROVER:
			if (!vis)
				break;
			if (fabsf(p->x - e->x) > 2) {
				float nx = e->x + (p->x > e->x ? 0.4f : -0.4f);

				if (!box_hits(nx, e->y, T_WALL))
					e->x = nx;
			}
			if (fabsf(p->y - e->y) > 2) {
				float ny = e->y + (p->y > e->y ? 0.4f : -0.4f);

				if (!box_hits(e->x, ny, T_WALL))
					e->y = ny;
			}
			if (e->t % period == 0)
				add_bullet(e->x, e->y, aim, 1.6f, 0, L_GROUND);
			break;
		}

		if (e->y > g.cam + SCREEN_H + 24) {
			e->alive = 0;
			continue;
		}

		r = t->radius + 5;
		if (!p->dead && !p->invuln && t->flies == p->ship &&
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

			if (!e->alive || e->delay || !(b->level & et[e->type].hit_by))
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
		g.score += 50;
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

static void show_title(void)
{
	level_generate(&lv, 0);
	g.cam = START_CAM;
	g.state = ST_TITLE;
	sfx_music("title");
}

void game_init(int start_row)
{
	memset(&g, 0, sizeof(g));
	g.seed = 0x2545f491u;
	g.start_row = start_row;
	g.wx.flip = g.wy.flip = -1000;
	show_title();
}

void game_update(const struct input *in)
{
	int morph = in->morph && !g.prev.morph;
	int start = (in->fire && !g.prev.fire) || (in->start && !g.prev.start);

	g.prev = *in;
	g.frame++;
	if (g.flash)
		g.flash--;

	/* one-button joystick: a quick wiggle on either axis morphs */
	morph |= wiggle(&g.wx, in->right - in->left);
	morph |= wiggle(&g.wy, in->down - in->up);

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
		break;

	case ST_CLEAR:
		update_particles();
		if (--g.timer == 0)
			start_sector(g.sector + 1);
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

/* Test driver: push forward, morph when the way is blocked. */
void game_autopilot(struct input *in)
{
	static float last;
	static int stuck;

	stuck = g.cam == last ? stuck + 1 : 0;
	last = g.cam;

	memset(in, 0, sizeof(*in));
	in->up = 1;
	in->fire = g.state != ST_PLAY ? g.frame & 1 : 1;
	in->left = (g.frame / 50) & 1;
	in->right = !in->left;
	in->morph = stuck % 45 == 31;
}

static void draw_tiles(int roof)
{
	static const SDL_Color tints[] = {
		{ 255, 255, 255, 255 }, { 255, 238, 222, 255 }, { 222, 255, 240, 255 }, { 240, 226, 255, 255 },
	};
	SDL_Color tint = tints[g.sector % 4];
	int r0 = (int)g.cam / TILE;
	int oy = -((int)g.cam % TILE);
	int glow = 50 + (int)(12 * sinf(g.frame * 0.1f));

	for (int r = r0; r <= r0 + SCREEN_H / TILE && r < MAP_H; r++) {
		for (int c = 0; c < MAP_W; c++) {
			int t = lv.tile[r][c];
			int x = c * TILE, y = (r - r0) * TILE + oy;
			unsigned h = (r * 73856093u) ^ (c * 19349663u);

			if (roof) {
				if (t != T_ROOF)
					continue;
				gfx_rect(x, y, TILE, TILE, (SDL_Color){ 40, 200, 220, glow });
				gfx_rect(x, y, TILE, 1, (SDL_Color){ 130, 240, 255, 90 });
				gfx_rect(x, y, 1, TILE, (SDL_Color){ 130, 240, 255, 90 });
			} else if (t == T_WALL) {
				gfx_plate(PLATE_WALL, x, y, tint);
			} else {
				gfx_plate(h % 11 == 0 ? PLATE_VENT : h % 5 == 0 ? PLATE_SEAM : PLATE_FLOOR, x, y, tint);
				if (r > 0 && lv.tile[r - 1][c] == T_WALL)
					gfx_rect(x, y, TILE, 5, (SDL_Color){ 0, 0, 0, 90 });
				if (c > 0 && lv.tile[r][c - 1] == T_WALL)
					gfx_rect(x, y, 4, TILE, (SDL_Color){ 0, 0, 0, 70 });
			}
		}
	}
}

static void draw_enemies(int air, int icam)
{
	for (int i = 0; i < MAX_ENEMY; i++) {
		const struct enemy *e = &g.enemy[i];
		const struct etype_info *t = &et[e->type];
		float y = e->y - icam, aim = aim_at(e), ang = 0;
		int scale = t->scale;

		if (!e->alive || e->delay || t->flies != air)
			continue;

		if (e->type == E_CRAWLER) {
			ang = e->vx > 0 ? 90 : -90;
			y += (e->t >> 3) & 1;
		} else if (e->type == E_ROVER) {
			ang = aim * 180 / (float)M_PI;
		}

		/* whatever the ship can hit is up high and casts a shadow */
		if (t->flies)
			gfx_sprite(t->sprite, e->x + 4, y + 8, 0, scale, DRAW_SHADOW);
		else if (t->hit_by & L_AIR)
			gfx_sprite(t->sprite, e->x + 3 * scale, y + 5 * scale, 0, scale, DRAW_SHADOW);
		gfx_sprite(t->sprite, e->x, y, ang, scale, e->flash ? DRAW_FLASH : DRAW_NORMAL);

		if (e->type == E_TURRET || e->type == E_POD) {
			float bx = sinf(aim) * 2 * scale, by = -cosf(aim) * 2 * scale;

			for (int k = 2; k <= 4; k++)
				gfx_rect(e->x + bx * k - scale, y + by * k - scale, 2 * scale, 2 * scale, WHITE);
		}
		if (shielded(e) && (g.frame & 4))
			gfx_rect(e->x - 26, y + 26, 52, 2, CYAN);
	}
}

static void draw_player(int icam)
{
	const struct player *p = &g.p;
	float y = p->y - icam;

	if (p->dead || (p->invuln & 2))
		return;

	for (int i = 0; i < p->buddies; i++) {
		int bx = g.bx[i], by = g.by[i] - icam;

		gfx_rect(bx - 3, by - 1, 6, 2, CYAN);
		gfx_rect(bx - 1, by - 3, 2, 6, CYAN);
		gfx_rect(bx - 1, by - 1, 2, 2, WHITE);
	}

	if (p->ship) {
		gfx_sprite(SPR_SHIP, p->x + 4, y + 8, 0, 1, DRAW_SHADOW);
		gfx_sprite(SPR_SHIP, p->x, y, 0, 1, DRAW_NORMAL);
	} else {
		gfx_sprite(SPR_TANK, p->x, y, player_angle() * 180 / (float)M_PI, 1, DRAW_NORMAL);
	}
}

static void draw_hud(void)
{
	const struct player *p = &g.p;

	gfx_rect(0, 0, SCREEN_W, 10, DARK);
	gfx_text(3, 2, 1, WHITE, "%06d", g.score);

	for (int i = 0; i < MAX_HP; i++)
		gfx_rect(40 + i * 7, 2, 6, 5, i < p->hp ? CYAN : (SDL_Color){ 40, 48, 64, 255 });

	gfx_text(80, 2, 1, p->ship ? CYAN : GREEN, p->ship ? "SHIP" : "TANK");
	gfx_text(108, 2, 1, YELLOW, "GUN %d", p->weapon);
	if (p->missiles)
		gfx_text(196, 2, 1, RED, "H %d", p->missiles);
	gfx_text(-1, 2, 1, GREY, "HI %06d", SDL_max(g.hiscore, g.score));
	gfx_text(SCREEN_W - 76, 2, 1, WHITE, "SECTOR %d", g.sector + 1);
	gfx_text(SCREEN_W - 30, 2, 1, WHITE, "LIFE %d", SDL_max(g.lives, 0));
}

void game_draw(void)
{
	int icam = (int)g.cam;
	int blink = (g.frame / 30) & 1;

	draw_tiles(0);

	if (g.state == ST_TITLE) {
		draw_tiles(1);
		gfx_rect(0, 0, SCREEN_W, SCREEN_H, (SDL_Color){ 0, 0, 0, 130 });
		gfx_text(-1, 54, 4, CYAN, "SECTOR NINE");
		gfx_text(-1, 90, 1, GREY, "TANK FIGHTS THE GROUND - SHIP FIGHTS THE AIR");
		if (blink)
			gfx_text(-1, 124, 2, WHITE, "PRESS FIRE");
		gfx_text(-1, 164, 1, GREY, "ARROWS OR WASD: MOVE");
		gfx_text(-1, 174, 1, GREY, "Z OR CTRL: FIRE - HOLD FOR AUTOFIRE");
		gfx_text(-1, 184, 1, GREY, "SPACE OR WIGGLE THE STICK: MORPH");
		gfx_text(-1, 194, 1, GREY, "P: PAUSE   F11: FULLSCREEN   ESC: QUIT");
		gfx_text(-1, 220, 1, YELLOW, "HI %06d", g.hiscore);
		return;
	}

	for (int i = 0; i < MAX_ITEM; i++) {
		const struct item *it = &g.item[i];
		int y = it->y - icam;
		int half = 5 - 4 * it->rise / RISE_FRAMES;

		if (!it->alive)
			continue;
		gfx_rect(it->x - half, y - half, half * 2, half * 2, blink ? WHITE : GREY);
		gfx_rect(it->x - half + 1, y - half + 1, half * 2 - 2, half * 2 - 2, *item_look[it->kind].tint);
		if (!it->rise)
			gfx_text(it->x - 1, y - 2, 1, DARK, "%c", item_look[it->kind].letter);
	}

	draw_enemies(0, icam);
	if (!g.p.ship)
		draw_player(icam);
	draw_tiles(1);
	draw_enemies(1, icam);
	if (g.p.ship)
		draw_player(icam);

	for (int i = 0; i < MAX_BULLET; i++) {
		const struct bullet *b = &g.bullet[i];
		int y = b->y - icam;

		if (!b->alive)
			continue;
		if (!b->friendly) {
			/* shots on the other level cannot hit, draw them faint */
			SDL_Color c = (b->level & L_AIR) ? RED : ORANGE, mid = (g.frame & 4) ? YELLOW : WHITE;
			int sz = (b->level & L_AIR) ? 2 : 1;

			c.a = mid.a = (b->level & player_level()) ? 255 : 70;
			gfx_rect(b->x - sz, y - sz, sz * 2, sz * 2, c);
			gfx_rect(b->x - 1, y - 1, sz, sz, mid);
		} else if (b->homing) {
			gfx_rect(b->x - 2, y - 2, 4, 4, WHITE);
			gfx_rect(b->x - 1, y - 1, 2, 2, RED);
		} else if (b->level & L_AIR) {
			gfx_rect(b->x - 1, y - 3, 2, 6, CYAN);
		} else {
			gfx_rect(b->x - 1, y - 1, 3, 3, YELLOW);
		}
	}

	for (int i = 0; i < MAX_PART; i++) {
		const struct part *q = &g.part[i];
		int sz = q->life > 12 ? 2 : 1;

		if (q->life > 0)
			gfx_rect(q->x, q->y - icam, sz, sz, q->c);
	}

	if (g.flash)
		gfx_rect(0, 0, SCREEN_W, SCREEN_H, (SDL_Color){ 255, 255, 255, g.flash * 10 });

	draw_hud();

	if (g.state == ST_CLEAR) {
		gfx_text(-1, 90, 3, YELLOW, "SECTOR %d CLEAR", g.sector + 1);
	} else if (g.state == ST_OVER) {
		gfx_text(-1, 90, 4, RED, "GAME OVER");
		gfx_text(-1, 124, 1, WHITE, "SCORE %06d", g.score);
		if (!g.timer && blink)
			gfx_text(-1, 140, 1, GREY, "PRESS FIRE");
	}
}
