/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_settings_internal.h - Settings record backend's SRAM flash operations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_SETTINGS_INTERNAL_H_
#define TIKU_S31_SETTINGS_INTERNAL_H_
#include <stdint.h>
#include <stddef.h>
/** @brief Execute a ROM read (0), sector erase (1), or write (2), returning 0 or -1.
 * Buffers require SRAM residency, 4-byte alignment and a whole-word length. */
int tiku_s31_settings_io(unsigned operation, uint32_t address, void *buffer, size_t length);
/** @brief Whether the caller runs in the CPU0 kernel thread outside an interrupt. */
int tiku_s31_settings_context(void);
#endif
