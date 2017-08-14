CC=gcc
CFLAGS=-Wall -O3 -std=c11 -I.. -I../Collections-C/src/include
LDLIBS=-lzmq -L../perf -lperf -lm -L../Collections-C/build/src -l:libcollectc.a

BIN := fuzz-monitor

SRCS := $(sort $(wildcard *.c))
OBJS := $(SRCS:.c=.o)

ifeq ($(DEBUG), 1)
CFLAGS += -g
endif

.PHONY: clean
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $^ -o $@ $(LDLIBS)

clean:
	rm -rf $(BIN) $(OBJS)
