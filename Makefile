CXX ?= c++
CPPFLAGS += $(shell pkg-config --cflags sdl2 gl)
CXXFLAGS ?= -O0 -g
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic
LDLIBS += $(shell pkg-config --libs sdl2 gl)

.PHONY: all run check smoke clean
all: build/omarchy-xr

build/omarchy-xr: src/main.cpp
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

run: all
	./build/omarchy-xr

check: all
	./build/omarchy-xr --help
	./build/omarchy-xr --version
	! ./build/omarchy-xr --invalid-option

# Requires a graphical session, or xvfb-run on CI.
smoke: all
	./build/omarchy-xr --smoke-test

clean:
	rm -rf build
