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

/* The trap frame, in words: every integer register but zero, sp, gp and tp,
 * then the CSRs mret depends on.  A thread switch resumes another frame. */
#define TIKU_ESP32C61_F_RA          0U
#define TIKU_ESP32C61_F_A0          6U
#define TIKU_ESP32C61_F_MEPC        28U
#define TIKU_ESP32C61_F_MSTATUS     29U
#define TIKU_ESP32C61_F_MCAUSE      30U
#define TIKU_ESP32C61_FRAME_WORDS   32U
#define TIKU_ESP32C61_FRAME_BYTES   (TIKU_ESP32C61_FRAME_WORDS * 4U)

/* Handlers never nest (MIE stays off in them), so one stack serves all. */
#define TIKU_ESP32C61_ISR_STACK_WORDS 512U

/** @brief Image entry the ROM jumps to (ENTRY in the linker script). */
void tiku_esp32c61_reset_handler(void);

/** @brief C half of the entry: .data, .bss, the trap vector, main(). */
void tiku_esp32c61_c_start(void) __attribute__((noreturn));

/** @brief Trap vector base: exceptions and non-vectored interrupts. */
void tiku_esp32c61_trap_entry(void);

/**
 * @brief Handle one trap; exceptions print a [TM:FAULT] line, then go to
 *        tiku_esp32c61_fault().
 *
 * @param frame  The saved context
 * @return The frame to resume
 */
uint32_t *tiku_esp32c61_trap(uint32_t *frame);

/**
 * @brief Interrupt hook: tiku_irq_arch.c defines it, and a weak default in
 *        tiku_crt_early.c returns @p frame.
 *
 * @param id     CLIC id from mcause
 * @param frame  The saved context
 * @return The frame to resume
 */
uint32_t *tiku_esp32c61_irq_dispatch(uint32_t id, uint32_t *frame);

/** @brief Non-zero while a handler runs: the stack is the ISR stack. */
int tiku_esp32c61_in_isr(void);

/** @brief Called after an exception's dump: the weak default parks, and
 *         tiku_fault_arch.c's version records the fault and resets. */
void tiku_esp32c61_fault(uint32_t *frame, uint32_t cause)
    __attribute__((noreturn));

#endif /* TIKU_ESP32C61_CRT_EARLY_H_ */
