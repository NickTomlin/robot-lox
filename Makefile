CC      := gcc
CFLAGS  := -Wall -Wextra -std=c17 -g -Iinclude
SRCS    := $(wildcard src/*.c)
OBJS    := $(patsubst src/%.c, build/obj/%.o, $(SRCS))
TARGET  := build/lox

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

build/obj/%.o: src/%.c
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	@bash tests/run_tests.sh

clean:
	rm -rf build
