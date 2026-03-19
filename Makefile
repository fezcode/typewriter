# Typewriter - lightweight text editor
# Requires: SDL2, SDL2_ttf

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?=

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 SDL2_ttf 2>/dev/null)

# Fallback if pkg-config not available
ifeq ($(SDL_CFLAGS),)
  SDL_CFLAGS = -I/usr/include/SDL2
  SDL_LIBS   = -lSDL2 -lSDL2_ttf
endif

TARGET = typewriter
ifeq ($(OS),Windows_NT)
  TARGET = typewriter.exe
  LDFLAGS += -mconsole
endif

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) $(LDFLAGS) -lm

clean:
	rm -f $(TARGET)

.PHONY: all clean
