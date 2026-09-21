CXX ?= c++
CPPFLAGS += -Ibuild $(shell pkg-config --cflags sdl2 gl wayland-client egl gbm libdrm)
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic
LDLIBS += $(shell pkg-config --libs sdl2 gl wayland-client egl gbm libdrm)

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

build/omarchy-xr: src/environment.hpp src/graphics_limits.hpp src/spectator.hpp build/xdg-shell-client.h build/xdg-shell-protocol.o src/direct_output.cpp src/direct_output.hpp build/drm-lease-client.h build/drm-lease-protocol.o src/main.cpp src/capture_plan.hpp src/hover.hpp src/targeting.hpp src/camera_controls.hpp src/live_controls.hpp src/tracking.hpp src/pose_socket.hpp src/layout.hpp src/curvature.hpp src/spacing.hpp src/capture.cpp src/capture.hpp src/gpu_capture.hpp src/pixels.hpp build/linux-dmabuf-client.h build/linux-dmabuf-protocol.o build/wlr-screencopy-client.h build/wlr-screencopy-protocol.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp src/capture.cpp src/direct_output.cpp build/xdg-shell-protocol.o build/linux-dmabuf-protocol.o build/drm-lease-protocol.o build/wlr-screencopy-protocol.o -o $@ $(LDFLAGS) $(LDLIBS)

run: all
	./build/omarchy-xr

build/test-pixels: tests/pixels.cpp src/pixels.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@

build/test-curvature: tests/curvature.cpp src/curvature.hpp src/layout.hpp src/spacing.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@

build/test-tracking: tests/tracking.cpp src/tracking.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@

check: all build/test-pixels build/test-curvature build/test-tracking build/test-camera-controls build/test-targeting build/test-hover build/test-capture-plan
	./build/test-capture-plan
	./build/test-hover
	./build/test-targeting
	./build/test-camera-controls
	lua tests/controls.lua
	./build/test-tracking
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

build/drm-lease-client.h: protocols/drm-lease-v1.xml
	mkdir -p build
	wayland-scanner client-header $< $@
build/drm-lease-protocol.c: protocols/drm-lease-v1.xml
	mkdir -p build
	wayland-scanner private-code $< $@
build/drm-lease-protocol.o: build/drm-lease-protocol.c
	$(CC) $(shell pkg-config --cflags wayland-client) -c $< -o $@

.PHONY: install-helper uninstall-helper
# Run from a terminal; sudo prompts only for installation/upgrades.
install-helper:
	sudo /usr/bin/python3 -I scripts/install-helper.py

uninstall-helper:
	sudo /usr/bin/python3 -I scripts/install-helper.py --uninstall

.PHONY: install-controls
install-controls:
	python3 scripts/install-controls.py
	hyprctl reload
	hyprctl configerrors

build/test-camera-controls: tests/camera_controls.cpp src/camera_controls.hpp src/targeting.hpp src/live_controls.hpp src/spacing.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@

build/test-targeting: tests/targeting.cpp src/targeting.hpp src/camera_controls.hpp src/curvature.hpp src/tracking.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@

build/test-hover: tests/hover.cpp src/hover.hpp src/targeting.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@

build/linux-dmabuf-client.h: protocols/linux-dmabuf-unstable-v1.xml
	mkdir -p build
	wayland-scanner client-header $< $@
build/linux-dmabuf-protocol.c: protocols/linux-dmabuf-unstable-v1.xml
	mkdir -p build
	wayland-scanner private-code $< $@
build/linux-dmabuf-protocol.o: build/linux-dmabuf-protocol.c
	$(CC) $(shell pkg-config --cflags wayland-client) -c $< -o $@

build/test-capture-plan: tests/capture_plan.cpp src/capture_plan.hpp src/pixels.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc $< -o $@

# Opt-in hardware probe; not part of unattended checks (moves the real pointer).
build/capture-timing: tests/capture_timing.cpp src/capture.cpp src/capture.hpp src/gpu_capture.hpp src/pixels.hpp build/linux-dmabuf-client.h build/linux-dmabuf-protocol.o build/wlr-screencopy-client.h build/wlr-screencopy-protocol.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc tests/capture_timing.cpp src/capture.cpp build/linux-dmabuf-protocol.o build/wlr-screencopy-protocol.o -o $@ $(LDFLAGS) $(LDLIBS)

build/xdg-shell-client.h: protocols/xdg-shell.xml
	mkdir -p build
	wayland-scanner client-header $< $@
build/xdg-shell-protocol.c: protocols/xdg-shell.xml
	mkdir -p build
	wayland-scanner private-code $< $@
build/xdg-shell-protocol.o: build/xdg-shell-protocol.c
	$(CC) $(shell pkg-config --cflags wayland-client) -c $< -o $@

# Optional Qt UI regression suite (requires Qt 6.6+ declarative development tools).
QMLTESTRUNNER ?= /usr/lib/qt6/bin/qmltestrunner
check-ui:
	QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=basic QT_QUICK_CONTROLS_STYLE=Basic $(QMLTESTRUNNER) -input tests/qml -import studio
.PHONY: check-ui

# Requires a GL-capable graphical session (or xvfb-run on CI).
build/test-environment: tests/environment.cpp src/environment.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags sdl2 gl) -Isrc $< -o $@ $(shell pkg-config --libs sdl2 gl)

check-environment: build/test-environment
	./build/test-environment
