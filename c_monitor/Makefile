CC=gcc
CFLAGS=-Wall -O3 -std=c11 -I..
LDLIBS=-lzmq -L../perf -lperf -lm

BIN := fuzz-monitor

SRCS := $(sort $(wildcard *.c))
OBJS := $(SRCS:.c=.o)

.PHONY: clean
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $^ -o $@ $(LDLIBS)

clean:
	rm -rf $(BIN) $(OBJS)
