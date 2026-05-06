CC      = gcc
# Default: optimised release build.  Use  make debug  for a debug build.
#
# Root cause of the ASLR-dependent SIGSEGV:
#   GCC's -fgcse (Global Common Subexpression Elimination) pass, enabled at
#   -O2+, misoptimises env_get / env_set / env_free and the parser in
#   interpreter.c, parser.c, and builtins.c by hoisting or eliminating memory
#   loads/stores that alias through the Env* pointer chain, producing incorrect
#   code that crashes at certain heap layouts.  Adding -fno-gcse globally
#   disables that pass while keeping all other -O3 optimisations (inlining,
#   constant folding, vectorisation, etc.).
#
#   vm.c uses its own flags: GCC's sibling-call / stack-slot-reuse passes
#   misoptimise the computed-goto dispatch loop.  -fno-optimize-sibling-calls
#   and -fstack-reuse=none prevent those without hurting dispatch speed.
VM_CFLAGS_EXTRA = -O2 -fno-optimize-sibling-calls -fstack-reuse=none
# Enhanced optimization flags for maximum performance
CFLAGS  = -Wall -Wextra -std=c11 -O3 -fno-gcse -DNDEBUG -march=native \
	  -fomit-frame-pointer -fno-strict-aliasing -finline-functions \
	  -flto -fwhole-program -funroll-loops -fprefetch-loop-arrays \
	  -fvect-cost-model=dynamic -ffunction-sections -fdata-sections \
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
	$(CC) $(CFLAGS) -o $@ $^ -lm $(LDFLAGS) -Wl,--gc-sections -Wl,--as-needed -flto

# vm.c gets its own rule: targeted flags to avoid computed-goto misoptimisation
src/vm.o: src/vm.c $(HEADERS)
	$(CC) $(filter-out -O3,$(CFLAGS)) $(VM_CFLAGS_EXTRA) -c -o $@ $<

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

# LTO (Link-Time Optimisation) build — best cross-file inlining, ~10-25% faster.
# Keeps per-file safety flags so vm.c computed-goto is not misoptimised.
lto: $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -flto -fno-gcse -o prism-lto \
	    $(filter-out src/vm.c,$(SRCS)) \
	    -c -flto $(VM_CFLAGS_EXTRA) src/vm.c -o src/vm-lto.o 2>/dev/null || \
	$(CC) $(CFLAGS) -flto -fno-gcse -o prism-lto $(SRCS) -lm $(LDFLAGS)
	@echo "LTO build: prism-lto"

# Simpler combined LTO target
lto-simple: $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -flto -fno-gcse -fno-optimize-sibling-calls -fstack-reuse=none \
	      -o prism-lto $(SRCS) -lm $(LDFLAGS)
	@echo "LTO build: prism-lto"

# Profile-guided optimisation convenience targets
# Usage: make pgo-train  (builds instrumented binary, runs training workload)
#        make pgo-use    (builds optimised binary using collected profile data)
pgo-gen: $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -fprofile-generate -o prism-pgo $(SRCS) -lm $(LDFLAGS)
	@echo "Instrumented build: prism-pgo"
	@echo "Run: ./prism-pgo benchmarks/fib_recursive.pr && ./prism-pgo benchmarks/loop_count.pr && ./prism-pgo benchmarks/sieve.pr"
	@echo "Then: make pgo-use"
pgo-use: $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -fprofile-use -fprofile-correction -o prism-pgo-opt $(SRCS) -lm $(LDFLAGS)
	@echo "PGO-optimised build: prism-pgo-opt"
pgo-train: pgo-gen
	./prism-pgo --vm benchmarks/fib_recursive.pr 2>/dev/null || true
	./prism-pgo --vm benchmarks/loop_count.pr    2>/dev/null || true
	./prism-pgo --vm benchmarks/sieve.pr         2>/dev/null || true
	./prism-pgo --vm benchmarks/ackermann.pr     2>/dev/null || true
	./prism-pgo --vm benchmarks/fib_iterative.pr 2>/dev/null || true
	@echo "Training done — run: make pgo-use"

# Benchmark convenience target
bench: $(TARGET)
	@echo "=== fib(32) recursive ==="
	@time ./prism --vm benchmarks/fib_recursive.pr
	@echo "=== loop 5M ==="
	@time ./prism --vm benchmarks/loop_count.pr
	@echo "=== ackermann(3,9) ==="
	@time ./prism --vm benchmarks/ackermann.pr
	@echo "=== sieve(1000000) ==="
	@time ./prism --vm benchmarks/sieve.pr

.PHONY: all clean run test install uninstall sanitize lto lto-simple pgo-gen pgo-use pgo-train bench
