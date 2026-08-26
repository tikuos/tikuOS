# The kits.
#
# Six libraries, in the sense BeOS meant: a kit is a group of headers and
# one archive, named for the job it does, that an application links
# without knowing what else links it.  The grouping is the directory --
# BeOS did the same, and did NOT decorate its class names with the kit
# they came from, which is why everything here shares one prefix.
#
# The order below is the dependency order, lowest first, and it is a DAG
# rather than a stack:
#
#   support                              nothing at all
#   device                               the link to a TikuOS device
#   interface                            pixels, faces, controls, windows
#   storage     -> support, device       nodes, attributes, queries, volumes
#   translation -> support, storage,     pictures into bitmaps
#                  interface
#   application -> support, device,      apps, windows, messages
#                  interface
#
# tools/check_kits.py holds that table and fails the build if an include
# crosses it the wrong way.  The rule is the point of the arrangement:
# without something checking it, a kit is a folder.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>

CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-parameter \
           -Isupport -Idevice -Istorage -Iinterface -Itranslation \
           -Iapplication

BUILD := build

SUPPORT     := support/tiku_msg.c support/tiku_strings.c
DEVICE      := device/tiku_tx.c device/tiku_session.c \
               device/tiku_ns.c
STORAGE     := storage/tiku_model.c storage/tiku_state.c \
               storage/tiku_ident.c storage/tiku_attr.c \
               storage/tiku_query.c storage/tiku_fs.c \
               storage/tiku_backend_posix.c \
               storage/tiku_backend_tiku.c storage/tiku_notify.c \
               storage/tiku_vdir.c storage/tiku_volume.c \
               storage/tiku_mount.c
INTERFACE   := interface/tiku_gfx.c interface/tiku_theme.c \
               interface/tiku_font.c interface/tiku_ttf.c \
               interface/tiku_glyphpath.c interface/tiku_cff.c \
               interface/tiku_ui.c interface/tiku_dl.c interface/tiku_logo.c \
               interface/tiku_scroll.c interface/tiku_field.c \
               interface/tiku_menu.c interface/tiku_tabs.c \
               interface/tiku_slider.c interface/tiku_colors.c \
               interface/tiku_textview.c interface/tiku_alert.c \
               interface/tiku_syntax.c interface/tiku_list.c \
               interface/tiku_window.c \
               interface/tiku_x11.c
TRANSLATION := translation/tiku_inflate.c translation/tiku_jpeg.c \
               translation/tiku_hvif.c translation/tiku_image.c \
               translation/tiku_thumb.c translation/tiku_iconpaint.c \
               translation/tiku_icons.c translation/tiku_fontdir.c
APPLICATION := application/tiku_app.c application/tiku_app_load.c \
               application/tiku_remote.c \
               application/tiku_conduct.c \
               application/tiku_client.c \
               application/tiku_demo_app.c

KITS := support device storage interface translation application

LIB_SUPPORT     := $(BUILD)/libtiku_support.a
LIB_DEVICE      := $(BUILD)/libtiku_device.a
LIB_STORAGE     := $(BUILD)/libtiku_storage.a
LIB_INTERFACE   := $(BUILD)/libtiku_interface.a
LIB_TRANSLATION := $(BUILD)/libtiku_translation.a
LIB_APPLICATION := $(BUILD)/libtiku_application.a

# In link order: a linker resolves left to right, so a kit must come
# before the kits it draws on.
LIBS := $(LIB_APPLICATION) $(LIB_TRANSLATION) $(LIB_INTERFACE) \
        $(LIB_STORAGE) $(LIB_DEVICE) $(LIB_SUPPORT)

PIC_LIBS := $(patsubst $(BUILD)/%,$(BUILD)/pic/%,$(LIBS))

.PHONY: all libs pic clean check
all: libs pic

libs: $(LIBS)
pic: $(PIC_LIBS)

$(BUILD) $(BUILD)/pic:
	@mkdir -p $@

$(BUILD)/%.o: %.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# The same sources again, position-independent: a shared object cannot
# link an archive of fixed-address code, and applications are loadable.
$(BUILD)/pic/%.o: %.c | $(BUILD)/pic
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -MMD -MP -c -o $@ $<

#
# The archive is REMOVED before it is made, every time.  `ar rcs` adds
# members and never takes any away, so an archive built over an older one
# keeps whatever used to be in it -- and a file MOVED from one kit to
# another therefore ends up in both.  The linker then resolves it from
# whichever archive comes first, which is the stale one, and the build
# quietly runs code that is no longer in the tree.  It cost an afternoon
# once; it costs an rm here.
#
define KIT_RULES
$(BUILD)/libtiku_$(1).a: $$(patsubst %.c,$(BUILD)/%.o,$($(2)))
	@rm -f $$@
	$$(AR) rcs $$@ $$^
$(BUILD)/pic/libtiku_$(1).a: $$(patsubst %.c,$(BUILD)/pic/%.o,$($(2)))
	@mkdir -p $$(dir $$@)
	@rm -f $$@
	$$(AR) rcs $$@ $$^
endef

$(eval $(call KIT_RULES,support,SUPPORT))
$(eval $(call KIT_RULES,device,DEVICE))
$(eval $(call KIT_RULES,storage,STORAGE))
$(eval $(call KIT_RULES,interface,INTERFACE))
$(eval $(call KIT_RULES,translation,TRANSLATION))
$(eval $(call KIT_RULES,application,APPLICATION))

# What keeps a kit a kit.
check:
	@python3 tools/check_kits.py

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
