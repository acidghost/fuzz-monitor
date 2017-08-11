CC = gcc
CFLAGS += -fPIC -Wall -O3 -std=c11

BIN := perf
LIB := libperf.a

SRCS := $(sort $(wildcard *.c))
OBJS := $(SRCS:.c=.o)
LIBDEPS := perf.o log.o

all: $(BIN) $(LIB)

$(BIN): $(OBJS)

$(LIB): $(LIBDEPS)
	ar rc $@ $^

clean:
	rm -rf $(BIN) $(OBJS) $(LIB)
