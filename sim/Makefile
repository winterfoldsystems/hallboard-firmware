# Host simulator for the HallBoard faces. Plain make, no cmake.
#
#   make -C firmware/sim png     fetch what is missing, build, write out/<scenario>.png
#   make -C firmware/sim run     the same UI in an SDL2 window, if SDL2 is installed
#   make -C firmware/sim lvgl    just the LVGL checkout
#   make -C firmware/sim fonts   just the fonts
#   make -C firmware/sim clean   drop build/ and out/
#
# lvgl/, fonts/ and out/ are not in git (see .gitignore). The two fetches are the only network
# the harness ever does: LVGL is pinned to the tag the device build uses and the fonts are
# checked against fonts.sha256. Running the simulator itself talks to nothing.

LVGL_REPO := https://github.com/lvgl/lvgl
# The version ESPHome's managed component brings in; see the device build's dependencies.lock.
LVGL_TAG := v9.5.0

# Google Fonts, which is where ui.yaml's gfonts:// files come from too.
GF := https://raw.githubusercontent.com/google/fonts/main
# Figtree's own repository, which is where its static weights live. google/fonts carries the
# variable font only, and TinyTTF is stb_truetype underneath, which ignores the weight axis.
FIGTREE := https://raw.githubusercontent.com/erikdkennedy/figtree/master/fonts/ttf

CC ?= clang
CXX ?= clang++
SHASUM := $(shell command -v sha256sum >/dev/null 2>&1 && echo sha256sum || echo "shasum -a 256")

INCLUDES := -I. -Istubs -I.. -Ilvgl -Ithird_party
COMMON := -O2 $(INCLUDES) -DLV_CONF_INCLUDE_SIMPLE -MMD -MP
# -Wall is for the code in this directory. LVGL is vendored upstream and warns about a handful of
# unused statics under this lv_conf; those are not ours to fix and would only hide our own.
CFLAGS := $(COMMON)
CXXFLAGS := -std=c++17 -Wall $(COMMON)

BUILD := build
BIN := $(BUILD)/hbsim

# The whole LVGL tree is compiled, most of it to nothing under this lv_conf. Doing it one file at
# a time is the slow part of a first build, so the recursive makes below spread it over the cores
# they find rather than making CI pass -j.
NPROC := $(shell (nproc || sysctl -n hw.ncpu) 2>/dev/null || echo 4)

# Every LVGL source but the platform drivers, which need a windowing system and are only pulled
# in by the SDL target below.
LVGL_SRC := $(shell find lvgl/src -name '*.c' -not -path 'lvgl/src/drivers/*' 2>/dev/null)
LVGL_OBJ := $(LVGL_SRC:%.c=$(BUILD)/%.o)

FIGTREE_WEIGHTS := Regular Medium SemiBold Bold
FONT_FILES := $(FIGTREE_WEIGHTS:%=fonts/Figtree-%.ttf) fonts/IBMPlexMono-Regular.ttf

.PHONY: all png run lvgl fonts clean list
all: png

# ---------------------------------------------------------------- fetches
lvgl: lvgl/lv_version.h
lvgl/lv_version.h:
	git clone --depth 1 --branch $(LVGL_TAG) $(LVGL_REPO) lvgl

# One file per weight, matching what the Google CSS API hands ESPHome for each `gfonts://`
# entry in ui.yaml. LVGL's built-in Montserrat bitmaps are compiled C and need no TTF here.
fonts/Figtree-%.ttf:
	@mkdir -p fonts
	curl -fsSL -o $@ "$(FIGTREE)/Figtree-$*.ttf"
fonts/IBMPlexMono-Regular.ttf:
	@mkdir -p fonts
	curl -fsSL -o $@ "$(GF)/ofl/ibmplexmono/IBMPlexMono-Regular.ttf"

# The first run writes fonts.sha256 from what it downloaded; every run after that checks against
# it, so a font changing under us is a build failure and not a silently different render.
fonts: $(FONT_FILES)
	@if [ -f fonts.sha256 ]; then \
	  (cd fonts && $(SHASUM) -c ../fonts.sha256) || \
	    { echo "A downloaded font does not match fonts.sha256. Delete fonts/ and look at why."; exit 1; }; \
	else \
	  (cd fonts && $(SHASUM) $(notdir $(FONT_FILES)) > ../fonts.sha256); \
	  echo "Wrote fonts.sha256 from this download. Check it in."; \
	fi

# ---------------------------------------------------------------- build and render
png: lvgl fonts
	@$(MAKE) --no-print-directory -j$(NPROC) $(BIN)
	@mkdir -p out
	./$(BIN)

list: lvgl fonts
	@$(MAKE) --no-print-directory -j$(NPROC) $(BIN)
	@./$(BIN) --list

$(BIN): $(BUILD)/main.o $(LVGL_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/main.o: main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# ---------------------------------------------------------------- optional window
# Not part of CI, and not needed to produce the PNGs. It exists so a face can be watched while a
# style is being changed rather than rebuilt and reopened each time.
SDL_BUILD := $(BUILD)/sdl
SDL_BIN := $(SDL_BUILD)/hbsim-sdl
SDL_SRC := $(shell find lvgl/src -name '*.c' 2>/dev/null)
SDL_OBJ := $(SDL_SRC:%.c=$(SDL_BUILD)/%.o)
SDL_CFLAGS := -DLV_USE_SDL=1 $(shell sdl2-config --cflags 2>/dev/null)

run: lvgl fonts
	@if command -v sdl2-config >/dev/null 2>&1; then \
	  $(MAKE) --no-print-directory -j$(NPROC) $(SDL_BIN) && ./$(SDL_BIN) $(ARGS); \
	else \
	  echo "No SDL2 here, so there is no window to open."; \
	  echo "Install it (brew install sdl2, or apt install libsdl2-dev) and run make run again."; \
	  echo "The PNGs do not need it: make -C firmware/sim png"; \
	fi

$(SDL_BIN): $(SDL_BUILD)/main.o $(SDL_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(shell sdl2-config --libs)

$(SDL_BUILD)/main.o: main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -DHB_SIM_SDL=1 -c -o $@ $<

$(SDL_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD) out

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
