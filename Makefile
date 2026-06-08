# Makefile — Config Opener for Sublime Text
# macOS GTK3 C application

CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
GTK_CFLAGS  := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS    := $(shell pkg-config --libs gtk+-3.0)
TARGET  := config-opener
SRCS    := main.c
OBJS    := main.o

.PHONY: all run clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $^ $(GTK_LIBS)

main.o: main.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c -o $@ $<

# ── Development shortcuts ──

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

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

# ── Help ──

help:
	@echo "Usage:"
	@echo "  make            Build the application"
	@echo "  make run        Build and run"
	@echo "  make debug      Build with debug symbols"
	@echo "  make clean      Remove build artifacts"
	@echo "  make install    Install to PREFIX ($(PREFIX))"
	@echo "  make uninstall  Remove installed binary"
	@echo "  make check-deps Verify dependencies"
	@echo "  make help       Show this help"
