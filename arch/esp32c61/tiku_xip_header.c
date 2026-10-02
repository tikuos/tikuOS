/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xip_header.c - ESP32-C61 XIP image's header, linked only in a build
 * with an XIP part (xip_head.ld places it first in the window).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "tiku_xip_arch.h"

extern char _etext[];
extern char __bss_end[];

__attribute__((section(".xip.header"), used))
const tiku_esp32c61_xip_header_t tiku_esp32c61_xip_header = {
    TIKU_ESP32C61_XIP_MAGIC, (uint32_t)(uintptr_t)_etext,
    (uint32_t)(uintptr_t)__bss_end
};
