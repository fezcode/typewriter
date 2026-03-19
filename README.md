# Typewriter

A distraction-free text editor with the soul of a mechanical typewriter.

Built in C with SDL2 — fast, lightweight, cross-platform. Every keystroke clicks. Every carriage return clunks. The paper is warm, the cursor blinks, and the bell rings at column 80.

## Features

- **Real typewriter sounds** — recorded mechanical key strikes, embedded directly in the binary
- **Paper aesthetic** — cream background, red cursor, subtle margin line, optional ruled notebook lines
- **Lightweight** — single C file, ~750KB binary, near-zero CPU at idle
- **Cross-platform** — Windows, macOS, Linux
- **No dependencies at runtime** — everything is embedded (sounds, icon)
- **Native file dialogs** — Win32 API on Windows, osascript on macOS, zenity/kdialog on Linux
- **Drag and drop** — drop a file onto the window to open it
- **Undo** — Ctrl+Z with a 512-entry history
- **Options menu** — Ctrl+K to toggle sounds, line numbers, notebook lines

## Screenshots

Open the editor and start typing. That's it.

## Build

Requires SDL2 and SDL2_ttf development libraries.

**Windows (MSYS2/MinGW)**

```sh
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_ttf
make
```

**macOS**

```sh
brew install sdl2 sdl2_ttf
make
```

**Linux (Debian/Ubuntu)**

```sh
sudo apt install libsdl2-dev libsdl2-ttf-dev
make
```

## Usage

```
typewriter [options] [file]

Options:
  -h, --help       Show help and exit
  -v, --version    Show version and exit
```

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+S | Save |
| Ctrl+O | Open |
| Ctrl+Q | Quit |
| Ctrl+Z | Undo |
| Ctrl+K | Options menu |
| Ctrl+C/X/V | Copy / Cut / Paste |
| Ctrl+A | Select all |

## Fonts

Typewriter searches for system monospace fonts automatically. For a specific typewriter font, either:

- Place `typewriter.ttf` next to the executable
- Set the `TYPEWRITER_FONT` environment variable to a `.ttf` path

## License

MIT
