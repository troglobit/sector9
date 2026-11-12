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
# Piper and SoX.  VOICE is a Piper voice model; the ones here were made
# with en_US-joe-medium, which is CC0.  Piper adds a little randomness, so
# no two runs give the same files.
VOICE  ?= en_US-joe-medium.onnx
WORDS   = tower gun-tower crawler drone dart rover gunship heavy-tank \
	  warden pinwheel trident ram brood cyclone lancer swarm overseer \
	  sector-one sector-two sector-three sector-four sector-five \
	  sector-six sector-seven sector-eight sector-nine beyond

voices:
	mkdir -p voice
	set -e; for w in $(WORDS); do \
		piper -m $(VOICE) --length-scale 1.15 -f voice/tmp.wav -- "$$(echo $$w | tr - ' ')."; \
		sox -D voice/tmp.wav -r 22050 -b 16 -c 1 voice/$$w.wav \
			silence 1 0.01 0.1% reverse silence 1 0.01 0.1% reverse pad 0.02 0.05 norm -1; \
	done
	$(RM) voice/tmp.wav

.PHONY: all clean voices
