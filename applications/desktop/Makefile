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

UI      := src/tiku_desk_gfx.c src/tiku_desk_theme.c src/tiku_desk_font.c src/tiku_desk_ui.c src/tiku_desk_about_app.c
UI_O    := $(patsubst src/%.c,$(BUILD)/%.o,$(UI))

RUNTIME   := src/tiku_desk_app.c src/tiku_desk_app_load.c src/tiku_desk_window.c src/tiku_desk_remote.c src/tiku_desk_conduct.c src/tiku_desk_client.c src/tiku_desk_x11.c
RUNTIME_O := $(patsubst src/%.c,$(BUILD)/%.o,$(RUNTIME))

PIC_CORE_O    := $(patsubst src/%.c,$(BUILD)/pic/%.o,$(CORE))
PIC_UI_O      := $(patsubst src/%.c,$(BUILD)/pic/%.o,$(UI))
PIC_RUNTIME_O := $(patsubst src/%.c,$(BUILD)/pic/%.o,$(RUNTIME))
PIC_TRANSPORT := $(BUILD)/pic/libtiku_desk_transport.a
PIC_UI        := $(BUILD)/pic/libtiku_desk_ui.a
PIC_RUNTIME   := $(BUILD)/pic/libtiku_desk_runtime.a
PIC_LIBS      := $(PIC_RUNTIME) $(PIC_UI) $(PIC_TRANSPORT)

LIB_TRANSPORT := $(BUILD)/libtiku_desk_transport.a
LIB_UI        := $(BUILD)/libtiku_desk_ui.a
LIB_RUNTIME   := $(BUILD)/libtiku_desk_runtime.a

PROBE   := $(BUILD)/tiku-desk-probe
GALLERY := $(BUILD)/tiku-desk-gallery
ABOUTAPP := $(BUILD)/tiku-about-app

# One binary per example, from one source file each.  The same file also
# compiles into a desktop with -DTIKU_EXAMPLE_EMBED, which drops its main
# and leaves the descriptor to be hosted in that process.
# Applications, as opposed to examples: these are meant to be used.
APP_NAMES := edit term
APPS := $(patsubst %,$(BUILD)/tiku-%,$(APP_NAMES))

# The loadable form of every application and example: one artifact that
# the desktop can open in its own process or start beside itself.
# Named "apps", not "apps.d": the dependency-file include below globs
# $(BUILD)/*.d, and a directory caught by that glob stops the build.
APPS_D    := $(BUILD)/apps
SO_NAMES  := edit term
# Named for what they ARE, since the file name is the label a desktop
# offers them under.
SO_APPS   := $(APPS_D)/terminal.so $(APPS_D)/editor.so
SO_EXAMPLES := $(patsubst %,$(APPS_D)/%.so,hello clock counter sketch)
RUNNER    := $(BUILD)/tiku-run

EXAMPLE_NAMES := hello clock counter sketch
EXAMPLES := $(patsubst %,$(BUILD)/tiku-example-%,$(EXAMPLE_NAMES))
TEST_APP := $(BUILD)/test-desk-app

.PHONY: all clean run gallery font libs test
all: libs $(PROBE) $(GALLERY) $(ABOUTAPP) $(APPS) $(EXAMPLES) \
     $(SO_APPS) $(SO_EXAMPLES) $(RUNNER)

libs: $(LIB_TRANSPORT) $(LIB_UI) $(LIB_RUNTIME)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(LIB_TRANSPORT): $(CORE_O)
	$(AR) rcs $@ $^

# The same sources again, position-independent, because a shared object
# cannot link an archive of fixed-address code.
$(BUILD)/pic/%.o: src/%.c | $(BUILD)/pic
	$(CC) $(CFLAGS) -fPIC -MMD -MP -c -o $@ $<

$(BUILD)/pic:
	@mkdir -p $(BUILD)/pic

$(PIC_TRANSPORT): $(PIC_CORE_O)
	$(AR) rcs $@ $^

$(PIC_UI): $(PIC_UI_O)
	$(AR) rcs $@ $^

$(PIC_RUNTIME): $(PIC_RUNTIME_O)
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

$(ABOUTAPP): src/tiku_desk_about_main.c $(LIB_RUNTIME) $(LIB_UI) $(LIB_TRANSPORT)
	$(CC) $(CFLAGS) -o $@ src/tiku_desk_about_main.c $(LIB_RUNTIME) $(LIB_UI) $(LIB_TRANSPORT) $(LDFLAGS) -lX11 -lm

$(BUILD)/tiku-%: apps/tiku_%.c $(LIB_RUNTIME) $(LIB_UI) \
                 $(LIB_TRANSPORT)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_RUNTIME) $(LIB_UI) $(LIB_TRANSPORT) \
	      $(LDFLAGS) -lX11 -lm

.PHONY: apps loadable
apps: $(APPS)

loadable: $(SO_APPS) $(SO_EXAMPLES) $(RUNNER)

$(APPS_D):
	@mkdir -p $(APPS_D)

$(APPS_D)/terminal.so: apps/tiku_term.c $(PIC_LIBS) | $(APPS_D)
	$(CC) $(CFLAGS) -fPIC -shared -DTIKU_APP_EMBED -DTIKU_APP_SO \
	      -o $@ $< $(PIC_LIBS) $(LDFLAGS) -lX11 -lm

$(APPS_D)/editor.so: apps/tiku_edit.c $(PIC_LIBS) | $(APPS_D)
	$(CC) $(CFLAGS) -fPIC -shared -DTIKU_APP_EMBED -DTIKU_APP_SO \
	      -o $@ $< $(PIC_LIBS) $(LDFLAGS) -lX11 -lm

$(APPS_D)/%.so: examples/%.c $(PIC_LIBS) | $(APPS_D)
	$(CC) $(CFLAGS) -fPIC -shared -DTIKU_EXAMPLE_EMBED -DTIKU_APP_SO \
	      -o $@ $< $(PIC_LIBS) $(LDFLAGS) -lX11 -lm

# Gives a loaded application a process of its own: the same file the
# desktop can open in-process, started beside it instead.
$(RUNNER): apps/tiku_run.c $(LIB_RUNTIME) $(LIB_UI) $(LIB_TRANSPORT)
	$(CC) $(CFLAGS) -o $@ apps/tiku_run.c $(LIB_RUNTIME) $(LIB_UI) \
	      $(LIB_TRANSPORT) $(LDFLAGS) -lX11 -lm -ldl

$(BUILD)/tiku-example-%: examples/%.c $(LIB_RUNTIME) $(LIB_UI) \
                        $(LIB_TRANSPORT)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_RUNTIME) $(LIB_UI) $(LIB_TRANSPORT) \
	      $(LDFLAGS) -lX11 -lm

.PHONY: examples
examples: $(EXAMPLES)

$(TEST_APP): tests/test_app.c $(LIB_RUNTIME) $(LIB_UI)
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
