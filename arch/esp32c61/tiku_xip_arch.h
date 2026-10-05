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

/** @brief "XIP1": the header's first word. */
#define TIKU_ESP32C61_XIP_MAGIC 0x31504958UL

/**
 * @brief The XIP image's header, first in the window.
 *
 * A rebuild that moves the kernel moves _etext and __bss_end, so the two
 * tell an xip.bin from another build apart.
 */
typedef struct {
    uint32_t magic;         /**< TIKU_ESP32C61_XIP_MAGIC */
    uint32_t etext;         /**< this image's _etext */
    uint32_t bss_end;       /**< this image's __bss_end */
} tiku_esp32c61_xip_header_t;

/** @brief Whether xip.bin in flash is this build's; 1 in a build with no
 *         XIP part. */
int tiku_esp32c61_xip_ok(void);

/**
 * @brief Halt with a console message when kernel code runs from the XIP
 *        image and xip.bin in flash is another build's.
 *
 * @note Call at boot, once the console is up.
 */
void tiku_esp32c61_xip_require(void);

#endif /* TIKU_ESP32C61_XIP_ARCH_H_ */
