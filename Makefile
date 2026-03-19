# Typewriter - lightweight text editor
# Requires: SDL2, SDL2_ttf

CC      ?= gcc
WINDRES ?= windres
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
LDFLAGS ?=

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 SDL2_ttf 2>/dev/null)

# Fallback if pkg-config not available
ifeq ($(SDL_CFLAGS),)
  SDL_CFLAGS = -I/usr/include/SDL2
  SDL_LIBS   = -lSDL2 -lSDL2_ttf
endif

TARGET = typewriter
OBJS   = main.c
RES    =

ifeq ($(OS),Windows_NT)
  TARGET = typewriter.exe
  LDFLAGS += -mwindows -lcomdlg32
  RES = typewriter_res.o
endif

all: $(TARGET)

# Windows resource (embeds icon.ico into the exe)
typewriter_res.o: typewriter.rc icon.ico
	$(WINDRES) typewriter.rc -o typewriter_res.o

$(TARGET): main.c $(RES)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ main.c $(RES) $(SDL_LIBS) $(LDFLAGS) -lm

clean:
	rm -f $(TARGET) typewriter_res.o

.PHONY: all clean
