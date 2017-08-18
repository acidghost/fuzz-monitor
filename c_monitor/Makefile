CC=gcc
CFLAGS=-Wall -O3 -std=c11 -I.. -I../Collections-C/src/include
LDLIBS=-lzmq -L../perf -lperf -lm -L../Collections-C/build/src -l:libcollectc.a

BIN := fuzz-monitor

SRCS := $(sort $(wildcard *.c))
OBJS := $(SRCS:.c=.o)

graphs := graphs

ifeq ($(DEBUG), 1)
CFLAGS += -g
endif

.PHONY: clean graphs graphs-clean
all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $^ -o $@ $(LDLIBS)

graphs:
	for f in `ls $(graphs)`; do \
		echo $$f; dot -Tpdf $(graphs)/$$f -o $(graphs)/$$f.pdf; \
	done

graphs-clean:
	rm -rf $(graphs)/*.gv $(graphs)/*.pdf

clean:
	rm -rf $(BIN) $(OBJS)
