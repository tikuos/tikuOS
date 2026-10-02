/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xip_arch.h - ESP32-C61 code that runs from flash: the XIP image.
 *
 * A build that places code in the XIP window writes it as xip.bin at flash
 * 1 MB.  Its header names where this image's text and bss end, so one left
 * by another build is told apart before anything calls into it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_XIP_ARCH_H_
#define TIKU_ESP32C61_XIP_ARCH_H_

#include <stdint.h>

/** "XIP1": the header's first word. */
#define TIKU_ESP32C61_XIP_MAGIC 0x31504958UL

/** @brief The XIP image's header, first in the window. */
typedef struct {
    uint32_t magic;
    uint32_t etext;         /* this image's _etext and __bss_end: any */
    uint32_t bss_end;       /* rebuild that moves the kernel moves them */
} tiku_esp32c61_xip_header_t;

/** @brief Whether xip.bin in flash is this build's; 1 in a build with no
 *         XIP part. */
int tiku_esp32c61_xip_ok(void);

/** @brief At boot, once the console is up: with kernel code in the XIP
 *         image, halt -- saying why -- if xip.bin is another build's. */
void tiku_esp32c61_xip_require(void);

#endif /* TIKU_ESP32C61_XIP_ARCH_H_ */
