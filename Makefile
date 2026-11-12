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

.PHONY: all clean
