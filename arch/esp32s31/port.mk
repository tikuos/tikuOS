# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# port.mk - Build native S31 kernel qualification in SRAM.
#
# SPDX-License-Identifier: Apache-2.0

BOARD ?= esp32s31_korvo1
ifneq ($(BOARD),esp32s31_korvo1)
$(error ESP32-S31 requires BOARD=esp32s31_korvo1)
endif
S31_PROFILE ?= kernel-test
ifeq ($(filter $(S31_PROFILE),firmware kernel-test board-test shell watchdog-test flash-shell psram-test flash-psram-test memory-test flash-memory-test display-test flash-display-test sd-test audio-test radio-os-test phy-test flash-wifi-test),)
$(error S31_PROFILE must be firmware, kernel-test, board-test, shell, watchdog-test, flash-shell, psram-test, flash-psram-test, memory-test, flash-memory-test, display-test flash-display-test sd-test, audio-test, radio-os-test, phy-test or flash-wifi-test)
endif
S31_ENABLED := $(strip $(foreach v,$(filter TIKU_%_ENABLE,$(.VARIABLES)),\
    $(if $(filter 1,$($(v))),$(v))))
ifneq ($(filter-out TIKU_THREADS_ENABLE,$(S31_ENABLED)),)
$(error S31 profiles select optional services through S31_PROFILE)
endif
ifdef TIKU_THREADS_ENABLE
ifneq ($(TIKU_THREADS_ENABLE),1)
$(error S31 profiles require TIKU_THREADS_ENABLE=1)
endif
endif
CROSS_COMPILE ?= riscv32-esp-elf-
PYTHON ?= python3
S31_CC := $(CROSS_COMPILE)gcc
S31_DIR := build/esp32s31/$(S31_PROFILE)
S31_ARCH := -march=rv32imafc_zicsr_zifencei -mabi=ilp32f
S31_FLAGS := $(S31_ARCH) -Os -g3 -Wall -Wextra -Werror \
    -Wno-missing-field-initializers -ffreestanding -fno-builtin \
    -ffunction-sections -fdata-sections -I. -DPLATFORM_ESP32S31=1 \
    -DTIKU_BOARD_ESP32S31_KORVO1=1 -DTIKU_THREADS_ENABLE=1 \
    -DTIKU_MEM_NVM_ENABLE=0
S31_SRCS := arch/esp32s31/tiku_runtime_arch.c arch/esp32s31/tiku_watchdog_arch.c \
    arch/esp32s31/tiku_i2c_arch.c interfaces/bus/tiku_i2c_bus.c \
    interfaces/gpio/tiku_gpio_owner.c \
    kernel/process/tiku_process.c kernel/scheduler/tiku_sched.c \
    kernel/timers/tiku_clock.c kernel/timers/tiku_timer.c \
    kernel/timers/tiku_htimer.c kernel/timers/tiku_crit.c \
    kernel/threads/tiku_thread.c kernel/vfs/tiku_vfs.c \
    kernel/cpu/tiku_hang.c
ifneq ($(filter firmware shell flash-shell,$(S31_PROFILE)),)
S31_SHELL_COMMANDS := HELP PS TIMER QUEUE ECHO CALC HTIMER CLEAR
ifeq ($(S31_PROFILE),firmware)
S31_SHELL_COMMANDS += WIFI IP PING
endif
S31_SHELL_FLAGS := $(shell awk '$$1 == sprintf("%cifndef",35) && $$2 ~ /^TIKU_SHELL_CMD_/ {print $$2}' shell/tiku_shell_config.h)
S31_FLAGS += -DTIKU_SHELL_ENABLE=1 -DTIKU_SHELL_ALIASES_ENABLE=0 \
    $(foreach v,$(sort $(S31_SHELL_FLAGS)),-D$(v)=$(if $(filter $(patsubst TIKU_SHELL_CMD_%,%,$(v)),$(S31_SHELL_COMMANDS)),1,0))
S31_SRCS += arch/esp32s31/tiku_psram_arch.c arch/esp32s31/tiku_mem_arch.c \
    kernel/memory/tiku_tier.c kernel/memory/tiku_region.c \
    kernel/cpu/tiku_watchdog.c kernel/memory/tiku_mem.c boot/tiku_s31_ram.c shell/tiku_shell.c shell/tiku_shell_io.c \
    shell/tiku_shell_parser.c shell/tiku_shell_cwd.c services/console/tiku_console.c \
    $(foreach c,ps timer queue echo calc clear,shell/commands/tiku_shell_cmd_$(c).c)
else ifneq ($(filter display-test flash-display-test,$(S31_PROFILE)),)
S31_DISPLAY_SOAK_SECONDS ?= 0
S31_FLAGS += -DTIKU_S31_DISPLAY_TEST=1 -DTIKU_GPIO_CLAIM_LIMIT=32 \
    -DTIKU_S31_DISPLAY_SOAK_SECONDS=$(S31_DISPLAY_SOAK_SECONDS)
S31_SRCS += arch/esp32s31/tiku_psram_arch.c arch/esp32s31/tiku_mem_arch.c \
    arch/esp32s31/tiku_dma_arch.c arch/esp32s31/tiku_display_arch.c \
    kernel/memory/tiku_mem.c kernel/memory/tiku_tier.c kernel/memory/tiku_region.c \
    interfaces/display/tiku_display.c TikuBench/tests/esp32s31/test_display.c \
    drivers/input/gt1151/tiku_drv_input_gt1151.c
endif
ifneq ($(filter firmware flash-wifi-test,$(S31_PROFILE)),)
S31_WIFI_COUNTRY ?= 01
S31_FLAGS += -DTIKU_S31_FLASH_BOOT=1 \
    -DTIKU_S31_WIFI_COUNTRY='"$(S31_WIFI_COUNTRY)"'
S31_SRCS += arch/esp32s31/tiku_trng_arch.c arch/esp32s31/tiku_mem_arch.c \
    arch/esp32s31/tiku_image_desc.c kernel/memory/tiku_mem.c \
    kernel/memory/tiku_tier.c kernel/memory/tiku_region.c \
    drivers/wifi/esp/esp_core.c drivers/wifi/esp/esp_osi.c \
    drivers/wifi/esp/esp_heap.c drivers/wifi/esp/esp_glue.c \
    drivers/wifi/esp/esp_crypto.c drivers/wifi/esp/esp_wpa.c \
    drivers/wifi/esp/tiku_drv_wifi_esp.c drivers/wifi/esp/s31/wifi_s31_arch.c \
    drivers/wifi/esp/s31/phy_s31.c drivers/wifi/esp/s31/wifi_s31_phy.c \
    drivers/wifi/esp/s31/radio_memory.c
ifeq ($(S31_PROFILE),flash-wifi-test)
S31_FLAGS += -DTIKU_S31_RADIO_TEST=1
S31_SRCS += TikuBench/tests/esp32s31/test_wifi.c
endif
S31_SRCS += $(addprefix tikukits/crypto/,sha1/tiku_kits_crypto_sha1.c \
    hmac/tiku_kits_crypto_hmac_sha1.c pbkdf2/tiku_kits_crypto_pbkdf2.c \
    aes128/tiku_kits_crypto_aes128.c aeskw/tiku_kits_crypto_aeskw.c \
    sha256/tiku_kits_crypto_sha256.c ccm/tiku_kits_crypto_ccm.c)
ifneq ($(strip $(S31_WIFI_TEST_HEADER)),)
S31_FLAGS += -include $(S31_WIFI_TEST_HEADER) -DTIKU_S31_WIFI_DATA_TEST=1 \
    -DTIKU_KITS_NET_DHCP_ENABLE=1 -DTIKU_KITS_NET_TCP_ENABLE=0 -DTIKU_KITS_NET_MTU=1500
S31_LDFLAGS += -Wl,--wrap=tiku_wireless_set_rx,--wrap=tiku_wireless_tx_eth,--wrap=esp_wifi_set_sta_key_internal,--wrap=esp_wifi_internal_reg_rxcb
S31_SRCS += TikuBench/tests/esp32s31/test_wifi_data.c \
    tikukits/net/wifi/tiku_kits_net_wifi.c tikukits/net/ipv4/tiku_kits_net_ipv4.c \
    tikukits/net/ipv4/tiku_kits_net_icmp.c tikukits/net/ipv4/tiku_kits_net_udp.c \
    tikukits/net/ipv4/tiku_kits_net_dhcp.c tikukits/net/slip/tiku_kits_net_slip.c
endif
S31_RADIO_VENDOR := drivers/wifi/esp/s31/vendor
S31_RADIO_HASH := $(shell cd drivers/wifi/esp/s31 && sha256sum -c SHA256SUMS >/dev/null 2>&1 && echo yes)
ifneq ($(S31_RADIO_HASH),yes)
$(error S31 radio assets missing or invalid: run python3 drivers/wifi/esp/s31/fetch.py)
endif
S31_LDFLAGS += $(addprefix -T$(S31_RADIO_VENDOR)/esp32s31.rom,.ld .phy.ld .version.ld .api.ld .pp.ld .net80211.ld .coexist.ld)
S31_LIBS += $(addprefix $(S31_RADIO_VENDOR)/,libnet80211.a libpp.a libcore.a libphy.a) -lm
else ifneq ($(filter shell flash-shell display-test flash-display-test,$(S31_PROFILE)),)
else ifeq ($(S31_PROFILE),phy-test)
S31_SRCS += arch/esp32s31/tiku_trng_arch.c drivers/wifi/esp/s31/wifi_s31_arch.c \
    drivers/wifi/esp/s31/phy_s31.c TikuBench/tests/esp32s31/test_radio_phy.c
S31_RADIO_VENDOR := drivers/wifi/esp/s31/vendor
S31_RADIO_HASH := $(shell cd drivers/wifi/esp/s31 && sha256sum -c SHA256SUMS >/dev/null 2>&1 && echo yes)
ifneq ($(S31_RADIO_HASH),yes)
$(error S31 radio assets missing or invalid: run python3 drivers/wifi/esp/s31/fetch.py)
endif
S31_LDFLAGS += $(addprefix -T$(S31_RADIO_VENDOR)/esp32s31.rom,.ld .phy.ld .version.ld)
S31_LIBS += $(S31_RADIO_VENDOR)/libphy.a -lm
else ifeq ($(S31_PROFILE),radio-os-test)
S31_FLAGS += -DTIKU_S31_RADIO_OS_TEST=1
S31_SRCS += arch/esp32s31/tiku_trng_arch.c
S31_SRCS += drivers/wifi/esp/esp_osi.c drivers/wifi/esp/esp_heap.c \
    drivers/wifi/esp/esp_npl.c drivers/wifi/esp/s31/wifi_s31_arch.c \
    drivers/wifi/esp/s31/btdm_s31_osal.c drivers/wifi/esp/esp_mempool.c \
    drivers/wifi/esp/s31/btdm_s31_mempool.c TikuBench/tests/esp32s31/test_radio_os.c
else ifeq ($(S31_PROFILE),audio-test)
S31_FLAGS += -DTIKU_S31_AUDIO_TEST=1
S31_SRCS += arch/esp32s31/tiku_audio_arch.c arch/esp32s31/tiku_psram_arch.c \
    arch/esp32s31/tiku_mem_arch.c kernel/memory/tiku_mem.c \
    kernel/memory/tiku_tier.c kernel/memory/tiku_region.c \
    drivers/audio/es8389/tiku_drv_audio_es8389.c TikuBench/tests/esp32s31/test_audio.c
ifeq ($(wildcard drivers/audio/es8389/tiku_drv_audio_es8389.c),)
$(error audio-test requires the optional drivers/audio/es8389 checkout)
endif
else ifeq ($(S31_PROFILE),sd-test)
S31_FLAGS += -DTIKU_S31_SD_TEST=1
S31_SRCS += arch/esp32s31/tiku_spi_arch.c arch/esp32s31/tiku_sdcard_arch.c \
    kernel/fs/tiku_media_probe.c TikuBench/tests/esp32s31/test_sd.c
else ifneq ($(filter memory-test flash-memory-test,$(S31_PROFILE)),)
S31_FLAGS += -DTIKU_S31_MEMORY_TEST=1
S31_SRCS += arch/esp32s31/tiku_psram_arch.c arch/esp32s31/tiku_mem_arch.c \
    arch/esp32s31/tiku_dma_arch.c kernel/memory/tiku_mem.c \
    kernel/memory/tiku_tier.c kernel/memory/tiku_region.c \
    TikuBench/tests/esp32s31/test_memory.c
else ifneq ($(filter psram-test flash-psram-test,$(S31_PROFILE)),)
S31_FLAGS += -DTIKU_S31_PSRAM_TEST=1
S31_SRCS += arch/esp32s31/tiku_psram_arch.c TikuBench/tests/esp32s31/test_psram.c
else ifeq ($(S31_PROFILE),watchdog-test)
S31_SRCS += TikuBench/tests/esp32s31/test_watchdog.c
else
S31_SRCS += TikuBench/tests/esp32s31/test_kernel.c TikuBench/tests/esp32s31/test_i2c.c
endif
ifneq ($(filter board-test display-test flash-display-test,$(S31_PROFILE)),)
ifeq ($(wildcard drivers/input/gt1151/tiku_drv_input_gt1151.c),)
$(error $(S31_PROFILE) requires the optional drivers/input/gt1151 checkout)
endif
endif
ifneq ($(filter radio-os-test phy-test flash-wifi-test,$(S31_PROFILE)),)
S31_SRCS += drivers/wifi/esp/s31/radio_memory.c
endif
ifeq ($(S31_PROFILE),board-test)
S31_FLAGS += -DS31_TOUCH_TEST=1
S31_SRCS += drivers/input/gt1151/tiku_drv_input_gt1151.c
endif
S31_LINKER := arch/esp32s31/diagnostic.ld
ifneq ($(filter radio-os-test phy-test,$(S31_PROFILE)),)
S31_LINKER := arch/esp32s31/radio.ld
endif
ifneq ($(filter flash-shell flash-psram-test flash-memory-test flash-display-test,$(S31_PROFILE)),)
S31_FLAGS += -DTIKU_S31_FLASH_BOOT=1
S31_LINKER := arch/esp32s31/flash.ld
S31_SRCS += arch/esp32s31/tiku_image_desc.c
endif
ifneq ($(filter firmware flash-wifi-test,$(S31_PROFILE)),)
S31_LINKER := arch/esp32s31/radio_flash.ld
endif
ifeq ($(S31_PROFILE),firmware)
S31_FLAGS += -DTIKU_S31_WIFI_ENABLE=1 -DTIKU_DRV_WIFI_ESP_ENABLE=1 -DTIKU_ESP_PROFILE_ENABLE=1 \
    -DTIKU_WIRELESS_AP_ENABLE=1 -DTIKU_ESP_SAE_ENABLE=1 \
    -DTIKU_KIT_NET_ENABLE=1 -DTIKU_KIT_NET_MIN=1 -DTIKU_KITS_NET_WIFI_ENABLE=1 \
    -DTIKU_KITS_NET_DHCP_ENABLE=1 -DTIKU_KITS_NET_TCP_ENABLE=0 -DTIKU_KITS_NET_MTU=1500
S31_LDFLAGS += -Wl,--defsym=S31_FIRMWARE=1
S31_SRCS += arch/esp32s31/tiku_settings_arch.c arch/esp32s31/tiku_settings_io.c drivers/wifi/esp/s31/wifi_s31_profile.c \
    tikukits/crypto/gcm/tiku_kits_crypto_gcm.c tikukits/crypto/pbkdf2/tiku_kits_crypto_pbkdf2_sha256.c \
    drivers/wifi/esp/esp_ap.c drivers/wifi/esp/esp_sae.c \
    shell/commands/tiku_shell_cmd_wifi.c shell/commands/tiku_shell_cmd_ip.c \
    shell/commands/tiku_shell_cmd_ping.c shell/commands/tiku_shell_cmd_slip.c \
    tikukits/net/wifi/tiku_kits_net_wifi.c tikukits/net/slip/tiku_kits_net_slip.c \
    $(addprefix tikukits/net/ipv4/tiku_kits_net_,ipv4.c icmp.c udp.c dhcp.c dhcps.c)
S31_SAE_HASH := $(shell cd drivers/wifi/esp/sae && sha256sum -c SHA256SUMS >/dev/null 2>&1 && echo yes)
ifneq ($(S31_SAE_HASH),yes)
$(error S31 SAE vendor assets failed SHA256 verification)
endif
include drivers/wifi/esp/sae/sources.mk
S31_SRCS += $(ESP_SAE_SRCS)
$(addprefix $(S31_DIR)/,$(ESP_SAE_SRCS:.c=.o)): S31_FLAGS += $(ESP_SAE_FLAGS) -Wno-unused-parameter
endif
S31_SRCS := $(sort $(S31_SRCS))
S31_ASM := arch/esp32s31/tiku_crt_arch.S
S31_OBJS := $(patsubst %.c,$(S31_DIR)/%.o,$(S31_SRCS)) \
    $(patsubst %.S,$(S31_DIR)/%.o,$(S31_ASM))
.PHONY: all ram-load clean FORCE
all: $(S31_DIR)/kernel.bin
$(S31_DIR)/.flags: FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(S31_CC) $(S31_FLAGS)' > $@.new
	@cmp -s $@.new $@ || cp $@.new $@
	@rm -f $@.new
$(S31_DIR)/%.o: %.c $(S31_DIR)/.flags
	@mkdir -p $(@D)
	$(S31_CC) $(S31_FLAGS) -MMD -MP -c $< -o $@
$(S31_DIR)/%.o: %.S $(S31_DIR)/.flags
	@mkdir -p $(@D)
	$(S31_CC) $(S31_FLAGS) -MMD -MP -c $< -o $@
$(S31_DIR)/kernel.elf: $(S31_OBJS) $(S31_LINKER) arch/esp32s31/port.mk
	$(S31_CC) $(S31_ARCH) -nostdlib -Wl,--gc-sections,--build-id=none \
	    -Wl,-Map=$(S31_DIR)/kernel.map -T$(S31_LINKER) \
	    $(S31_LDFLAGS) $(S31_OBJS) -Wl,--start-group $(S31_LIBS) -lc -lnosys -lgcc -Wl,--end-group -o $@
$(S31_DIR)/kernel.bin: $(S31_DIR)/kernel.elf
	$(PYTHON) -m esptool --chip esp32s31 elf2image --flash-size 16MB -o $@ $<
ram-load: all
ifneq ($(filter flash-shell flash-psram-test flash-memory-test flash-display-test flash-wifi-test firmware,$(S31_PROFILE)),)
	$(error flash profiles require the installed second-stage bootloader)
endif
	$(PYTHON) tools/esp32s31_ram.py --image $(S31_DIR)/kernel.bin \
	    --port '$(PORT)' --serial '$(S31_SERIAL)' --$(if $(filter firmware shell flash-shell,$(S31_PROFILE)),shell-test,$(if $(filter watchdog-test board-test psram-test memory-test display-test sd-test audio-test radio-os-test phy-test,$(S31_PROFILE)),$(S31_PROFILE),kernel-test)) --seconds $(if $(filter audio-test,$(S31_PROFILE)),95,$(if $(filter psram-test memory-test display-test sd-test audio-test radio-os-test phy-test,$(S31_PROFILE)),60,6)) --log '$(LOG)'
clean:
	$(RM) $(S31_OBJS) $(S31_OBJS:.o=.d) $(S31_DIR)/kernel.elf \
	    $(S31_DIR)/kernel.bin $(S31_DIR)/kernel.map $(S31_DIR)/.flags
-include $(S31_OBJS:.o=.d)
