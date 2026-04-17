CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g -Isrc -D_POSIX_C_SOURCE=200809L
PREFIX  = /usr/local
BINDIR  = $(DESTDIR)$(PREFIX)/bin

# Detect X11 / Xft availability via pkg-config
X11_CFLAGS  := $(shell pkg-config --cflags x11 xft fontconfig 2>/dev/null)
X11_LDFLAGS := $(shell pkg-config --libs   x11 xft fontconfig 2>/dev/null)

ifneq ($(X11_CFLAGS),)
  CFLAGS  += -DHAVE_X11 $(X11_CFLAGS)
  LDFLAGS  = $(X11_LDFLAGS)
  X11_SRCS = src/pss.c src/xgui.c
else
  LDFLAGS  =
  X11_SRCS =
endif

SRCS = \
	src/main.c \
	src/lexer.c \
	src/ast.c \
	src/parser.c \
	src/value.c \
	src/gc.c \
	src/builtins.c \
	src/interpreter.c \
	src/chunk.c \
	src/compiler.c \
	src/vm.c \
	src/jit.c \
	src/transpiler.c \
	src/gui_native.c \
	src/formatter.c \
	$(X11_SRCS)

OBJS   = $(SRCS:.c=.o)
TARGET = prism

HEADERS = \
	src/lexer.h \
	src/ast.h \
	src/value.h \
	src/gc.h \
	src/parser.h \
	src/interpreter.h \
	src/chunk.h \
	src/compiler.h \
	src/vm.h \
	src/jit.h \
	src/transpiler.h \
	src/opcode.h \
	src/gui_native.h \
	src/formatter.h \
	src/pss.h \
	src/xgui.h

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm $(LDFLAGS)

src/%.o: src/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o $(TARGET) prism-san

run: $(TARGET)
	./prism examples/hello.pr

test: $(TARGET)
	@chmod +x tests/run_tests.sh
	@bash tests/run_tests.sh

# Item 5: AddressSanitizer + LeakSanitizer + UndefinedBehaviorSanitizer build.
# On Linux, ASan automatically includes LSan.
# Usage:  make sanitize && ASAN_OPTIONS=detect_leaks=1 ./prism-san examples/hello.pr
SAN_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer -g
sanitize: $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) $(SAN_FLAGS) -o prism-san $(SRCS) -lm $(LDFLAGS)
	@echo "Sanitizer build: prism-san"
	@echo "Run: ASAN_OPTIONS=detect_leaks=1 ./prism-san examples/hello.pr"

install: $(TARGET)
	@mkdir -p $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)
	@echo "Installed prism -> $(BINDIR)/$(TARGET)"

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	@echo "Removed $(BINDIR)/$(TARGET)"

# Release build: maximum optimisation, no debug info, no assertions
RELEASE_FLAGS = -O3 -DNDEBUG -march=native -fomit-frame-pointer
release: $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -o prism-release $(SRCS) -lm $(LDFLAGS)
	@echo "Release build: prism-release  ($(CC) -O3 -DNDEBUG -march=native)"

# Profile-guided optimisation convenience targets
pgo-gen: $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -O2 -fprofile-generate -o prism-pgo $(SRCS) -lm $(LDFLAGS)
	@echo "Run your workload with ./prism-pgo, then 'make pgo-use'"
pgo-use: $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -O3 -DNDEBUG -fprofile-use -fprofile-correction -o prism-pgo-opt $(SRCS) -lm $(LDFLAGS)
	@echo "PGO optimised build: prism-pgo-opt"

.PHONY: all clean run test install uninstall sanitize release pgo-gen pgo-use
