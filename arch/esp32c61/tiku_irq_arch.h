/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_irq_arch.h - ESP32-C61 interrupt lines over the matrix and the CLIC.
 *
 * A peripheral source reaches the core on one of 32 CPU lines; each line has
 * a handler, a level and an enable.  The lines in use are fixed here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_IRQ_ARCH_H_
#define TIKU_ESP32C61_IRQ_ARCH_H_

#include <stdint.h>

#include "tiku_esp32c61_regs.h"

/* The map, whole: every line this port claims. */
#define TIKU_ESP32C61_LINE_TICK     10U
#define TIKU_ESP32C61_LINE_HTIMER   11U
#define TIKU_ESP32C61_LINE_GPIO     12U
#define TIKU_ESP32C61_LINE_UART0    13U

/* Levels run 1..7; the timers outrank everything that may join them. */
#define TIKU_ESP32C61_LEVEL_TIMER   7U
#define TIKU_ESP32C61_LEVEL_DEFAULT 3U

/** @brief A line's handler; the source is cleared inside it, not after. */
typedef void (*tiku_esp32c61_isr_t)(void);

/** @brief mstatus.MIE off. @return Its previous state, for _mie_restore() */
static inline uint32_t tiku_esp32c61_mie_off(void) {
    uint32_t s;

    __asm__ volatile ("csrrc %0, mstatus, %1"
                      : "=r" (s) : "r" (ESP32C61_MSTATUS_MIE) : "memory");
    return s & ESP32C61_MSTATUS_MIE;
}

/** @brief Put back the state _mie_off() returned. @param s  That state */
static inline void tiku_esp32c61_mie_restore(uint32_t s) {
    if (s != 0UL) {
        __asm__ volatile ("csrs mstatus, %0" :: "r" (s) : "memory");
    }
}

/** @brief Unroute every source and quiet every line; MIE is left off. */
void tiku_esp32c61_irq_init(void);

/**
 * @brief Route @p source to @p line at @p level and give the line @p isr.
 *
 * The line is left disabled; level-triggered, which every source here is.
 */
void tiku_esp32c61_irq_attach(unsigned line, unsigned source,
                              unsigned level, tiku_esp32c61_isr_t isr);

/** @brief Enable one line. @param line  0..31 */
void tiku_esp32c61_irq_enable(unsigned line);

/** @brief Disable one line; done when this returns. @param line  0..31 */
void tiku_esp32c61_irq_disable(unsigned line);

/** @brief Lines enabled now, one bit each. @return The set */
uint32_t tiku_esp32c61_irq_enabled(void);

/** @brief Make exactly @p lines enabled. @param lines  One bit per line */
void tiku_esp32c61_irq_set_enabled(uint32_t lines);

/** @brief Interrupts that arrived on a line no handler claimed. */
uint32_t tiku_esp32c61_irq_spurious(void);

#endif /* TIKU_ESP32C61_IRQ_ARCH_H_ */
