/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - RA8P1 clock state.
 *
 * R2 does not touch the clock tree.  The part comes out of reset on MOCO at
 * 8 MHz with every SCKDIVCR field zero, and every derived constant in the port
 * -- console divisor, SysTick reload, delay loop -- is computed from that one
 * number.  What this module does provide is a way to READ the tree back, so
 * R4 can prove the PLL landed where it was asked to rather than assuming it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_RA8P1_CPU_FREQ_BOOT_ARCH_H_

#include <stdint.h>

#include <arch/ra8p1/tiku_device_select.h>

/** @brief Spin-loop iterations per millisecond, before any measurement. */
#ifndef TIKU_RA8P1_SPIN_ITERS_PER_MS
#define TIKU_RA8P1_SPIN_ITERS_PER_MS    (TIKU_RA8P1_ICLK_BOOT_HZ / 1000UL)
#endif

/** @brief Clock sources SCKSCR.CKSEL can select (UM 9.2.5). */
#define TIKU_RA8P1_CKSEL_HOCO       0U
#define TIKU_RA8P1_CKSEL_MOCO       1U
#define TIKU_RA8P1_CKSEL_LOCO       2U
#define TIKU_RA8P1_CKSEL_MAIN       3U
#define TIKU_RA8P1_CKSEL_SUBCLK     4U
#define TIKU_RA8P1_CKSEL_PLL1P      5U
#define TIKU_RA8P1_CKSEL_PLL2P      6U

/** @brief Live clock-tree state, as read back from SYSC. */
typedef struct {
    uint8_t       cksel;        /**< SCKSCR.CKSEL, the system clock source */
    uint8_t       iclk_div;     /**< ICLK divider, as a divisor not a code */
    uint8_t       pclka_div;    /**< PCLKA divider, as a divisor           */
    uint8_t       pclkb_div;    /**< PCLKB divider, as a divisor           */
    unsigned long src_hz;       /**< source rate, 0 when not yet derivable */
    unsigned long iclk_hz;      /**< implied core rate                     */
    unsigned long pclka_hz;     /**< implied PCLKA rate                    */
} tiku_ra8p1_clock_t;

/**
 * @brief Prepare whatever clock state the rest of the port depends on.
 *
 * Nothing, deliberately: R2 runs on the reset tree.  The call exists so the
 * boot sequence matches the other ports and R4 has one place to grow into.
 */
void tiku_cpu_boot_ra8p1_init(void);

/**
 * @brief Read the live clock tree.
 *
 * @param out  Receives the current configuration; NULL is ignored
 */
void tiku_cpu_ra8p1_clock_probe(tiku_ra8p1_clock_t *out);

/**
 * @brief Core clock rate in Hz.
 *
 * @return The rate implied by SCKSCR and SCKDIVCR, MOCO-derived until R4
 */
unsigned long tiku_cpu_ra8p1_clock_get_hz(void);

/**
 * @brief Peripheral clock A rate in Hz -- what the SCI baud divisor uses.
 *
 * @return The rate implied by SCKSCR and SCKDIVCR
 */
unsigned long tiku_cpu_ra8p1_pclka_get_hz(void);

/**
 * @brief Delay-loop iterations per millisecond.
 *
 * Measured against the kernel tick on first call once the tick is running;
 * before that the compile-time estimate is reported.
 *
 * @return Loop iterations that occupy one millisecond
 */
unsigned long tiku_cpu_ra8p1_spin_per_ms(void);

#endif /* TIKU_RA8P1_CPU_FREQ_BOOT_ARCH_H_ */
