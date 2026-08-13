# Tiku Desktop -- host build (Linux/Mac).
#
# S0 builds the headless probe only; the SDL desktop links the same objects
# once S1 lands.  Kept warning-clean at -Wall -Wextra, C11, no dependencies
# beyond libc so the Emscripten step needs no untangling later.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2 -g
# gnu11, not c11: strict C11 hides the POSIX/BSD declarations this needs
# (getaddrinfo, cfmakeraw, clock_gettime) and the build fails on the seam
# rather than on anything real.
CFLAGS  += -std=gnu11 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-parameter -Isrc
LDFLAGS ?=

BUILD   := build
CORE    := src/tiku_desk_tx.c src/tiku_desk_session.c src/tiku_desk_ns.c
CORE_O  := $(patsubst src/%.c,$(BUILD)/%.o,$(CORE))

UI      := src/tiku_desk_gfx.c src/tiku_desk_font.c src/tiku_desk_ui.c
UI_O    := $(patsubst src/%.c,$(BUILD)/%.o,$(UI))

RUNTIME   := src/tiku_desk_app.c
RUNTIME_O := $(patsubst src/%.c,$(BUILD)/%.o,$(RUNTIME))

LIB_TRANSPORT := $(BUILD)/libtiku_desk_transport.a
LIB_UI        := $(BUILD)/libtiku_desk_ui.a
LIB_RUNTIME   := $(BUILD)/libtiku_desk_runtime.a

PROBE   := $(BUILD)/tiku-desk-probe
GALLERY := $(BUILD)/tiku-desk-gallery
TEST_APP := $(BUILD)/test-desk-app

.PHONY: all clean run gallery font libs test
all: libs $(PROBE) $(GALLERY)

libs: $(LIB_TRANSPORT) $(LIB_UI) $(LIB_RUNTIME)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(LIB_TRANSPORT): $(CORE_O)
	$(AR) rcs $@ $^

$(LIB_UI): $(UI_O)
	$(AR) rcs $@ $^

$(LIB_RUNTIME): $(RUNTIME_O)
	$(AR) rcs $@ $^

$(PROBE): $(BUILD)/tiku_desk_probe.o $(LIB_TRANSPORT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "built $@"

$(GALLERY): $(BUILD)/tiku_desk_gallery.o $(BUILD)/tiku_desk_x11.o $(LIB_UI)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lX11
	@echo "built $@"

$(TEST_APP): tests/test_app.c $(LIB_RUNTIME)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: $(TEST_APP)
	@$(TEST_APP)

# The gallery is S1's exit test: hold this image against an R5 screenshot.
gallery: $(GALLERY)
	$(GALLERY) -o $(BUILD)/gallery.png -q

# Regenerate the baked font (needs python3 + PIL; output is committed).
font:
	python3 tools/genfont.py > src/tiku_desk_font_data.h

$(BUILD):
	@mkdir -p $(BUILD)

# Convenience: probe the first board on the bench.
run: $(PROBE)
	$(PROBE) -v

clean:
	rm -rf $(BUILD)

-include $(BUILD)/*.d
