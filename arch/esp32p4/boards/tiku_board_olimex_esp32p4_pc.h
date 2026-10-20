/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_board_olimex_esp32p4_pc.h - Olimex ESP32-P4-PC board profile.
 *
 * The board's pin wiring is not recorded here: every pad is reserved, so the
 * GPIO API reads levels and refuses every change, and no LED is defined.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_BOARD_OLIMEX_ESP32P4_PC_H_
#define TIKU_BOARD_OLIMEX_ESP32P4_PC_H_

#define TIKU_BOARD_NAME "Olimex ESP32-P4-PC"

/* GPIO0..54, all reserved. */
#define TIKU_BOARD_GPIO_RESERVED ((1ULL << 55) - 1u)

/* /dev/gpio pin files are read-only on this board. */
#define TIKU_BOARD_GPIO_READONLY 1

#endif
