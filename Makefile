dirs := perf preloads c_monitor


.PHONY: all $(dirs) rust clean
all: $(dirs)

$(dirs):
	$(MAKE) -C $@

rust:
	cargo build --release

clean:
	for d in $(dirs); do $(MAKE) -C $$d clean; done
