/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.h - ESP32-C61 startup and trap entry points.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_CRT_EARLY_H_
#define TIKU_ESP32C61_CRT_EARLY_H_

#include <stdint.h>

/** @brief Image entry the ROM jumps to (ENTRY in the linker script). */
void tiku_esp32c61_reset_handler(void);

/** @brief C half of the entry: .data, .bss, the trap vector, main(). */
void tiku_esp32c61_c_start(void) __attribute__((noreturn));

/** @brief Trap vector base: exceptions and non-vectored interrupts. */
void tiku_esp32c61_trap_entry(void);

/**
 * @brief Handle one trap; exceptions print a [TM:FAULT] line and park.
 *
 * @param frame  Saved caller-saved registers, ra first
 */
void tiku_esp32c61_trap(uint32_t *frame);

/**
 * @brief Interrupt hook, weak until the interrupt layer provides it.
 *
 * @param line   CPU interrupt line from mcause
 * @param frame  Saved caller-saved registers
 */
void tiku_esp32c61_irq_dispatch(uint32_t line, uint32_t *frame);

#endif /* TIKU_ESP32C61_CRT_EARLY_H_ */
