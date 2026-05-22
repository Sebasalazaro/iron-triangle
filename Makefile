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

# Roundtrip LZ77 + pipeline completo LZ77+RC4
test: $(TARGET)
	@bash tests/test_roundtrip.sh

# Unit test RC4 standalone (sin pipeline)
test-rc4:
	$(CC) $(CFLAGS) -DRC4_TEST -o rc4_test src/rc4.c
	./rc4_test
	@rm -f rc4_test

# Benchmark A (cp) vs B (LZ77) vs C (LZ77+RC4)
benchmark: $(TARGET)
	@bash benchmark/benchmark.sh

clean:
	rm -f $(OBJS) $(TARGET) rc4_test

.PHONY: all test test-rc4 benchmark clean
