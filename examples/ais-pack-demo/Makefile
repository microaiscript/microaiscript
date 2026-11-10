# Demo native module + ais-pack smoke targets (run from repo root or here).
AIS_ROOT := $(abspath ../..)
CC ?= cc
CFLAGS := -Wall -Wextra -std=gnu11 -fPIC -shared -I$(AIS_ROOT)/include

.PHONY: all native clean pack unpack exe

all: native

lib/demo_native.so: native/demo_native.c | lib
	$(CC) $(CFLAGS) -o $@ $<

lib:
	mkdir -p lib

native: lib/demo_native.so

pack: native
	$(AIS_ROOT)/utilities/ais-pack/ais-pack pack -m ais-pack.toml -o demo.aiz

unpack: pack
	$(AIS_ROOT)/utilities/ais-pack/ais-pack unpack -i demo.aiz -d _unpack

exe: native
	cd $(AIS_ROOT) && $(MAKE) lib && utilities/ais-pack/ais-pack build-exe \
	  -m examples/ais-pack-demo/ais-pack.toml \
	  -o examples/ais-pack-demo/demo-packed

clean:
	rm -rf lib _unpack demo.aiz demo-packed
