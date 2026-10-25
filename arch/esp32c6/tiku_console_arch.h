/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_console_arch.h - native C6 fixed-function USB Serial/JTAG byte console.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_CONSOLE_ARCH_H_
#define TIKU_C6_CONSOLE_ARCH_H_
#include <services/console/tiku_console.h>
/** @brief Bounded USB byte callbacks; absent-host output is discarded and counted. */
const tiku_console_wire_t *tiku_c6_console_io(void);
/** @brief Number of bytes refused by the physical USB transmit FIFO. */
unsigned tiku_c6_console_drops(void);
#endif
