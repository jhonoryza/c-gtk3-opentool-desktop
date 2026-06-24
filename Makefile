# Makefile — Opentool Desktop
# Linux GTK3 C application with terminal emulation and web views

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2
PKG_DEPS := gtk+-3.0 sqlite3 vte-2.91 webkit2gtk-4.1
GTK_CFLAGS  := $(shell pkg-config --cflags $(PKG_DEPS) 2>/dev/null)
GTK_LIBS    := $(shell pkg-config --libs $(PKG_DEPS) 2>/dev/null)
TARGET  := opentool
SRCS    := main.c
OBJS    := main.o

.PHONY: all run clean install uninstall clangd clean-clangd check-deps help debug

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
	@echo "        \"-std=c11\"," >> .clangd
	@echo "        \"-Wall\"," >> .clangd
	@echo "        \"-Wextra\"" >> .clangd
	@echo "    ]" >> .clangd
	@echo ".clangd generated"

clean-clangd:
	@rm -f .clangd

# ── Development shortcuts ──

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

# ── Installation ──

PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin
DATADIR := $(PREFIX)/share/opentool
APPDIR  := $(PREFIX)/share/applications

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(DATADIR)
	$(if $(wildcard logo-512.png),install -m 644 logo-512.png $(DESTDIR)$(DATADIR)/logo-512.png,)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)$(DATADIR)
	rm -f $(DESTDIR)$(APPDIR)/opentool.desktop

# ── Debug build ──

debug: CFLAGS = -std=c11 -Wall -Wextra -g -O0 -DDEBUG
debug: clean all

# ── Dependency check ──

check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists gtk+-3.0 || { \
		echo "ERROR: GTK3 not found. Install: sudo apt install libgtk-3-dev"; \
		exit 1; \
	}
	@echo "  gtk+-3.0"
	@pkg-config --exists sqlite3 || { \
		echo "ERROR: sqlite3 not found. Install: sudo apt install libsqlite3-dev"; \
		exit 1; \
	}
	@echo "  sqlite3"
	@pkg-config --exists vte-2.91 || { \
		echo "ERROR: libvte not found. Install: sudo apt install libvte-2.91-dev"; \
		exit 1; \
	}
	@echo "  vte-2.91"
	@pkg-config --exists webkit2gtk-4.1 || pkg-config --exists webkit2gtk-4.0 || { \
		echo "ERROR: webkit2gtk not found. Install: sudo apt install libwebkit2gtk-4.1-dev (or 4.0-dev)"; \
		exit 1; \
	}
	@echo "  webkit2gtk"
	@echo "  compiler: $(CC)"
	@echo "All dependencies satisfied."

# ── Help ──

help:
	@echo "Usage:"
	@echo "  make              Build the application"
	@echo "  make run          Build and run"
	@echo "  make debug        Build with debug symbols"
	@echo "  make clean        Remove build artifacts"
	@echo "  make install      Install to PREFIX ($(PREFIX))"
	@echo "  make uninstall    Remove installed files"
	@echo "  make check-deps   Verify dependencies"
	@echo "  make clangd       Generate .clangd for LSP"
	@echo "  make help         Show this help"
