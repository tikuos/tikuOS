/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_xip_arch.h - Paired SRAM/flash executable integrity contract.
 *
 * Code linked to the XIP window may run only after tiku_p4_xip_init()
 * returns zero.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_XIP_ARCH_H_
#define TIKU_ESP32P4_XIP_ARCH_H_
#include <stddef.h>
#include <stdint.h>

/** @brief Check aligned paired headers, reserved fields, identity and payload CRC.
 * @return Nonzero for a complete matching companion, zero on invalid input.
 */
int tiku_p4_xip_validate(const uint32_t *expected, const uint8_t *mapped,
                         size_t capacity);
/** @brief Map flash and require the linked executable companion before use.
 * @return Zero on success; -1 leaves execution of companion code forbidden.
 */
int tiku_p4_xip_init(void);
#endif
