CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -Isrc
SRCS = src/main.c src/lexer.c src/ast.c src/parser.c src/value.c src/interpreter.c
OBJS = $(SRCS:.c=.o)
TARGET = prism

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

src/%.o: src/%.c src/lexer.h src/ast.h src/value.h src/parser.h src/interpreter.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o $(TARGET)

run: $(TARGET)
	./prism examples/hello.pm

.PHONY: all clean run
