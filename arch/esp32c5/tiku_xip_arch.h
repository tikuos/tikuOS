/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_xip_arch.h - C5 paired boot/XIP image integrity contract.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_XIP_ARCH_H_
#define TIKU_ESP32C5_XIP_ARCH_H_
#include <stddef.h>
#include <stdint.h>

#define TIKU_C5_XIP_HEADER_BYTES 64u

/** @brief Compare paired metadata and check payload bounds and CRC; return nonzero on success. */
int tiku_c5_xip_validate(const uint32_t *expected, const uint8_t *mapped, size_t capacity);
/** @brief Halt before application initialization if the flash companion does not match. */
void tiku_c5_xip_require(void);
#endif
