# Typewriter

A distraction-free text editor with the soul of a mechanical typewriter.

Built in C with SDL2 — fast, lightweight, cross-platform. Every keystroke clicks. Every carriage return clunks. The paper is warm, the cursor blinks, and the bell rings at column 80.

## Features

- **Real typewriter sounds** — recorded mechanical key strikes, embedded directly in the binary. Supports concurrent/overlapping playback for a true tactile feel.
- **Paper aesthetic** — Multiple themes including Classic Cream, Dark Mode, and Terminal Green.
- **Lightweight** — single C file, ~750KB binary, near-zero CPU at idle.
- **Cross-platform** — Windows, macOS, Linux.
- **No dependencies at runtime** — everything is embedded (sounds, icon).
- **Find & Replace** — Built-in search and replace functionality with a custom dialog (`Ctrl+F`).
- **Settings Persistence** — Automatically saves your preferences (sounds, line numbers, theme) to `typewriter.ini`.
- **File Support** — Open and save `.txt`, `.md`, and `.ini` files using native system dialogs.
- **Drag and drop** — drop a file onto the window to open it.
- **Undo** — `Ctrl+Z` with a 512-entry history.
- **Options menu** — `Ctrl+K` to toggle sounds, line numbers, notebook lines, and cycle themes.

## Build

Requires SDL2 and SDL2_ttf development libraries.

**Windows (MSYS2/MinGW)**

```sh
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_ttf
mingw32-make
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
| **Ctrl+S** | Save |
| **Ctrl+O** | Open |
| **Ctrl+Q** | Quit |
| **Ctrl+Z** | Undo |
| **Ctrl+F** | Find & Replace |
| **Ctrl+K** | Options menu |
| **Ctrl+C / X / V** | Copy / Cut / Paste |
| **Ctrl+A** | Select all |
| **Esc** | Clear selection / Close dialog |

### Options Menu (`Ctrl+K`)
- **Up/Down**: Navigate options
- **Left/Right / Space / Enter**: Cycle themes or toggle options
- **Esc**: Close menu

### Find & Replace (`Ctrl+F`)
- **Tab**: Switch between Find and Replace fields
- **Enter**: Find next occurrence
- **Shift+Enter**: Replace current match
- **Esc**: Close dialog

### Save Dialog
- **S**: Save and quit
- **D / N**: Don't save and quit
- **C / Esc**: Cancel

## Fonts

Typewriter searches for system monospace fonts automatically. For a specific typewriter font, either:

- Place `typewriter.ttf` next to the executable
- Set the `TYPEWRITER_FONT` environment variable to a `.ttf` path

## License

MIT
