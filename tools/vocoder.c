/* SPDX-License-Identifier: ISC */
/* Channel vocoder: raw 16-bit mono on stdin and stdout.  The speech shapes a
 * buzz of fixed pitch, which gives the flat voice of old machines.
 *
 *     vocoder RATE PITCH HISS
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define BANDS 18

struct bq { double b0, b2, a1, a2, x1, x2, y1, y2; };

static void bandpass(struct bq *f, double hz, double q, double rate)
{
	double w = 2 * M_PI * hz / rate, alpha = sin(w) / (2 * q), a0 = 1 + alpha;

	f->b0 = alpha / a0;
	f->b2 = -alpha / a0;
	f->a1 = -2 * cos(w) / a0;
	f->a2 = (1 - alpha) / a0;
}

static double run(struct bq *f, double x)
{
	double y = f->b0 * x + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;

	f->x2 = f->x1;
	f->x1 = x;
	f->y2 = f->y1;
	f->y1 = y;

	return y;
}

int main(int argc, char *argv[])
{
	static struct bq mod[BANDS], car[BANDS];
	double env[BANDS] = { 0 }, phase = 0, phase2 = 0;
	double rate, pitch, hiss, smooth;
	unsigned lfsr = 0xace1u;
	short s;

	if (argc != 4) {
		fprintf(stderr, "Usage: vocoder RATE PITCH HISS\n");
		return 1;
	}
	rate = atof(argv[1]);
	pitch = atof(argv[2]);
	hiss = atof(argv[3]);
	/* the envelope of each band follows the speech up to 35 Hz */
	smooth = exp(-2 * M_PI * 35 / rate);

	/* bands a constant ratio apart, from 140 Hz to 7 kHz */
	for (int i = 0; i < BANDS; i++) {
		bandpass(&mod[i], 140 * pow(7000.0 / 140, (double)i / (BANDS - 1)), 7, rate);
		car[i] = mod[i];
	}

	while (fread(&s, sizeof(s), 1, stdin) == 1) {
		double x = s / 32768.0, buzz, out = 0;

		phase += pitch / rate;
		phase -= floor(phase);
		/* a second saw, a touch sharp, thickens the buzz */
		phase2 += pitch * 1.006 / rate;
		phase2 -= floor(phase2);
		lfsr ^= lfsr << 13;
		lfsr ^= lfsr >> 17;
		lfsr ^= lfsr << 5;
		buzz = (phase + phase2 - 1) + hiss * ((lfsr & 0xffff) / 32768.0 - 1);

		for (int i = 0; i < BANDS; i++) {
			double m = fabs(run(&mod[i], x));

			env[i] = m + smooth * (env[i] - m);
			out += run(&car[i], buzz) * env[i];
		}

		out *= 14;
		s = (short)(fmax(-1, fmin(1, out)) * 32000);
		fwrite(&s, sizeof(s), 1, stdout);
	}

	return 0;
}
