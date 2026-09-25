CXX ?= c++
CC ?= cc
BUILD ?= build
CPPFLAGS += -I$(BUILD) $(shell pkg-config --cflags sdl2 gl wayland-client egl gbm libdrm pangocairo json-c)
CXXFLAGS ?= -O2 -g
override CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Werror -pthread
DEPFLAGS = -MMD -MP
WL_CFLAGS := $(shell pkg-config --cflags wayland-client)
LDLIBS += $(shell pkg-config --libs sdl2 gl wayland-client egl gbm libdrm pangocairo json-c)

APP_OBJS = $(BUILD)/main.o $(BUILD)/capture.o $(BUILD)/direct_output.o \
	$(BUILD)/xdg-shell-protocol.o $(BUILD)/linux-dmabuf-protocol.o \
	$(BUILD)/drm-lease-protocol.o $(BUILD)/wlr-screencopy-protocol.o \
	$(BUILD)/window_capture.o $(BUILD)/hyprland-toplevel-export-protocol.o $(BUILD)/wlr-foreign-toplevel-protocol.o
GEN_HEADERS = $(BUILD)/xdg-shell-client.h $(BUILD)/linux-dmabuf-client.h \
	$(BUILD)/drm-lease-client.h $(BUILD)/wlr-screencopy-client.h $(BUILD)/hyprland-toplevel-export-client.h
UNIT_BINS = $(BUILD)/test-pixels $(BUILD)/test-curvature $(BUILD)/test-tracking \
	$(BUILD)/test-camera-controls $(BUILD)/test-targeting $(BUILD)/test-hover \
	$(BUILD)/test-capture-plan $(BUILD)/test-vblank $(BUILD)/test-load-governor $(BUILD)/test-sky-cull $(BUILD)/test-dwell \
	$(BUILD)/test-frame-source $(BUILD)/test-canvas-model $(BUILD)/test-window-list $(BUILD)/test-canvas-placement \
	$(BUILD)/test-canvas-memory $(BUILD)/test-capture-cadence $(BUILD)/test-capture-governor
UNIT_OBJS = $(UNIT_BINS:%=%.o)

.PHONY: all run check run-units check-san smoke clean install-studio studio compile_commands.json spike-canvas
all: $(BUILD)/omarchy-xr compile_commands.json

# $(1) source, $(2) object, $(3) extra compiler flags. Sidecar JSON feeds compile_commands.json.
define compile_cxx
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(3) $(DEPFLAGS) -c $(1) -o $(2)
	@mkdir -p $(BUILD)/cc
	@printf '%s\n' '{"directory":"$(CURDIR)","file":"$(abspath $(1))","command":"$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(3) -c $(abspath $(1)) -o $(abspath $(2))"}' > $(BUILD)/cc/$(notdir $(2)).json
endef
define compile_c
	$(CC) $(WL_CFLAGS) $(DEPFLAGS) -c $(1) -o $(2)
	@mkdir -p $(BUILD)/cc
	@printf '%s\n' '{"directory":"$(CURDIR)","file":"$(abspath $(1))","command":"$(CC) $(WL_CFLAGS) -c $(abspath $(1)) -o $(abspath $(2))"}' > $(BUILD)/cc/$(notdir $(2)).json
endef

$(BUILD):
	mkdir -p $@

$(BUILD)/omarchy-xr: $(APP_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD)/main.o: src/main.cpp $(GEN_HEADERS) | $(BUILD)
	$(call compile_cxx,src/main.cpp,$(BUILD)/main.o,)
$(BUILD)/capture.o: src/capture.cpp $(GEN_HEADERS) | $(BUILD)
	$(call compile_cxx,src/capture.cpp,$(BUILD)/capture.o,)
$(BUILD)/direct_output.o: src/direct_output.cpp $(GEN_HEADERS) | $(BUILD)
	$(call compile_cxx,src/direct_output.cpp,$(BUILD)/direct_output.o,)
$(BUILD)/window_capture.o: src/window_capture.cpp $(GEN_HEADERS) | $(BUILD)
	$(call compile_cxx,src/window_capture.cpp,$(BUILD)/window_capture.o,)

$(BUILD)/wlr-screencopy-client.h: protocols/wlr-screencopy-unstable-v1.xml | $(BUILD)
	wayland-scanner client-header $< $@
$(BUILD)/wlr-screencopy-protocol.c: protocols/wlr-screencopy-unstable-v1.xml | $(BUILD)
	wayland-scanner private-code $< $@
$(BUILD)/wlr-screencopy-protocol.o: $(BUILD)/wlr-screencopy-protocol.c | $(BUILD)
	$(call compile_c,$(BUILD)/wlr-screencopy-protocol.c,$(BUILD)/wlr-screencopy-protocol.o)

$(BUILD)/hyprland-toplevel-export-client.h: protocols/hyprland-toplevel-export-v1.xml | $(BUILD)
	wayland-scanner client-header $< $@
$(BUILD)/hyprland-toplevel-export-protocol.c: protocols/hyprland-toplevel-export-v1.xml | $(BUILD)
	wayland-scanner private-code $< $@
$(BUILD)/hyprland-toplevel-export-protocol.o: $(BUILD)/hyprland-toplevel-export-protocol.c | $(BUILD)
	$(call compile_c,$(BUILD)/hyprland-toplevel-export-protocol.c,$(BUILD)/hyprland-toplevel-export-protocol.o)

$(BUILD)/wlr-foreign-toplevel-protocol.c: protocols/wlr-foreign-toplevel-management-unstable-v1.xml | $(BUILD)
	wayland-scanner private-code $< $@
$(BUILD)/wlr-foreign-toplevel-protocol.o: $(BUILD)/wlr-foreign-toplevel-protocol.c | $(BUILD)
	$(call compile_c,$(BUILD)/wlr-foreign-toplevel-protocol.c,$(BUILD)/wlr-foreign-toplevel-protocol.o)

# Window canvas M0 spike (docs/infinite-canvas-plan.md §3.6); not part of all/check.
spike-canvas: $(BUILD)/spike-window-capture
$(BUILD)/spike-window-capture.o: tools/spike_window_capture.cpp $(BUILD)/hyprland-toplevel-export-client.h $(BUILD)/linux-dmabuf-client.h | $(BUILD)
	$(call compile_cxx,tools/spike_window_capture.cpp,$(BUILD)/spike-window-capture.o,-Isrc)
$(BUILD)/spike-window-capture: $(BUILD)/spike-window-capture.o $(BUILD)/hyprland-toplevel-export-protocol.o \
	$(BUILD)/wlr-foreign-toplevel-protocol.o $(BUILD)/linux-dmabuf-protocol.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD)/drm-lease-client.h: protocols/drm-lease-v1.xml | $(BUILD)
	wayland-scanner client-header $< $@
$(BUILD)/drm-lease-protocol.c: protocols/drm-lease-v1.xml | $(BUILD)
	wayland-scanner private-code $< $@
$(BUILD)/drm-lease-protocol.o: $(BUILD)/drm-lease-protocol.c | $(BUILD)
	$(call compile_c,$(BUILD)/drm-lease-protocol.c,$(BUILD)/drm-lease-protocol.o)

$(BUILD)/linux-dmabuf-client.h: protocols/linux-dmabuf-unstable-v1.xml | $(BUILD)
	wayland-scanner client-header $< $@
$(BUILD)/linux-dmabuf-protocol.c: protocols/linux-dmabuf-unstable-v1.xml | $(BUILD)
	wayland-scanner private-code $< $@
$(BUILD)/linux-dmabuf-protocol.o: $(BUILD)/linux-dmabuf-protocol.c | $(BUILD)
	$(call compile_c,$(BUILD)/linux-dmabuf-protocol.c,$(BUILD)/linux-dmabuf-protocol.o)

$(BUILD)/xdg-shell-client.h: protocols/xdg-shell.xml | $(BUILD)
	wayland-scanner client-header $< $@
$(BUILD)/xdg-shell-protocol.c: protocols/xdg-shell.xml | $(BUILD)
	wayland-scanner private-code $< $@
$(BUILD)/xdg-shell-protocol.o: $(BUILD)/xdg-shell-protocol.c | $(BUILD)
	$(call compile_c,$(BUILD)/xdg-shell-protocol.c,$(BUILD)/xdg-shell-protocol.o)

$(BUILD)/test-pixels.o: tests/pixels.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-curvature.o: tests/curvature.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-tracking.o: tests/tracking.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-camera-controls.o: tests/camera_controls.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-targeting.o: tests/targeting.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-hover.o: tests/hover.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-capture-plan.o: tests/capture_plan.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-vblank.o: tests/vblank.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-load-governor.o: tests/load_governor.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-sky-cull.o: tests/sky_cull.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-dwell.o: tests/dwell.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-frame-source.o: tests/frame_source.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-canvas-model.o: tests/canvas_model.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-window-list.o: tests/window_list.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-canvas-placement.o: tests/canvas_placement.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-canvas-memory.o: tests/canvas_memory.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-capture-cadence.o: tests/capture_cadence.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)
$(BUILD)/test-capture-governor.o: tests/capture_governor.cpp | $(BUILD)
	$(call compile_cxx,$<,$@,-Isrc)

$(UNIT_BINS): %: %.o
	$(CXX) $(CXXFLAGS) $^ -o $@

compile_commands.json: $(APP_OBJS) $(UNIT_OBJS)
	@python3 -c 'import json,pathlib,sys; root=pathlib.Path(sys.argv[1]); items=[json.loads(p.read_text()) for p in sorted(root.glob("*.json"))]; pathlib.Path(sys.argv[2]).write_text(json.dumps(items, indent=2)+"\n")' $(BUILD)/cc $@

run: all
	./$(BUILD)/omarchy-xr

run-units: $(UNIT_BINS)
	./$(BUILD)/test-capture-plan
	./$(BUILD)/test-hover
	./$(BUILD)/test-targeting
	./$(BUILD)/test-camera-controls
	./$(BUILD)/test-tracking
	./$(BUILD)/test-curvature
	./$(BUILD)/test-pixels
	./$(BUILD)/test-vblank
	./$(BUILD)/test-load-governor
	./$(BUILD)/test-sky-cull
	./$(BUILD)/test-dwell
	./$(BUILD)/test-frame-source
	./$(BUILD)/test-canvas-model
	./$(BUILD)/test-window-list
	./$(BUILD)/test-canvas-placement
	./$(BUILD)/test-canvas-memory
	./$(BUILD)/test-capture-cadence
	./$(BUILD)/test-capture-governor

check: all run-units
	lua tests/controls.lua
	python3 -m unittest discover -s tests -p 'test_*.py'
	./$(BUILD)/omarchy-xr --help
	./$(BUILD)/omarchy-xr --version
	! ./$(BUILD)/omarchy-xr --invalid-option
	! ./$(BUILD)/omarchy-xr --capture
	! ./$(BUILD)/omarchy-xr --canvas x --layout y
	! ./$(BUILD)/omarchy-xr --list-leases --layout x --capture y
	! ./$(BUILD)/omarchy-xr --canvas-windows-file x

# Address and undefined-behavior sanitizers on the unit binaries only.
check-san:
	$(MAKE) BUILD=$(BUILD)/san CXXFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" run-units

# Requires a graphical session, or xvfb-run on CI.
smoke: all
	./$(BUILD)/omarchy-xr --smoke-test

# Opt-in Window Canvas smoke (Hyprland session): SPIKE-canvas output, 1 staged + 4 parked test clients,
# --smoke-test --stereo --canvas windowed; removes the output and the clients again. Not part of check.
smoke-canvas: all $(BUILD)/spike-window-capture
	python3 tests/live_canvas.py --smoke
.PHONY: smoke-canvas

# Convenience alias: the Window Canvas subset of UNIT_BINS (run-units and check-san run them too) plus
# the offscreen canvas focus invariants (also in check-workspace-focus). Adds no coverage of its own.
CANVAS_UNITS = $(filter %canvas-model %canvas-placement %canvas-memory %window-list %capture-cadence %capture-governor,$(UNIT_BINS))
check-canvas: $(CANVAS_UNITS) $(BUILD)/test-canvas-focus
	for t in $(CANVAS_UNITS); do ./$$t || exit 1; done
	SDL_VIDEODRIVER=offscreen ./$(BUILD)/test-canvas-focus
.PHONY: check-canvas

clean:
	rm -rf $(BUILD) compile_commands.json

install-studio: all
	python3 scripts/install-studio.py

studio:
	omarchy-shell shell summon afruth.omarchy-xr '{}'

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

install-notifications:
	python3 scripts/install-notifications.py
.PHONY: install-notifications

# Opt-in hardware probe; not part of unattended checks (moves the real pointer).
$(BUILD)/capture-timing: tests/capture_timing.cpp src/capture.cpp $(BUILD)/capture.o $(BUILD)/linux-dmabuf-protocol.o $(BUILD)/wlr-screencopy-protocol.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc tests/capture_timing.cpp $(BUILD)/capture.o $(BUILD)/linux-dmabuf-protocol.o $(BUILD)/wlr-screencopy-protocol.o -o $@ $(LDFLAGS) $(LDLIBS)

# Opt-in window capture probe (Hyprland session): per-window distinct fps, request->ready p50, transport.
$(BUILD)/window-capture-probe: tests/window_capture_probe.cpp $(BUILD)/window_capture.o $(BUILD)/linux-dmabuf-protocol.o \
	$(BUILD)/hyprland-toplevel-export-protocol.o $(BUILD)/wlr-foreign-toplevel-protocol.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc $^ -o $@ $(LDFLAGS) $(LDLIBS)

# Optional Qt UI regression suite (requires Qt 6.6+ declarative development tools).
QMLTESTRUNNER ?= /usr/lib/qt6/bin/qmltestrunner
check-ui:
	QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=basic QT_QUICK_CONTROLS_STYLE=Basic $(QMLTESTRUNNER) -input tests/qml -import studio
.PHONY: check-ui

# Python, Lua, and QML gates. ruff, mypy, and luacheck come from the environment.
RUFF ?= ruff
MYPY ?= mypy
LUACHECK ?= luacheck
QMLLINT ?= /usr/lib/qt6/bin/qmllint
check-lint:
	$(RUFF) check studio scripts tests
	$(MYPY)
	$(LUACHECK) config/xr-controls.lua tests/controls.lua
	$(QMLLINT) -I tools/qmlstubs studio/MonitorStudio.qml studio/BarWidget.qml studio/AngleField.qml studio/RequestState.qml
	python3 scripts/function_length.py
.PHONY: check-lint

# Requires a GL-capable graphical session (or xvfb-run on CI).
$(BUILD)/test-environment: tests/environment.cpp src/environment.hpp src/tron_environment.hpp src/theme.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Isrc $< -o $@ $(shell pkg-config --libs sdl2 gl)

$(BUILD)/test-tron-environment: tests/tron_environment.cpp src/environment.hpp src/tron_environment.hpp src/theme.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Isrc $< -o $@ $(shell pkg-config --libs sdl2 gl)

check-environment: $(BUILD)/test-environment $(BUILD)/test-tron-environment
	./$(BUILD)/test-environment
	./$(BUILD)/test-tron-environment

$(BUILD)/test-workspace-focus: tests/workspace_focus.cpp $(APP_OBJS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc $< $(filter-out $(BUILD)/main.o,$(APP_OBJS)) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD)/test-canvas-focus: tests/canvas_focus.cpp $(APP_OBJS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc $< $(filter-out $(BUILD)/main.o,$(APP_OBJS)) -o $@ $(LDFLAGS) $(LDLIBS)

# Hidden SDL window; exercises the actual renderer without capturing the desktop (monitor and canvas mode).
check-workspace-focus: $(BUILD)/test-workspace-focus $(BUILD)/test-canvas-focus
	SDL_VIDEODRIVER=offscreen ./$(BUILD)/test-workspace-focus
	SDL_VIDEODRIVER=offscreen ./$(BUILD)/test-canvas-focus
.PHONY: check-workspace-focus

$(BUILD)/test-scene-seam: tests/scene_seam.cpp $(APP_OBJS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc $< $(filter-out $(BUILD)/main.o,$(APP_OBJS)) -o $@ $(LDFLAGS) $(LDLIBS)

# The View scene seam (geometry, cylinder, surface views, shared GBM device, routing) on a hidden offscreen window; part of check-notifications.
check-scene-seam: $(BUILD)/test-scene-seam
	SDL_VIDEODRIVER=offscreen ./$(BUILD)/test-scene-seam
.PHONY: check-scene-seam

$(BUILD)/test-notifications: tests/notifications.cpp src/notification_hud.hpp src/gl_texture.hpp src/notification_content.hpp src/notification_space.hpp src/notification_draw.hpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc $< -o $@ $(LDLIBS)

$(BUILD)/test-notification-space: tests/notification_space.cpp src/notification_space.hpp src/targeting.hpp src/curvature.hpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc $< -o $@

$(BUILD)/test-notification-controls: tests/notification_controls.cpp $(APP_OBJS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc $< $(filter-out $(BUILD)/main.o,$(APP_OBJS)) -o $@ $(LDFLAGS) $(LDLIBS)

check-notifications: $(BUILD)/test-notifications $(BUILD)/test-notification-space $(BUILD)/test-notification-controls check-scene-seam
	SDL_VIDEODRIVER=offscreen ./$(BUILD)/test-notifications
	./$(BUILD)/test-notification-space
	SDL_VIDEODRIVER=offscreen ./$(BUILD)/test-notification-controls
.PHONY: check-notifications

$(BUILD)/notification-preview: tests/notification_preview.cpp src/notification_space.hpp src/notification_draw.hpp src/notification_hud.hpp $(APP_OBJS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc $< $(filter-out $(BUILD)/main.o,$(APP_OBJS)) -o $@ $(LDFLAGS) $(LDLIBS)

PREVIEW_BASELINE = tests/baselines/notification-preview
PREVIEW_STILLS = overview turn behind settled zoomed reading cycled
$(BUILD)/image-diff: tests/image_diff.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(shell pkg-config --libs cairo)
# Pixel-exact renderer regression against a committed baseline (same machine and GL driver).
# PREVIEW_UPDATE=1 rewrites the baseline after an intentional visual change.
check-preview: $(BUILD)/notification-preview $(BUILD)/image-diff
	rm -rf $(BUILD)/preview && mkdir -p $(BUILD)/preview
	SDL_VIDEODRIVER=offscreen ./$(BUILD)/notification-preview $(BUILD)/preview
	if [ -n "$(PREVIEW_UPDATE)" ]; then for s in $(PREVIEW_STILLS); do cp $(BUILD)/preview/$$s.png $(PREVIEW_BASELINE)/; done; fi
	./$(BUILD)/image-diff $(PREVIEW_BASELINE) $(BUILD)/preview $(PREVIEW_STILLS)
.PHONY: check-preview

-include $(wildcard $(BUILD)/*.d)

# Manual visuals and GPU timings: build/environment-preview OUT_DIR [PANORAMA_BMP]
$(BUILD)/environment-preview: tests/environment_preview.cpp src/environment.hpp src/tron_environment.hpp src/theme.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Isrc $< -o $@ $(shell pkg-config --libs sdl2 gl)
