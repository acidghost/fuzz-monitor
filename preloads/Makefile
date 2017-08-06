libs := afl.so

all: $(libs)

%.so: %.c
	$(CC) -Wall -fPIC -shared -o $@ $< -ldl -lm -lzmq

clean:
	rm -rf $(libs)
