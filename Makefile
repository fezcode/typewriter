# Typewriter - lightweight text editor
# Requires: SDL2, SDL2_ttf
#
#   make              dynamic build against the system SDL2 / SDL2_ttf (default)
#   make STATIC=1     self-contained executable: SDL2, SDL2_ttf and FreeType are
#                     linked in, nothing besides the OS is needed at runtime.
#       Windows       links the MSYS2 static libs
#                     (pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_ttf)
#       Linux/macOS   run `make deps` (or scripts/build-deps.sh) once first; it
#                     builds static SDL2 + SDL2_ttf into deps/prefix
#   make embed-font   regenerate font_embed.h after replacing typewriter.ttf

CC      ?= gcc
WINDRES ?= windres
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
LDFLAGS ?=
STATIC  ?= 0
DEPS_PREFIX ?= $(CURDIR)/deps/prefix

TARGET = typewriter
RES    =

ifeq ($(OS),Windows_NT)
  TARGET = typewriter.exe
  LDFLAGS += -mwindows -lcomdlg32
  RES = typewriter_res.o
endif

ifeq ($(STATIC),1)
  ifeq ($(OS),Windows_NT)
    # MSYS2 ships static .a variants of SDL2, SDL2_ttf and their dependencies.
    # HarfBuzz/graphite2 (pulled in by SDL2_ttf) are C++, hence -lstdc++.
    SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf 2>/dev/null)
    SDL_LIBS   := $(shell pkg-config --static --libs sdl2 SDL2_ttf 2>/dev/null)
    LDFLAGS    += -static -lstdc++
  else
    # Libraries built by scripts/build-deps.sh. sdl2-config knows the platform
    # libs/frameworks SDL2 needs; SDL2_ttf and its vendored FreeType are added.
    SDL2_CONFIG := $(DEPS_PREFIX)/bin/sdl2-config
    ifeq ($(wildcard $(SDL2_CONFIG)),)
      $(error STATIC=1 needs the static SDL2 build in $(DEPS_PREFIX). Run `make deps` first)
    endif
    -include $(DEPS_PREFIX)/deps.mk
    SDL_CFLAGS := $(shell $(SDL2_CONFIG) --cflags)
    SDL_LIBS   := -L$(DEPS_PREFIX)/lib -lSDL2_ttf -lfreetype $(shell $(SDL2_CONFIG) --static-libs)
    CFLAGS     += $(DEPS_ARCHFLAGS)
    LDFLAGS    += $(DEPS_ARCHFLAGS)
  endif
else
  SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf 2>/dev/null)
  SDL_LIBS   := $(shell pkg-config --libs sdl2 SDL2_ttf 2>/dev/null)
endif

# Fallback if pkg-config is not available
ifeq ($(SDL_CFLAGS),)
  SDL_CFLAGS = -I/usr/include/SDL2
  SDL_LIBS   = -lSDL2 -lSDL2_ttf
endif

all: $(TARGET)

# Windows resource (embeds icon.ico into the exe)
typewriter_res.o: typewriter.rc icon.ico
	$(WINDRES) typewriter.rc -o typewriter_res.o

$(TARGET): main.c font_embed.h $(RES)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ main.c $(RES) $(SDL_LIBS) $(LDFLAGS) -lm

# Static SDL2 + SDL2_ttf for STATIC=1 builds on Linux/macOS
deps:
	sh scripts/build-deps.sh

embed-font:
	sh scripts/embed-font.sh

clean:
	rm -f typewriter typewriter.exe typewriter_res.o

distclean: clean
	rm -rf deps dist

.PHONY: all deps embed-font clean distclean
