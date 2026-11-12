# SPDX-License-Identifier: ISC
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_mixer)
SDL_LIBS   := $(shell pkg-config --libs sdl2 SDL2_mixer)

CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra $(SDL_CFLAGS)
LDLIBS += $(SDL_LIBS) -lm

OBJS    = src/main.o src/game.o src/gfx.o src/level.o src/sfx.o

all: sector9

sector9: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJS): src/game.h

clean:
	$(RM) sector9 $(OBJS)

# The spoken names in voice/ are checked in.  This makes them again, with
# eSpeak NG and SoX, as 8-bit samples at 11 kHz.
ESPEAK ?= espeak-ng -v en-us -p 20 -s 130
WORDS   = tower gun-tower crawler drone dart rover gunship heavy-tank \
	  warden pinwheel trident ram brood cyclone lancer swarm overseer

voices:
	mkdir -p voice
	set -e; for w in $(WORDS); do \
		$(ESPEAK) --stdout "$$(echo $$w | tr - ' ')" | \
		sox -D -t wav - -r 11025 -b 8 voice/$$w.wav \
			silence 1 0.01 0.5% reverse silence 1 0.01 0.5% reverse norm -1; \
	done

.PHONY: all clean voices
