CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -Isrc -D_POSIX_C_SOURCE=200809L

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
    src/gui_native.c

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
    src/gui_native.h

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

src/%.o: src/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o $(TARGET)

run: $(TARGET)
	./prism examples/hello.pm

.PHONY: all clean run
