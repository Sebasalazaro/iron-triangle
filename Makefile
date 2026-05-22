CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99
TARGET  = iron-triangle
SRCS    = src/main.c src/io.c src/lz77.c src/rc4.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test:
	@bash tests/test_roundtrip.sh

test-rc4:
	$(CC) $(CFLAGS) -DRC4_TEST -o rc4_test src/rc4.c
	./rc4_test
	rm -f rc4_test

benchmark:
	@bash benchmark/benchmark.sh

clean:
	rm -f $(OBJS) $(TARGET) rc4_test

.PHONY: all test test-rc4 benchmark clean
