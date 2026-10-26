/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_layout_arch.h - Native SRAM bounds and reserved cold-entry storage.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_LAYOUT_ARCH_H
#define TIKU_C6_LAYOUT_ARCH_H
#if TIKU_C6_FIRMWARE
#define C6_ARENA_BYTES 409600
#define C6_COLD_LOG 0x4086df00
#else
#define C6_ARENA_BYTES 65536
#define C6_COLD_LOG 0x40819f00
#endif
#endif
