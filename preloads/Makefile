CC = gcc
CFLAGS = -Wall -fPIC -shared -O3
LDLIBS = -ldl -lm -lzmq

preloads := afl.so hongg.so

all: $(preloads)

%.so: preload.c
	$(CC) $(CFLAGS) -o $@ $< -D `echo $* | tr a-z A-Z` -D FUZZ=$* $(LDLIBS)

clean:
	rm -rf $(preloads)
