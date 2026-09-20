CXX ?= c++
CPPFLAGS += -Ibuild $(shell pkg-config --cflags sdl2 gl wayland-client)
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic
LDLIBS += $(shell pkg-config --libs sdl2 gl wayland-client)

.PHONY: all run check smoke clean install-studio studio
all: build/omarchy-xr

build/wlr-screencopy-client.h: protocols/wlr-screencopy-unstable-v1.xml
	mkdir -p build
	wayland-scanner client-header $< $@

build/wlr-screencopy-protocol.c: protocols/wlr-screencopy-unstable-v1.xml
	mkdir -p build
	wayland-scanner private-code $< $@

build/wlr-screencopy-protocol.o: build/wlr-screencopy-protocol.c
	$(CC) $(shell pkg-config --cflags wayland-client) -c $< -o $@

build/omarchy-xr: src/main.cpp src/layout.hpp src/curvature.hpp src/spacing.hpp src/capture.cpp src/capture.hpp src/pixels.hpp build/wlr-screencopy-client.h build/wlr-screencopy-protocol.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp src/capture.cpp build/wlr-screencopy-protocol.o -o $@ $(LDFLAGS) $(LDLIBS)

run: all
	./build/omarchy-xr

build/test-pixels: tests/pixels.cpp src/pixels.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@

build/test-curvature: tests/curvature.cpp src/curvature.hpp src/layout.hpp src/spacing.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@

check: all build/test-pixels build/test-curvature
	./build/test-curvature
	python3 -m unittest discover -s tests -p 'test_*.py'
	./build/test-pixels
	./build/omarchy-xr --help
	./build/omarchy-xr --version
	! ./build/omarchy-xr --invalid-option
	! ./build/omarchy-xr --capture

# Requires a graphical session, or xvfb-run on CI.
smoke: all
	./build/omarchy-xr --smoke-test

clean:
	rm -rf build

install-studio: all
	python3 scripts/install-studio.py

studio:
	omarchy-shell shell summon afruth.omarchy-xr '{}'
