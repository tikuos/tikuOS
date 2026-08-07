/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - RA8P1 clock tree and operating points.
 *
 * MOSC and PLL bring-up, rung selection, the CAC cross-check, and the live
 * ICLK/PCLKA/PCLKB/SCICLK/PCLKD/BCLK rates.  The part comes out of reset on
 * MOCO at 8 MHz with every SCKDIVCR field zero.
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
 * @brief Start the board's main crystal oscillator.
 *
 * Needed both as the PLL's reference and as the CAC's, so it is brought up
 * before either.
 *
 * @return 0 when the oscillator reports stable, -1 when it never did
 */
int tiku_cpu_ra8p1_mosc_start(void);

/**
 * @brief Count one clock against another, entirely on-chip.
 *
 * The reference is divided by the RCDS code below, and the return is how many
 * target-clock edges fell inside one such period -- so the target rate is
 * count * ref_hz / divider.
 *
 * @param target     Clock to measure, an RA8P1_CAC_CLK_* value
 * @param reference  Clock to measure against, an RA8P1_CAC_CLK_* value
 * @param ref_div    RCDS code: 0 = /32, 1 = /128, 2 = /1024, 3 = /8192
 * @return Target-clock count, or 0 if the measurement never completed
 */
uint16_t tiku_cpu_ra8p1_cac_measure(uint8_t target, uint8_t reference,
                                    uint8_t ref_div);

/**
 * @brief Move the clock tree to @p mhz.
 *
 * Refuses a rate it cannot produce rather than approximating: `freq` naming a
 * rate the part is not running at is worse than a refusal.
 *
 * @param mhz  Requested core frequency in MHz
 */
void tiku_cpu_freq_ra8p1_init(unsigned int mhz);

/**
 * @brief Report whether a frequency is one this port can select.
 *
 * @param mhz  Frequency in MHz
 * @return 1 when supported, 0 otherwise
 */
int tiku_cpu_freq_ra8p1_supported(unsigned int mhz);

/**
 * @brief Prepare whatever clock state the rest of the port depends on.
 *
 * Nothing, deliberately: the boot path runs on the reset tree and the rung is
 * chosen explicitly by tiku_cpu_freq_ra8p1_init().  The call exists so the
 * boot sequence matches the other ports.
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
 * @return The rate the clock tree is configured for, in Hz
 */
unsigned long tiku_cpu_ra8p1_clock_get_hz(void);

/**
 * @brief Peripheral clock A rate in Hz -- what the SCI baud divisor uses.
 *
 * @return The rate implied by SCKSCR and SCKDIVCR
 */
unsigned long tiku_cpu_ra8p1_pclka_get_hz(void);

/**
 * @brief Peripheral clock B rate in Hz -- the CAC's measurable proxy.
 *
 * @return The rate PCLKB is running at
 */
unsigned long tiku_cpu_ra8p1_pclkb_get_hz(void);

/**
 * @brief SCICLK rate in Hz -- what the console's baud divisor divides.
 *
 * A separate clock from PCLKA, with its own source select; they only coincide
 * at boot, when both are MOCO at /1.
 *
 * @return The rate SCICLK is running at
 */
unsigned long tiku_cpu_ra8p1_sciclk_get_hz(void);

/**
 * @brief PCLKD rate in Hz -- what the GPT counts, and so the htimer's tick.
 *
 * @return The rate PCLKD is running at
 */
unsigned long tiku_cpu_ra8p1_pclkd_get_hz(void);

/**
 * @brief ICLK rate in Hz, which is what SysTick counts.
 *
 * @note NOT the core rate.  SysTick's CLKSOURCE selects the processor clock,
 *       and on this part that is ICLK, which the divider table pins near 240
 *       at every rung while CPUCLK0 rides PLL1P.  A tick reload built from
 *       the core rate runs slow by exactly the ratio between them.
 * @return The rate ICLK is running at
 */
unsigned long tiku_cpu_ra8p1_iclk_get_hz(void);

/**
 * @brief External bus clock (BCLK), which is also the SDRAM clock source.
 *
 * Read live rather than assumed: the SDRAM timings are derived from it, and a
 * clock change that outran the part would otherwise corrupt data silently.
 *
 * @return BCLK in Hz
 */
unsigned long tiku_cpu_ra8p1_bclk_get_hz(void);

/**
 * @brief Delay-loop iterations per millisecond.
 *
 * Measured against the kernel tick on first call once the tick is running;
 * before that the compile-time estimate is reported.
 *
 * @return Loop iterations that occupy one millisecond
 */
unsigned long tiku_cpu_ra8p1_spin_per_ms(void);

/**
 * @brief Discard the delay-loop calibration so the next caller re-measures.
 *
 * Called when the clock tree moves.  The figure is measured against the tick
 * and cached forever otherwise, so without this every delay after a second
 * frequency change is wrong by the ratio between the two rates.
 */
void tiku_cpu_ra8p1_spin_invalidate(void);

/**
 * @brief Enter Sleep mode (WFI) until any unmasked interrupt.
 *
 * Clocks keep running, so the tick, console RX and an armed htimer all wake
 * the core.  Software Standby is deeper but is not entered by this port.
 */
void tiku_cpu_boot_ra8p1_power_wfi_enter(void);

#endif /* TIKU_RA8P1_CPU_FREQ_BOOT_ARCH_H_ */
