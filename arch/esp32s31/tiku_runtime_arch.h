/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_runtime_arch.h - S31 SRAM trap frame and native runtime interface.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32S31_RUNTIME_ARCH_H_
#define TIKU_ESP32S31_RUNTIME_ARCH_H_
#define S31_FRAME_SIZE 272
#define S31_FRAME_PC 128
#define S31_FRAME_STATUS 132
#define S31_FRAME_CAUSE 136
#define S31_FRAME_FCSR 140
#define S31_FRAME_F0 144
#ifndef __ASSEMBLER__
#include <stdint.h>
/** @brief Nonzero during the non-nested trap dispatcher. */
extern volatile uint8_t tiku_s31_in_trap;
/** @brief Initialize or reclaim the C-library state for a worker slot. */
void tiku_s31_reent_init(uint8_t slot);
/** @brief Initialize CPU0 clocks, interrupt routing and the system timer. */
void tiku_s31_runtime_init(void);
/** @brief Return the wrapping 52-bit crystal-derived counter. */
uint64_t tiku_s31_time(void);
/** @brief Dispatch a non-nested machine trap and return its resume frame. */
uint32_t *tiku_s31_trap(uint32_t *frame);
/** @brief Write a terminated string through UART0 with bounded FIFO waits. */
void tiku_s31_puts(const char *text);
/** @brief Write a named hexadecimal counter through UART0. */
void tiku_s31_report(const char *name, uint32_t value);
/** @brief Report a runtime failure and halt with interrupts masked. */
void tiku_s31_fatal(const char *reason) __attribute__((noreturn));
/** @brief Return the number of timer interrupts delivered since initialization. */
uint32_t tiku_s31_timer_interrupts(void);
/** @brief Verify FPU register and control preservation across an ECALL. */
uint32_t tiku_s31_fpu_probe(void);
/** @brief Claim CPU0 line 20..31 and a source 0..168; return -1 on conflict.
 * Calls require CPU0 thread context. Lines start disabled. The handler runs
 * with machine interrupts masked and must acknowledge its peripheral.
 */
int tiku_s31_irq_claim(unsigned line, unsigned source, unsigned priority,
                       void (*handler)(void));
/** @brief Count handler entries since a line was claimed; invalid lines return zero. */
uint32_t tiku_s31_irq_deliveries(unsigned line);
/** @brief Enable a claimed line, or disable it; return -1 for an unowned line. */
int tiku_s31_irq_enable(unsigned line, int enable);
/** @brief Release a matching claim and restore its matrix route; return -1 on mismatch. */
int tiku_s31_irq_release(unsigned line, void (*handler)(void));
/** @brief Arm SYSTIMER comparator 2, owned through source 35; return -1 without a claim. */
int tiku_s31_driver_alarm(uint64_t deadline, int enable);
#endif
#endif
