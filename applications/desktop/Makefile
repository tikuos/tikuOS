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
KITS    := ../kits
CFLAGS  += -std=gnu11 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-parameter \
           -Isrc -I$(KITS)/support -I$(KITS)/device -I$(KITS)/storage \
           -I$(KITS)/interface -I$(KITS)/translation -I$(KITS)/application
LDFLAGS ?=

BUILD   := build

# The toolkit moved out into the kits; what is left here is the samples
# and the tools that exercise them.  The three names below are kept
# because every rule downstream is written in terms of them -- they now
# resolve to kit archives, in the order a linker wants them.
KIT_BUILD     := $(KITS)/build
LIB_RUNTIME   := $(KIT_BUILD)/libtiku_application.a
LIB_UI        := $(KIT_BUILD)/libtiku_interface.a
LIB_TRANSPORT := $(KIT_BUILD)/libtiku_storage.a $(KIT_BUILD)/libtiku_device.a \
                 $(KIT_BUILD)/libtiku_support.a

PIC_RUNTIME   := $(KIT_BUILD)/pic/libtiku_application.a
PIC_UI        := $(KIT_BUILD)/pic/libtiku_interface.a
PIC_TRANSPORT := $(KIT_BUILD)/pic/libtiku_storage.a \
                 $(KIT_BUILD)/pic/libtiku_device.a \
                 $(KIT_BUILD)/pic/libtiku_support.a
PIC_LIBS      := $(PIC_RUNTIME) $(PIC_UI) $(PIC_TRANSPORT)

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

# The kits are built where they live, by their own Makefile: this one
# only says that nothing here can be built before they are.
libs:
	@$(MAKE) --no-print-directory -C $(KITS) libs pic


$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# The archives themselves are the kits' business; these say only that
# they are made by going there, so make does not call them missing.
$(LIB_RUNTIME) $(LIB_UI) $(LIB_TRANSPORT) $(PIC_LIBS): libs ;

$(PROBE): $(BUILD)/tiku_desk_probe.o $(LIB_TRANSPORT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "built $@"

$(GALLERY): $(BUILD)/tiku_desk_gallery.o $(LIB_UI) $(LIB_TRANSPORT)
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
