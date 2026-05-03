CC      = gcc
# Default: optimised release build.  Use  make debug  for a debug build.
#
# Root cause of the ASLR-dependent SIGSEGV:
#   GCC's -fgcse (Global Common Subexpression Elimination) pass, enabled at
#   -O2+, misoptimises env_get / env_set / env_free and the parser in
#   interpreter.c, parser.c, and builtins.c by hoisting or eliminating memory
#   loads/stores that alias through the Env* pointer chain, producing incorrect
#   code that crashes at certain heap layouts.  Adding -fno-gcse globally
#   disables that pass while keeping all other -O2 optimisations (inlining,
#   constant folding, vectorisation, etc.).
#
#   vm.c uses -O1 separately: GCC's -O2 sibling-call / stack-slot-reuse passes
#   misoptimise the computed-goto dispatch loop, causing a separate class of
#   intermittent crashes.  -O1 avoids those without hurting dispatch speed.
VM_CFLAGS_EXTRA = -O1
CFLAGS  = -Wall -Wextra -std=c11 -O2 -fno-gcse -DNDEBUG -march=native \
          -fomit-frame-pointer -fno-strict-aliasing \
          -Isrc -D_POSIX_C_SOURCE=200809L
PREFIX  = /usr/local
BINDIR  = $(DESTDIR)$(PREFIX)/bin

# Detect X11 / Xft availability via pkg-config
X11_CFLAGS  := $(shell pkg-config --cflags x11 xft xrender fontconfig 2>/dev/null)
X11_LDFLAGS := $(shell pkg-config --libs   x11 xft xrender fontconfig 2>/dev/null)

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
        \
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

# vm.c gets its own rule: drop to -O1 to avoid computed-goto misoptimisation
src/vm.o: src/vm.c $(HEADERS)
	$(CC) $(filter-out -O2,$(CFLAGS)) $(VM_CFLAGS_EXTRA) -c -o $@ $<

src/%.o: src/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

debug: clean
	$(CC) -Wall -Wextra -std=c11 -g -Isrc -D_POSIX_C_SOURCE=200809L \
        $(X11_CFLAGS) $(if $(filter yes,$(shell pkg-config --exists x11 xft xrender fontconfig 2>/dev/null && echo yes)),-DHAVE_X11,) \
        -o prism-debug $(SRCS) -lm $(LDFLAGS)
	@echo "Debug build: prism-debug"

clean:
	rm -f src/*.o $(TARGET) prism-san prism-debug

run: $(TARGET)
	./prism examples/hello.pr

test: $(TARGET)
	@chmod +x tests/run_tests.sh
	@bash tests/run_tests.sh

# AddressSanitizer + UndefinedBehaviorSanitizer build.
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

# Profile-guided optimisation convenience targets
pgo-gen: $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -O2 -fprofile-generate -o prism-pgo $(SRCS) -lm $(LDFLAGS)
	@echo "Run your workload with ./prism-pgo, then 'make pgo-use'"
pgo-use: $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -O2 -fprofile-use -fprofile-correction -o prism-pgo-opt $(SRCS) -lm $(LDFLAGS)
	@echo "PGO optimised build: prism-pgo-opt"

.PHONY: all clean run test install uninstall sanitize pgo-gen pgo-use
