# Opentool Desktop

A GTK3 desktop application (pure C, C11) for Linux that manages CLI sessions, terminals, and developer tools from one unified interface. Ported from the macOS-native `platform-ai` (Swift/SwiftUI) to C/GTK3.

![Platform](https://img.shields.io/badge/platform-Linux-blue)
![Language](https://img.shields.io/badge/language-C11-orange)
![GTK](https://img.shields.io/badge/GTK-3-green)
![License](https://img.shields.io/badge/license-MIT-green)

## Features (12 tabs)

| Tab | Description |
|-----|-------------|
| **Home** | Dashboard with shortcut grid and keyboard shortcuts reference |
| **OpenCode** | Session manager reading `~/.local/share/opencode/opencode.db` |
| **Claude** | JSONL indexer for `~/.claude/projects/` with SQLite cache |
| **SSH** | Connection CRUD + import from `~/.ssh/config`, grouped display |
| **Terminal** | Embedded PTY terminal using libvte with quick-launch for tools |
| **Chat Web** | Embedded webkit2gtk browser for AI chat providers |
| **Kimchi** | Session indexer for `~/.local/share/kimchi/projects/` |
| **Mimo** | Shell wrapper running `mimo session list --format json` |
| **Freebuff** | Directory scanner for `~/.config/manicode/projects/` |
| **Port Monitor** | Parses `ss -tlnp` output for active TCP listening ports |
| **Config Opener** | Manage config file shortcuts, open with `$EDITOR` |
| **Claude Switcher** | Manage multiple Claude API accounts (settings.json writer) |

### Command Palette
Press `Ctrl+P` for fuzzy search navigation across all tabs.

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+P` | Command Palette |
| `Ctrl+K` | Go to Home |
| `Ctrl+W` | Close window |
| `Ctrl+Q` | Quit application |

## Requirements

- **Linux** (tested on Ubuntu/Debian)
- **GCC** with C11 support
- **GTK3** (`libgtk-3-dev`)
- **SQLite3** (`libsqlite3-dev`)
- **VTE** terminal library (`libvte-2.91-dev`)
- **WebKitGTK** (`libwebkit2gtk-4.1-dev` or `libwebkit2gtk-4.0-dev`)
- **pkg-config**

### Install dependencies (Debian/Ubuntu)

```bash
sudo apt install build-essential pkg-config \
    libgtk-3-dev libsqlite3-dev \
    libvte-2.91-dev libwebkit2gtk-4.1-dev
```

### Install dependencies (Arch Linux)

```bash
sudo pacman -S base-devel pkgconf \
    gtk3 sqlite vte webkitgtk
```

## Build & Run

```bash
make check-deps  # verify dependencies
make             # build
make run         # build and run
make debug       # build with debug symbols (-g -O0)
make clean       # remove build artifacts
make install     # install to /usr/local (or PREFIX=...)
make uninstall   # remove installed files
```

## Installation

```bash
make && sudo make install
```

This installs:
- Binary: `/usr/local/bin/opentool`
- Icon: `/usr/local/share/opentool/logo-512.png`

## Project Structure

```
opentool-desktop/
├── main.c           # All application code (single file, ~5000 lines)
├── Makefile         # Build system with dependency detection
├── Info.plist       # (legacy, ignored on Linux)
├── logo-512.png     # Application icon
├── README.md        # This file
└── LICENSE          # MIT License
```

## Technical Details

| Aspect | Detail |
|--------|--------|
| Language | C11 |
| GUI Toolkit | GTK 3.24+ |
| Terminal | libvte-2.91 (VteTerminal) |
| Web View | webkit2gtk-4.1 |
| Database | SQLite3 (4 separate DBs) |
| UI Builder | Programmatic (no Glade/XML) |
| Processes | `fork()` + `execlp()`, `g_spawn_command_line_sync()` |
| Theme | Custom CSS with light/dark mode toggle |
| Data Dir | `$XDG_DATA_HOME/opentool/` (fallback `~/.local/share/opentool/`) |

### Database Files

| Database | Location | Purpose |
|----------|----------|---------|
| `accounts.db` | `$XDG_DATA_HOME/opentool/` | Claude switcher accounts + plugins |
| `claude_index.db` | `$XDG_DATA_HOME/opentool/` | Indexed Claude JSONL sessions |
| `ssh.db` | `$XDG_DATA_HOME/opentool/` | SSH connection records |
| `chat_providers.db` | `$XDG_DATA_HOME/opentool/` | AI chat web providers |

### External Paths Read

| Path | Feature |
|------|---------|
| `~/.local/share/opencode/opencode.db` | OpenCode session list |
| `~/.claude/projects/` | Claude JSONL scan |
| `~/.claude/settings.json` | Claude Switcher (write) |
| `~/.ssh/config` | SSH import |
| `~/.local/share/kimchi/projects/` | Kimchi JSONL scan |
| `~/.config/manicode/projects/` | Freebuff directory scan |

## License

MIT License - see [LICENSE](LICENSE) for details.
