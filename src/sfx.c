/* SPDX-License-Identifier: ISC */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <SDL_mixer.h>

#include "game.h"

#define RATE   22050
#define VOICES 8
#define WORDS  24

struct voice {
	float phase, freq, sweep, vol, decay, level;
	int   noise;
};

/* sweep and decay are per-sample multipliers of freq and vol */
#define PRESET(f, s, v, d, n) { .freq = f, .sweep = s, .vol = v, .decay = d, .level = 1, .noise = n }

static const struct voice preset[SFX_MAX] = {
	[SFX_SHOT]   = PRESET( 900, 0.99970f, 0.25f, 0.99950f, 0),
	[SFX_HIT]    = PRESET(3000, 0.99980f, 0.35f, 0.99900f, 1),
	[SFX_BOOM]   = PRESET(1400, 0.99990f, 0.80f, 0.99975f, 1),
	[SFX_MORPH]  = PRESET( 220, 1.00015f, 0.40f, 0.99970f, 0),
	[SFX_DENY]   = PRESET( 110, 1.00000f, 0.40f, 0.99940f, 0),
	[SFX_PICKUP] = PRESET( 660, 1.00020f, 0.40f, 0.99960f, 0),
	[SFX_HURT]   = PRESET( 700, 0.99990f, 0.70f, 0.99970f, 1),
};

static struct voice voice[VOICES];
static SDL_atomic_t pending[SFX_MAX];
static unsigned     lfsr = 0xace1u;
static int          ready, out_rate, out_chan;
static Mix_Music   *music;
static char         playing[32];

static struct {
	char       name[24];
	Mix_Chunk *chunk;
} word[WORDS];

static int synth(void)
{
	float s = 0;

	for (int n = 0; n < VOICES; n++) {
		struct voice *v = &voice[n];

		if (v->vol < 0.002f)
			continue;

		v->phase += v->freq / RATE;
		if (v->phase >= 1.0f) {
			v->phase -= 1.0f;
			if (v->noise) {
				lfsr ^= lfsr << 13;
				lfsr ^= lfsr >> 17;
				lfsr ^= lfsr << 5;
				v->level = (lfsr & 1) ? 1.0f : -1.0f;
			}
		}
		if (!v->noise)
			v->level = v->phase < 0.5f ? 1.0f : -1.0f;

		s += v->level * v->vol;
		v->freq *= v->sweep;
		v->vol *= v->decay;
	}

	return (int)(SDL_clamp(s, -1.0f, 1.0f) * 9000);
}

/* Adds the effects on top of whatever SDL_mixer has mixed. */
static void mix(void *arg, Uint8 *stream, int len)
{
	static int acc, cur;
	Sint16 *out = (Sint16 *)stream;

	(void)arg;
	for (int id = 0; id < SFX_MAX; id++) {
		struct voice *v = &voice[0];

		if (!SDL_AtomicSet(&pending[id], 0))
			continue;
		for (int n = 1; n < VOICES; n++) {
			if (voice[n].vol < v->vol)
				v = &voice[n];
		}
		*v = preset[id];
	}

	for (int i = 0; i < len / 2; i += out_chan) {
		acc += RATE;
		if (acc >= out_rate) {
			acc -= out_rate;
			cur = synth();
		}
		for (int c = 0; c < out_chan; c++)
			out[i + c] = SDL_clamp(out[i + c] + cur, -32768, 32767);
	}
}

void sfx_init(void)
{
	Uint16 fmt;

	if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024))
		return;
	Mix_QuerySpec(&out_rate, &fmt, &out_chan);
	if (fmt != AUDIO_S16SYS) {
		Mix_CloseAudio();
		return;
	}

	Mix_SetPostMix(mix, NULL);
	Mix_VolumeMusic(MIX_MAX_VOLUME / 2);
	ready = 1;
}

void sfx_play(int id)
{
	SDL_AtomicSet(&pending[id], 1);
}

/* A file that comes with the game, next to the binary. */
static void locate(char *path, size_t len, const char *dir, const char *name, const char *ext)
{
	char *base = SDL_GetBasePath();

	snprintf(path, len, "%s%s/%s.%s", base ? base : "", dir, name, ext);
	SDL_free(base);
}

/* Loops music/<name>.mod, a missing file means silence. */
void sfx_music(const char *name)
{
	char path[1024];

	if (!ready || !strcmp(playing, name))
		return;
	snprintf(playing, sizeof(playing), "%s", name);

	Mix_HaltMusic();
	Mix_FreeMusic(music);

	locate(path, sizeof(path), "music", name, "mod");
	music = Mix_LoadMUS(path);
	if (music)
		Mix_PlayMusic(music, -1);
}

/* One word at a time: a new one cuts off the last.  A missing file means silence. */
void sfx_say(const char *name)
{
	char path[1024], file[sizeof(word[0].name)];
	int i;

	if (!ready)
		return;

	snprintf(file, sizeof(file), "%s", name);
	for (char *p = file; *p; p++)
		*p = *p == ' ' ? '-' : tolower((unsigned char)*p);

	for (i = 0; i < WORDS && word[i].name[0] && strcmp(word[i].name, file); i++)
		;
	if (i == WORDS)
		return;
	if (!word[i].name[0]) {
		snprintf(word[i].name, sizeof(word[i].name), "%s", file);
		locate(path, sizeof(path), "voice", file, "wav");
		word[i].chunk = Mix_LoadWAV(path);
	}
	if (word[i].chunk)
		Mix_PlayChannel(0, word[i].chunk, 0);
}
