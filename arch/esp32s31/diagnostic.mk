# Tiku Operating System v0.06
# Simple. Persistent. Intelligent.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# diagnostic.mk - Build the S31 SRAM diagnostic without the firmware profile.
#
# SPDX-License-Identifier: Apache-2.0

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
OUT ?= $(ROOT)/build/esp32s31-diagnostic
CROSS_COMPILE ?= riscv32-esp-elf-
PYTHON ?= python3
CC := $(CROSS_COMPILE)gcc
ARCH := $(ROOT)/arch/esp32s31
FLAGS := -march=rv32imac_zicsr_zifencei -mabi=ilp32 -mcmodel=medany \
         -msmall-data-limit=0 -Os -g3 -ffreestanding -fno-builtin \
         -fno-stack-protector -Wall -Wextra -Werror -nostdlib

.PHONY: all
all: $(OUT)/diagnostic.bin

$(OUT):
	mkdir -p $@

$(OUT)/diagnostic.elf: $(ARCH)/diagnostic_start.S $(ARCH)/diagnostic.c \
                     $(ARCH)/diagnostic.ld | $(OUT)
	$(CC) $(FLAGS) -Wl,--build-id=none,-Map,$(OUT)/diagnostic.map \
	    -T $(ARCH)/diagnostic.ld $(ARCH)/diagnostic_start.S \
	    $(ARCH)/diagnostic.c -o $@

$(OUT)/diagnostic.bin: $(OUT)/diagnostic.elf
	$(PYTHON) -m esptool --chip esp32s31 elf2image --flash-size 16MB \
	    --output $@ $<
