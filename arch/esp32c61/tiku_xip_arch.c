/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xip_arch.c - ESP32-C61 check that xip.bin is this build's.
 *
 * The header and the code section's bounds are link-time facts, read through
 * weak references: a build with no XIP part links neither, and passes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>

#include <hal/tiku_printf_hal.h>
#include "tiku_xip_arch.h"

extern char _etext[];
extern char __bss_end[];

/* tiku_xip_header.c, linked only with an XIP part. */
extern const tiku_esp32c61_xip_header_t tiku_esp32c61_xip_header
    __attribute__((weak));

/* xip_code.ld's bounds, defined only when kernel code runs from flash. */
extern char __tiku_xip_code_start[] __attribute__((weak));
extern char __tiku_xip_code_end[] __attribute__((weak));

int tiku_esp32c61_xip_ok(void) {
    /* Through a volatile view: the compiler knows the initializer, but the
     * flash may hold an older build's bytes. */
    const volatile tiku_esp32c61_xip_header_t *h =
        (const volatile tiku_esp32c61_xip_header_t *)&tiku_esp32c61_xip_header;

    if (&tiku_esp32c61_xip_header == NULL) {
        return 1;
    }
    return h->magic == TIKU_ESP32C61_XIP_MAGIC &&
           h->etext == (uint32_t)(uintptr_t)_etext &&
           h->bss_end == (uint32_t)(uintptr_t)__bss_end;
}

/** @brief @p p as an integer the compiler cannot fold: two absent weak
 *         symbols are equal, yet it may assume distinct symbols differ. */
static uintptr_t opaque(const void *p) {
    uintptr_t a = (uintptr_t)p;

    __asm__ volatile ("" : "+r"(a));
    return a;
}

void tiku_esp32c61_xip_require(void) {
    if (opaque(__tiku_xip_code_end) == opaque(__tiku_xip_code_start) ||
        tiku_esp32c61_xip_ok()) {
        return;
    }
    TIKU_PRINTF("xip.bin in flash is not this build's, and kernel code runs "
                "from it -- make flash writes both images\n");
    for (;;) {
    }
}
