# Makefile — Config Opener for Sublime Text
# macOS GTK3 C application

CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
GTK_CFLAGS  := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS    := $(shell pkg-config --libs gtk+-3.0)
TARGET  := config-opener
SRCS    := main.c
OBJS    := main.o

.PHONY: all run clean install uninstall clangd clean-clangd

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

clean-all: clean clean-clangd
	@echo "✅ All build artifacts and .clangd removed"

# ── Installation (optional) ──

PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin

install: $(TARGET)
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)

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
	@echo "  make install        Install to PREFIX ($(PREFIX))"
	@echo "  make uninstall      Remove installed binary"
	@echo "  make check-deps     Verify dependencies"
	@echo ""
	@echo "  ── LSP / Editor Support ──"
	@echo "  make clangd         Generate .clangd for clangd LSP"
	@echo "  make clean-clangd   Remove .clangd file"
	@echo "  make compile-commands  Generate compile_commands.json (requires compiledb)"
	@echo "  make setup-lsp      Complete LSP setup (check-deps + clangd)"
	@echo ""
	@echo "  make help           Show this help"