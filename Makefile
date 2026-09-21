CXX ?= c++
CC ?= cc
BUILD ?= build
CPPFLAGS += -I$(BUILD) $(shell pkg-config --cflags sdl2 gl wayland-client egl gbm libdrm)
CXXFLAGS ?= -O2 -g
override CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Werror -pthread
DEPFLAGS = -MMD -MP
WL_CFLAGS := $(shell pkg-config --cflags wayland-client)
LDLIBS += $(shell pkg-config --libs sdl2 gl wayland-client egl gbm libdrm)

APP_OBJS = $(BUILD)/main.o $(BUILD)/capture.o $(BUILD)/direct_output.o \
	$(BUILD)/xdg-shell-protocol.o $(BUILD)/linux-dmabuf-protocol.o \
	$(BUILD)/drm-lease-protocol.o $(BUILD)/wlr-screencopy-protocol.o
GEN_HEADERS = $(BUILD)/xdg-shell-client.h $(BUILD)/linux-dmabuf-client.h \
	$(BUILD)/drm-lease-client.h $(BUILD)/wlr-screencopy-client.h
UNIT_BINS = $(BUILD)/test-pixels $(BUILD)/test-curvature $(BUILD)/test-tracking \
	$(BUILD)/test-camera-controls $(BUILD)/test-targeting $(BUILD)/test-hover \
	$(BUILD)/test-capture-plan $(BUILD)/test-vblank
UNIT_OBJS = $(UNIT_BINS:%=%.o)

.PHONY: all run check run-units check-san smoke clean install-studio studio compile_commands.json
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

$(BUILD)/wlr-screencopy-client.h: protocols/wlr-screencopy-unstable-v1.xml | $(BUILD)
	wayland-scanner client-header $< $@
$(BUILD)/wlr-screencopy-protocol.c: protocols/wlr-screencopy-unstable-v1.xml | $(BUILD)
	wayland-scanner private-code $< $@
$(BUILD)/wlr-screencopy-protocol.o: $(BUILD)/wlr-screencopy-protocol.c | $(BUILD)
	$(call compile_c,$(BUILD)/wlr-screencopy-protocol.c,$(BUILD)/wlr-screencopy-protocol.o)

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

check: all run-units
	lua tests/controls.lua
	python3 -m unittest discover -s tests -p 'test_*.py'
	./$(BUILD)/omarchy-xr --help
	./$(BUILD)/omarchy-xr --version
	! ./$(BUILD)/omarchy-xr --invalid-option
	! ./$(BUILD)/omarchy-xr --capture

# Address and undefined-behavior sanitizers on the unit binaries only.
check-san:
	$(MAKE) BUILD=$(BUILD)/san CXXFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" run-units

# Requires a graphical session, or xvfb-run on CI.
smoke: all
	./$(BUILD)/omarchy-xr --smoke-test

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

# Opt-in hardware probe; not part of unattended checks (moves the real pointer).
$(BUILD)/capture-timing: tests/capture_timing.cpp src/capture.cpp $(BUILD)/capture.o $(BUILD)/linux-dmabuf-protocol.o $(BUILD)/wlr-screencopy-protocol.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Isrc tests/capture_timing.cpp $(BUILD)/capture.o $(BUILD)/linux-dmabuf-protocol.o $(BUILD)/wlr-screencopy-protocol.o -o $@ $(LDFLAGS) $(LDLIBS)

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
$(BUILD)/test-environment: tests/environment.cpp src/environment.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Isrc $< -o $@ $(shell pkg-config --libs sdl2 gl)

check-environment: $(BUILD)/test-environment
	./$(BUILD)/test-environment

-include $(wildcard $(BUILD)/*.d)
