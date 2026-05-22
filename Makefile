CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99
TARGET  = iron-triangle
SRCS    = src/main.c src/io.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test:
	@bash tests/test_roundtrip.sh

benchmark:
	@bash benchmark/benchmark.sh

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all test benchmark clean
