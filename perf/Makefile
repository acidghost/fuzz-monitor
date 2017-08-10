CC = gcc
CFLAGS = -Wall -O3 -std=c11

BIN := perf

SRCS := $(sort $(wildcard *.c))
OBJS := $(SRCS:.c=.o)

all: $(BIN)

$(BIN): $(OBJS)

clean:
	rm -rf $(BIN) $(OBJS)
