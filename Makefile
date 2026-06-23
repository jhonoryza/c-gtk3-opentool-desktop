# Makefile — Config Opener for Sublime Text
# macOS GTK3 C application

CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
GTK_CFLAGS  := $(shell pkg-config --cflags gtk+-3.0 sqlite3)
GTK_LIBS    := $(shell pkg-config --libs gtk+-3.0 sqlite3)
TARGET  := config-opener
SRCS    := main.c
OBJS    := main.o

.PHONY: all run clean install uninstall clangd clean-clangd app-bundle install-app uninstall-app icon clean-icon

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $^ $(GTK_LIBS)

main.o: main.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c -o $@ $<

# ── Generate .clangd for LSP support ──

clangd:
	@echo "Generating .clangd from pkg-config..."
	@echo "CompileFlags:" > .clangd
	@echo "    Add: [" >> .clangd
	@printf '%s\n' $(GTK_CFLAGS) | sed 's/-I/        "-I/g; s/ /",\n        "/g; s/$$/",/' >> .clangd
	@echo "        \"-std=c99\"," >> .clangd
	@echo "        \"-Wall\"," >> .clangd
	@echo "        \"-Wextra\"" >> .clangd
	@echo "    ]" >> .clangd
	@echo "✅ .clangd generated successfully!"
	@echo ""
	@echo "📝 Next steps:"
	@echo "   1. Restart LSP in Sublime Text: Cmd+Shift+P → LSP: Restart Server"
	@echo "   2. Open main.c to verify GTK includes work"

clean-clangd:
	@rm -f .clangd
	@echo "✅ .clangd removed"

# ── Generate compile_commands.json (better alternative) ──

compile-commands: check-compiledb
	@echo "Generating compile_commands.json..."
	@compiledb -n $(CC) $(SRCS) $(GTK_CFLAGS) $(GTK_LIBS)
	@echo "✅ compile_commands.json generated!"

check-compiledb:
	@which compiledb >/dev/null 2>&1 || { \
		echo "❌ compiledb not found. Install with:"; \
		echo "   pip install compiledb"; \
		exit 1; \
	}

# ── Development shortcuts ──

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

clean-all: clean clean-clangd clean-icon
	@echo "✅ All build artifacts, .clangd and AppIcon.icns removed"

# ── Installation (optional) ──

PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin

install: $(TARGET)
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)

# ── macOS App Bundle (Spotlight-launchable) ──

APP_NAME    := "Config Opener"
BUNDLE      := Config\ Opener.app
CONTENTS    := Config\ Opener.app/Contents
MACOS_DIR   := Config\ Opener.app/Contents/MacOS
RES_DIR     := Config\ Opener.app/Contents/Resources
APP_DEST    ?= /Applications

# Generate AppIcon.icns from logo-512.png using built-in macOS tools.
AppIcon.icns: logo-512.png
	@echo "Generating AppIcon.icns from logo-512.png..."
	@rm -rf AppIcon.iconset
	@mkdir -p AppIcon.iconset
	@sips -z 16   16   logo-512.png --out AppIcon.iconset/icon_16x16.png       >/dev/null
	@sips -z 32   32   logo-512.png --out AppIcon.iconset/icon_16x16@2x.png    >/dev/null
	@sips -z 32   32   logo-512.png --out AppIcon.iconset/icon_32x32.png       >/dev/null
	@sips -z 64   64   logo-512.png --out AppIcon.iconset/icon_32x32@2x.png    >/dev/null
	@sips -z 128  128  logo-512.png --out AppIcon.iconset/icon_128x128.png     >/dev/null
	@sips -z 256  256  logo-512.png --out AppIcon.iconset/icon_128x128@2x.png  >/dev/null
	@sips -z 256  256  logo-512.png --out AppIcon.iconset/icon_256x256.png     >/dev/null
	@sips -z 512  512  logo-512.png --out AppIcon.iconset/icon_256x256@2x.png  >/dev/null
	@sips -z 512  512  logo-512.png --out AppIcon.iconset/icon_512x512.png     >/dev/null
	@cp logo-512.png AppIcon.iconset/icon_512x512@2x.png
	@iconutil -c icns AppIcon.iconset -o AppIcon.icns
	@rm -rf AppIcon.iconset
	@echo "✅ AppIcon.icns generated"

icon: AppIcon.icns

clean-icon:
	@rm -rf AppIcon.icns AppIcon.iconset
	@echo "✅ AppIcon.icns removed"

app-bundle: $(TARGET) AppIcon.icns
	@echo "Creating app bundle..."
	@rm -rf "Config Opener.app"
	@mkdir -p "Config Opener.app/Contents/MacOS" "Config Opener.app/Contents/Resources"
	@cp $(TARGET) "Config Opener.app/Contents/MacOS/$(TARGET)"
	@cp Info.plist "Config Opener.app/Contents/Info.plist"
	@printf '#!/bin/bash\nexport PATH="/opt/homebrew/bin:/usr/local/bin:$$PATH"\nexport XDG_DATA_DIRS="/opt/homebrew/share:/usr/local/share:$${XDG_DATA_DIRS:-}"\nDIR="$$(cd "$$(dirname "$$0")" && pwd)"\nexec "$$DIR/$(TARGET)"\n' > "Config Opener.app/Contents/MacOS/launcher"
	@chmod +x "Config Opener.app/Contents/MacOS/launcher"
	@cp AppIcon.icns "Config Opener.app/Contents/Resources/AppIcon.icns"
	@cp logo-512.png "Config Opener.app/Contents/Resources/logo-512.png"
	@echo "✅ Config Opener.app created"

install-app: app-bundle
	@echo "Installing to $(APP_DEST)/Config Opener.app..."
	@rm -rf "/Applications/Config Opener.app"
	@cp -R "Config Opener.app" "/Applications/"
	@/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "/Applications/Config Opener.app" 2>/dev/null || true
	@echo "✅ Installed. Open from Spotlight: Config Opener"

uninstall-app:
	@echo "Removing /Applications/Config Opener.app..."
	@rm -rf "/Applications/Config Opener.app"
	@/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -u "/Applications/Config Opener.app" 2>/dev/null || true
	@echo "✅ Uninstalled"

# ── Debug build ──

debug: CFLAGS = -std=c99 -Wall -Wextra -g -O0 -DDEBUG
debug: clean all

# ── Dependency check ──

check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists gtk+-3.0 || { \
		echo "ERROR: GTK3 not found. Install with: brew install gtk+3 pkg-config"; \
		exit 1; \
	}
	@echo "  ✓ GTK3 found"
	@pkg-config --exists sqlite3 || { \
		echo "ERROR: sqlite3 not found. Install with: brew install sqlite3"; \
		exit 1; \
	}
	@echo "  ✓ sqlite3 found"
	@which subl >/dev/null 2>&1 || { \
		echo "WARNING: 'subl' not found in PATH."; \
		echo "  Install: ln -s /Applications/Sublime\\ Text.app/Contents/SharedSupport/bin/subl /usr/local/bin/subl"; \
	}
	@echo "  ✓ compiler: $(CC)"
	@echo "All dependencies satisfied."

# ── Complete setup for Sublime Text LSP ──

setup-lsp: check-deps clangd
	@echo ""
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo "✨ LSP setup complete for Sublime Text!"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo ""
	@echo "Next steps in Sublime Text:"
	@echo "  1. Cmd+Shift+P → LSP: Restart Server"
	@echo "  2. Select 'clangd' from the list"
	@echo "  3. Open main.c - GTK errors should be gone"
	@echo ""
	@echo "Troubleshooting if errors persist:"
	@echo "  • Check LSP settings: Preferences → Package Settings → LSP → Settings"
	@echo "  • Ensure clangd path: \"command\": [\"/opt/homebrew/bin/clangd\"]"
	@echo "  • View logs: Cmd+Shift+P → LSP: Show Logs"

# ── Help ──

help:
	@echo "Usage:"
	@echo "  make                Build the application"
	@echo "  make run            Build and run"
	@echo "  make debug          Build with debug symbols"
	@echo "  make clean          Remove build artifacts (obj, binary)"
	@echo "  make clean-all      Remove everything including .clangd"
	@echo "  make app-bundle      Create .app bundle (Spotlight-launchable)"
	@echo "  make install-app     Build bundle and install to /Applications"
	@echo "  make uninstall-app   Remove app from /Applications"
	@echo "  make install         Install binary to PREFIX ($(PREFIX))"
	@echo "  make uninstall       Remove installed binary"
	@echo "  make check-deps     Verify dependencies"
	@echo ""
	@echo "  ── LSP / Editor Support ──"
	@echo "  make clangd         Generate .clangd for clangd LSP"
	@echo "  make clean-clangd   Remove .clangd file"
	@echo "  make compile-commands  Generate compile_commands.json (requires compiledb)"
	@echo "  make setup-lsp      Complete LSP setup (check-deps + clangd)"
	@echo ""
	@echo "  make help           Show this help"