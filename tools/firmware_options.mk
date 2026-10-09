# firmware_options.mk -- the build knobs a tool may offer, with their meaning.
#
# `make options MCU=<mcu> [BOARD=<board>] [KNOB=value ...]` prints each knob's
# value as this configuration resolves it, and whether it applies to the board.
# Front ends (tikubench firmware, the desktop's Firmware tab) read the defaults
# from it.  A combination the Makefile refuses fails with the build's own error.
# The Makefile includes this file last, so every default is already decided.
#
# Output, tab-separated: one header line, the boards this platform builds for,
# then per knob: name, value, kind, group, applies (1/0), help.
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0

TIKU_OPTIONS :=

# $(call tiku_option,NAME,kind,group,where,help) registers a knob:
#   kind   bool | int | enum:<a>+<b>...
#   where  all | arm (every platform but MSP430) | plat:<p>+<q> | mcu:<m>+<n>
#          | cap:<board capability>
#   help   one line; no commas (they split the call's arguments)
define tiku_option
TIKU_OPTIONS += $(1)
opt_kind_$(1)  := $(2)
opt_group_$(1) := $(3)
opt_where_$(1) := $(4)
opt_help_$(1)  := $(5)
endef

# $(call opt_in,a+b,x) -> 1 when x is one of the +-separated names, else 0.
opt_in = $(if $(filter $(2),$(subst +, ,$(1))),1,0)
# $(call opt_applies,where) -> 1 when a knob with that `where` applies to this
# MCU and board, else 0.
opt_applies = $(strip \
  $(if $(filter all,$(1)),1, \
  $(if $(filter arm,$(1)),$(if $(filter msp430,$(TIKU_PLATFORM)),0,1), \
  $(if $(filter plat:%,$(1)), \
    $(call opt_in,$(patsubst plat:%,%,$(1)),$(TIKU_PLATFORM)), \
  $(if $(filter mcu:%,$(1)),$(call opt_in,$(patsubst mcu:%,%,$(1)),$(MCU)), \
  $(if $(filter cap:%,$(1)), \
    $(if $(call board_has,$(patsubst cap:%,%,$(1))),1,0),0))))))
# $(call opt_value,NAME) -> the knob's resolved value; an unset bool reads 0.
opt_value = $(strip \
  $(if $(filter bool,$(opt_kind_$(1))),$(or $($(1)),0),$($(1))))

# --- System ------------------------------------------------------------------
$(eval $(call tiku_option,TIKU_SHELL_ENABLE,bool,System,all,\
  Interactive shell on the console))
$(eval $(call tiku_option,TIKU_SHELL_BASIC_ENABLE,bool,System,all,\
  Tiku BASIC interpreter in the shell))
$(eval $(call tiku_option,TIKU_INIT_ENABLE,bool,System,all,\
  Init system: boot configuration kept in NVM (needs the shell)))
$(eval $(call tiku_option,TIKU_THREADS_ENABLE,bool,System,arm,\
  Preemptive worker threads))
$(eval $(call tiku_option,TIKU_SHELL_COLOR,bool,System,all,Colour in the shell))
$(eval $(call tiku_option,TIKU_ESP32C5_XIP_CODE,bool,System,plat:esp32c5,\
  Run BASIC and shell commands from paired flash code to free internal SRAM))

# --- Console -----------------------------------------------------------------
$(eval $(call tiku_option,UART_BAUD,int,Console,all,\
  Console baud rate (empty: the board default)))
$(eval $(call tiku_option,TIKU_CONSOLE,enum:uart+usb,Console,\
  mcu:rp2350+nrf54lm20a+nrf54lm20b,Console on the UART or on native USB))
ifeq ($(TIKU_PLATFORM),esp32c5)
opt_kind_TIKU_CONSOLE := enum:usb
opt_where_TIKU_CONSOLE := plat:esp32c5
opt_help_TIKU_CONSOLE := Native USB Serial/JTAG console
opt_where_UART_BAUD := plat:esp32c61
endif
$(eval $(call tiku_option,MEMORY_MODEL,enum:small+large,Console,plat:msp430,\
  MSP430 memory model (large puts code and data in upper FRAM)))

# --- Board parts -------------------------------------------------------------
$(eval $(call tiku_option,TIKU_DRV_EMMC_ENABLE,bool,Parts,cap:EMMC,eMMC card))
$(eval $(call tiku_option,TIKU_DRV_PSRAM_ENABLE,bool,Parts,cap:PSRAM,PSRAM))
$(eval $(call tiku_option,TIKU_DRV_NOR_ENABLE,bool,Parts,cap:NOR,NOR flash))
$(eval $(call tiku_option,TIKU_DRV_USB_ENABLE,bool,Parts,cap:USB_RAILS,\
  USB device controller))
$(eval $(call tiku_option,TIKU_DRV_USBHS_ENABLE,bool,Parts,cap:USBHS,\
  USB high-speed device))
$(eval $(call tiku_option,TIKU_DRV_WIFI_CYW43_ENABLE,bool,Parts,cap:CYW43,\
  CYW43439 WiFi))
$(eval $(call tiku_option,TIKU_DRV_BLE_EM9305_ENABLE,bool,Parts,mcu:apollo510b,\
  EM9305 Bluetooth LE radio))

# --- Accelerators and display ------------------------------------------------
$(eval $(call tiku_option,TIKU_DRV_GPU_ENABLE,bool,Accelerators,\
  mcu:apollo510+apollo510b,2.5D GPU))
$(eval $(call tiku_option,TIKU_DRV_DC_ENABLE,bool,Accelerators,\
  mcu:apollo510+apollo510b,Display controller))
$(eval $(call tiku_option,TIKU_AXON_ENABLE,bool,Accelerators,mcu:nrf54lm20b,\
  Axon NPU (needs the Nordic Axon checkout)))
$(eval $(call tiku_option,TIKU_FLPR_ENABLE,bool,Accelerators,\
  mcu:nrf54l15+nrf54lm20a+nrf54lm20b,FLPR RISC-V coprocessor (needs a RISC-V \
  toolchain)))
$(eval $(call tiku_option,TIKU_CRACEN_PK_ENABLE,bool,Accelerators,plat:nordic,\
  CRACEN public-key hardware))
$(eval $(call tiku_option,TIKU_NPU_ENABLE,bool,Accelerators,plat:ra8p1,\
  Ethos-U55 NPU))
$(eval $(call tiku_option,TIKU_DRV_DRW_ENABLE,bool,Accelerators,plat:ra8p1,\
  2D drawing engine))
$(eval $(call tiku_option,TIKU_DRV_GLCDC_ENABLE,bool,Accelerators,plat:ra8p1,\
  GLCDC display controller))
$(eval $(call tiku_option,TIKU_DRV_CPU1_ENABLE,bool,Accelerators,plat:ra8p1,\
  Second core (Cortex-M33)))

# --- Kits (need the tikukits checkout) ---------------------------------------
$(eval $(call tiku_option,TIKU_KIT_CRYPTO_ENABLE,bool,Kits,all,\
  Crypto: AES SHA-256 HMAC CRC Base64))
$(eval $(call tiku_option,TIKU_KIT_TIME_ENABLE,bool,Kits,all,Time and calendar))
$(eval $(call tiku_option,TIKU_KIT_CODEC_ENABLE,bool,Kits,all,\
  Codecs: CBOR JSON protobuf hex))
$(eval $(call tiku_option,TIKU_KIT_MATHS_ENABLE,bool,Kits,all,\
  Maths: matrices statistics distances))
$(eval $(call tiku_option,TIKU_KIT_DS_ENABLE,bool,Kits,all,Data structures))
$(eval $(call tiku_option,TIKU_KIT_ML_ENABLE,bool,Kits,all,\
  Machine learning: regression trees kNN SVM NN))
$(eval $(call tiku_option,TIKU_KIT_SENSORS_ENABLE,bool,Kits,all,\
  Sensor interface))
$(eval $(call tiku_option,TIKU_KIT_SIGFEATURES_ENABLE,bool,Kits,all,\
  Signal features))
$(eval $(call tiku_option,TIKU_KIT_TEXTCOMPRESSION_ENABLE,bool,Kits,all,\
  Text compression))
$(eval $(call tiku_option,TIKU_KIT_GFX_ENABLE,bool,Kits,all,\
  Graphics primitives))
$(eval $(call tiku_option,TIKU_KIT_UI_ENABLE,bool,Kits,all,UI widgets))
$(eval $(call tiku_option,TIKU_KIT_EPAPER_ENABLE,bool,Kits,all,e-paper display))

# --- Networking --------------------------------------------------------------
$(eval $(call tiku_option,TIKU_KIT_NET_ENABLE,bool,Networking,all,\
  IPv4 stack: SLIP on the console or WiFi on the Pico 2 W))
$(eval $(call tiku_option,HAS_TLS,bool,Networking,all,\
  TLS (needs the crypto kit)))
$(eval $(call tiku_option,TIKU_KITS_NET_HTTP_ENABLE,bool,Networking,all,\
  HTTP client))
$(eval $(call tiku_option,TIKU_KITS_NET_MQTT_ENABLE,bool,Networking,all,\
  MQTT client))
$(eval $(call tiku_option,TIKU_KITS_NET_DNS_ENABLE,bool,Networking,all,\
  DNS resolver))
$(eval $(call tiku_option,TIKU_KITS_NET_DHCP_ENABLE,bool,Networking,all,\
  DHCP client))

ifeq ($(TIKU_PLATFORM),esp32c5)
C5_UNAVAILABLE_OPTIONS := $(filter-out TIKU_SHELL_ENABLE TIKU_SHELL_BASIC_ENABLE \
    TIKU_THREADS_ENABLE,$(filter TIKU_%_ENABLE,$(TIKU_OPTIONS)))
$(foreach o,$(C5_UNAVAILABLE_OPTIONS),$(eval opt_where_$(o) := unavailable))
endif

# The boards this platform builds for, the one this build targets first.
opt_boards = $(BOARD) $(filter-out $(BOARD),$(foreach b,$(KNOWN_BOARDS), \
  $(if $(filter $(TIKU_PLATFORM),$(BOARD_PLATFORM_$(b))),$(b))))

TAB := $(shell printf '\t')
# A literal '#': unescaped, it would start a comment.
opt_hash := \#
# The header line: the format name and version (#tiku-options, 1), then MCU,
# BOARD and PLATFORM.
opt_head = $(opt_hash)tiku-options$(TAB)1$(TAB)MCU=$(MCU)$(TAB)$(strip \
  BOARD=$(BOARD))$(TAB)PLATFORM=$(TIKU_PLATFORM)
# $(call opt_row,NAME) -> one knob's row, in the column order above.
opt_row = $(1)$(TAB)$(call opt_value,$(1))$(TAB)$(opt_kind_$(1))$(TAB)$(strip \
  $(opt_group_$(1)))$(TAB)$(call opt_applies,$(opt_where_$(1)))$(TAB)$(strip \
  $(opt_help_$(1)))

# Prints the header line, the boards line and one row per knob; builds
# nothing.
.PHONY: options
options:
	@$(info $(opt_head))
	@$(info $(opt_hash)boards$(TAB)$(strip $(opt_boards)))
	@$(foreach o,$(TIKU_OPTIONS),$(info $(call opt_row,$(o))))
	@:

# --- Build identity -----------------------------------------------------------
# tiku_build_id, which /sys/device/build reports: the short commit hash, with
# a + when tracked files differ from it, then a checksum of MCU, BOARD, APP,
# EXTRA_CFLAGS and every knob above.  tiku_build_board, which
# /sys/device/board reports, is BOARD.  The rule runs on every build but
# rewrites the file only when its text changes, so an unchanged build
# recompiles nothing.
opt_sq := '
# $(call opt_sh,text) -> text single-quoted for the shell.
opt_sh = '$(subst $(opt_sq),$(opt_sq)\$(opt_sq)$(opt_sq),$(1))'
TIKU_BUILD_CONFIG_TEXT = MCU=$(MCU) BOARD=$(BOARD) APP=$(APP) \
  EXTRA_CFLAGS=$(EXTRA_CFLAGS) \
  $(foreach o,$(TIKU_OPTIONS),$(o)=$(call opt_value,$(o)))

opt_git = git -C $(PROJ_DIR)
$(TIKU_BUILD_ID_C): FORCE
	@mkdir -p $(dir $@)
	@c=`$(opt_git) rev-parse --short=7 HEAD 2>/dev/null || echo nogit`; \
	 d=`$(opt_git) status --porcelain --untracked-files=no 2>/dev/null`; \
	 if [ -n "$$d" ]; then c="$$c+"; fi; \
	 h=`printf '%s' $(call opt_sh,$(TIKU_BUILD_CONFIG_TEXT)) | cksum`; \
	 h=`printf '%08x' "$${h%% *}"`; \
	 { printf '/* Generated by tools/firmware_options.mk; do not edit. */\n'; \
	   printf 'const char tiku_build_id[] = "%s %s";\n' "$$c" "$$h"; \
	   printf 'const char tiku_build_board[] = "%s";\n' "$(BOARD)"; \
	 } > $@.tmp; \
	 if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv -f $@.tmp $@; fi

$(TIKU_BUILD_ID_O): $(TIKU_BUILD_ID_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Always out of date, so the build-id rule runs on every build.
.PHONY: FORCE
FORCE:
