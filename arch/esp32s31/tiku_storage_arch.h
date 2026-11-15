/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_storage_arch.h - Reserved flash storage and its PSRAM read shadow.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_STORAGE_ARCH_H_
#define TIKU_S31_STORAGE_ARCH_H_
#include <stddef.h>
#include <stdint.h>
#define TIKU_S31_STORAGE_START 0x00e40000u
#define TIKU_S31_REGION_BYTES  0x001b0000u
#define TIKU_S31_MIRROR_A      0x00ff0000u
#define TIKU_S31_MIRROR_B      0x00ff4000u
#define TIKU_S31_MIRROR_BYTES  0x00004000u
#define TIKU_S31_STORAGE_END   0x00ff8000u
#define TIKU_S31_STORAGE_SHADOW (TIKU_S31_REGION_BYTES + 2u * TIKU_S31_MIRROR_BYTES)
/** @brief Restore flash and replay interrupted sector updates before tier initialization. */
int tiku_s31_storage_init(void);
/** @brief End blank-region provisioning after the layout service establishes ownership. */
void tiku_s31_storage_seal(void);
/** @brief Read, erase or program reserved flash using aligned internal SRAM buffers. */
int tiku_s31_storage_io(unsigned operation, uint32_t address, void *buffer, size_t length);
/** @brief Return nonzero in CPU0 kernel foreground context. */
int tiku_s31_storage_context(void);
#endif
