/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_settings_arch.h - Alternating flash records in the S31 settings reservation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_SETTINGS_ARCH_H_
#define TIKU_S31_SETTINGS_ARCH_H_
#include <stddef.h>
/** @brief Maximum stored record size; the final two 4 KiB flash sectors are reserved. */
#define TIKU_S31_SETTINGS_BYTES 512u
/** @brief Read the newest record; return 0, 1 if empty, or -1 on invalid storage/context. */
int tiku_s31_settings_read(void *data, size_t capacity, size_t *length);
/** @brief Commit a record, then erase its predecessor; NULL/zero commits an empty record. */
int tiku_s31_settings_write(const void *data, size_t length);
#endif
