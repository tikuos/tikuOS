# TikuDesktop -- host build of the app framework (Linux/Mac).
#
# Builds what the toolkit ships beside the shell: the headless probe, the
# gallery, the demo app, tiku-edit and tiku-term, the four examples, the
# loadable .so form of the applications and examples, and tiku-run.  The
# desktop SHELL is not COMPILED here despite the directory name: it is one
# of two front-ends over a shared core that lives in the separate
# applications/TikuTracker repository.  `make shell` goes there for it and
# brings the result back, so build/TikuDesktop under this directory is
# where a reader looking for the shell now finds it.
#
# Only the probe is still libc-only, and deliberately so: it is the one
# binary that has to run on a machine with no display.  Everything that
# draws links X11 and libm, and tiku-run adds libdl to open the .so form.
# Kept warning-clean at -Wall -Wextra.
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

# Where the shell is really made.  Naming the two binaries rather than
# asking for `all` keeps this to the two links that matter: going next
# door for that whole tree's test suite as well would cost minutes and
# build nothing this directory delivers.
TRACKER       := ../TikuTracker
SHELL_FROM    := $(TRACKER)/build/TikuDesktop
SHELLDEV_FROM := $(TRACKER)/build/dev/TikuDesktop
SHELL_HERE    := $(BUILD)/TikuDesktop
SHELLDEV_HERE := $(BUILD)/dev/TikuDesktop

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
PIC_UI        := $(KIT_BUILD)/pic/libtiku_interface.a \
                 $(KIT_BUILD)/pic/libtiku_translation.a
PIC_TRANSPORT := $(KIT_BUILD)/pic/libtiku_storage.a \
                 $(KIT_BUILD)/pic/libtiku_device.a \
                 $(KIT_BUILD)/pic/libtiku_support.a
PIC_LIBS      := $(PIC_RUNTIME) $(PIC_UI) $(PIC_TRANSPORT)

PROBE   := $(BUILD)/tiku-desk-probe
GALLERY := $(BUILD)/tiku-desk-gallery

# The board's own BASIC interpreter, compiled for the host: the editor
# runs a program by running this, so what its output pane shows is what
# a board would do -- not a re-implementation that agrees on good days.
BASICHOST := $(BUILD)/tiku-basic
BASIC_GATES := -DTIKU_BASIC_GPIO_ENABLE=0 -DTIKU_BASIC_ADC_ENABLE=0 \
               -DTIKU_BASIC_I2C_ENABLE=0 -DTIKU_BASIC_LED_ENABLE=0 \
               -DTIKU_BASIC_RTC_ENABLE=0 -DTIKU_BASIC_NET_ENABLE=0 \
               -DTIKU_BASIC_BLE_ENABLE=0 -DTIKU_BASIC_JSON_ENABLE=0 \
               -DTIKU_BASIC_CRYPTO_ENABLE=0 -DTIKU_BASIC_HTTPS_ENABLE=0 \
               -DTIKU_BASIC_REBOOT_ENABLE=0 -DTIKU_BASIC_PEEK_POKE_ENABLE=0 \
               -DTIKU_BASIC_MODULE_ENABLE=0 -DTIKU_BASIC_FILE_ENABLE=0

$(BASICHOST): basichost/tiku_basic_host.c ../../kernel/shell/basic/tiku_basic.c | $(BUILD)
	$(CC) -O2 -std=gnu11 -o $@ basichost/tiku_basic_host.c \
	    ../../kernel/shell/basic/tiku_basic.c \
	    -Ibasichost -I../../ -I../../kernel/shell/basic $(BASIC_GATES)
DEMOAPP := $(BUILD)/tiku-demo-app

# Applications, as opposed to examples: these are meant to be used.
# One binary per name, from one source file each.  The same file also
# builds a loadable .so with -DTIKU_APP_EMBED, which drops its main and
# leaves the descriptor for whatever process opens it.  The examples
# below are built the same way, under -DTIKU_EXAMPLE_EMBED.
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
PROG_D    := $(APPS_D)/programming
SO_APPS   := $(APPS_D)/terminal.so $(APPS_D)/editor.so \
             $(PROG_D)/TikuBasic.so $(PROG_D)/TikuC.so
SO_EXAMPLES := $(patsubst %,$(APPS_D)/%.so,hello clock counter sketch twowin)
RUNNER    := $(BUILD)/tiku-run

EXAMPLE_NAMES := hello clock counter sketch
EXAMPLES := $(patsubst %,$(BUILD)/tiku-example-%,$(EXAMPLE_NAMES))
TEST_APP := $(BUILD)/test-desk-app

.PHONY: all clean run gallery font libs test shell
all: libs $(PROBE) $(GALLERY) $(DEMOAPP) $(APPS) $(EXAMPLES) $(BASICHOST) \
     $(SO_APPS) $(SO_EXAMPLES) $(RUNNER) shell

# The kits are built where they live, by their own Makefile: this one
# only says that nothing here can be built before they are.
libs:
	@$(MAKE) --no-print-directory -C $(KITS) libs pic

# After `libs`, and that ordering is load-bearing rather than tidy: the
# Tracker tree builds the kits the same way this one does, so under
# `make -j` the two sub-makes would be writing the same archives at the
# same time.  Going through libs first leaves them up to date, and the
# sub-make next door then finds nothing to do about them.
#
# Copied rather than linked: a symlink into another repository's build
# directory dangles the moment that repository is cleaned, and points out
# of this tree in a tarball.  Through a temporary name and a rename,
# because overwriting a binary that is currently running is ETXTBSY while
# replacing it is not.
shell: libs
	@$(MAKE) --no-print-directory -C $(TRACKER) \
	    build/TikuDesktop build/dev/TikuDesktop
	@mkdir -p $(BUILD)/dev
	@cp -p $(SHELL_FROM) $(SHELL_HERE).new
	@mv -f $(SHELL_HERE).new $(SHELL_HERE)
	@cp -p $(SHELLDEV_FROM) $(SHELLDEV_HERE).new
	@mv -f $(SHELLDEV_HERE).new $(SHELLDEV_HERE)


$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# The archives themselves are the kits' business; these say only that
# they are made by going there, so make does not call them missing.
$(LIB_RUNTIME) $(LIB_UI) $(LIB_TRANSPORT) $(PIC_LIBS): libs ;

$(PROBE): $(BUILD)/tiku_probe.o $(LIB_TRANSPORT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "built $@"

$(GALLERY): $(BUILD)/tiku_gallery.o $(LIB_UI) $(LIB_TRANSPORT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lX11
	@echo "built $@"

$(DEMOAPP): src/tiku_demo_main.c $(LIB_RUNTIME) $(LIB_UI) $(LIB_TRANSPORT)
	$(CC) $(CFLAGS) -o $@ src/tiku_demo_main.c $(LIB_RUNTIME) $(LIB_UI) $(LIB_TRANSPORT) $(LDFLAGS) -lX11 -lm

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

# Built INTO apps/programming/, because where a file is installed is
# where it is offered -- a build that put these beside the editor would
# put them in the Applications menu on every machine that runs it.
$(PROG_D):
	@mkdir -p $(PROG_D)

$(PROG_D)/TikuBasic.so: apps/tiku_ide.c $(PIC_LIBS) | $(PROG_D)
	$(CC) $(CFLAGS) -fPIC -shared -DTIKU_APP_EMBED -DTIKU_APP_SO \
	      -o $@ $< $(PIC_LIBS) $(LDFLAGS) -lX11 -lm

# The same source wearing its other name: what differs is what it puts
# forward, and a copy would only learn to disagree.
$(PROG_D)/TikuC.so: apps/tiku_ide.c $(PIC_LIBS) | $(PROG_D)
	$(CC) $(CFLAGS) -fPIC -shared -DTIKU_APP_EMBED -DTIKU_APP_SO \
	      -DTIKU_IDE_C \
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
	python3 tools/genfont.py > $(KITS)/interface/tiku_font_data.h

$(BUILD):
	@mkdir -p $(BUILD)

# Convenience: probe the first board on the bench.
run: $(PROBE)
	$(PROBE) -v

clean:
	rm -rf $(BUILD)

-include $(BUILD)/*.d
