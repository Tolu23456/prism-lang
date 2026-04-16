CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -Isrc -D_POSIX_C_SOURCE=200809L

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
        src/interpreter.c \
        src/chunk.c \
        src/compiler.c \
        src/vm.c \
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
	rm -f src/*.o $(TARGET)

run: $(TARGET)
	./prism examples/hello.pm

.PHONY: all clean run
