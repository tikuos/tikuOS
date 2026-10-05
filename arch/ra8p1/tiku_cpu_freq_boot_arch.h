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
    unsigned long src_hz;       /**< source rate; 0 for HOCO and PLL2P     */
    unsigned long iclk_hz;      /**< ICLK: src_hz / iclk_div               */
    unsigned long pclka_hz;     /**< PCLKA: src_hz / pclka_div             */
} tiku_ra8p1_clock_t;

/**
 * @brief Start the board's main crystal oscillator.
 *
 * The PLL and the CAC both use it as their reference.
 *
 * @return 0 when the oscillator reports stable, -1 after three failed starts
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
 * @return Target-clock count, or 0 if the measurement timed out or overflowed
 */
uint16_t tiku_cpu_ra8p1_cac_measure(uint8_t target, uint8_t reference,
                                    uint8_t ref_div);

/**
 * @brief Move the clock tree to the rung for @p mhz.
 *
 * A rate with no rung is ignored, as is a change of rung while the octal
 * flash is in OPI mode.  A failed change falls back to the previous rung and
 * sets the flag tiku_cpu_ra8p1_clock_has_fault() reports.
 *
 * @param mhz  Requested core frequency in MHz: 240, 480 or 1000
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
 * @brief Report whether the last rung change failed.
 *
 * Set when the oscillator, the PLL or a transition the change waits on never
 * settled, even if the tree got back to the previous rung; cleared by the next
 * change that succeeds.  A refused rate leaves it as it was.
 *
 * @return 1 after a failed rung change, 0 otherwise
 */
int tiku_cpu_ra8p1_clock_has_fault(void);

/**
 * @brief Boot-time clock hook, empty on this port.
 *
 * The boot path runs on the reset tree (MOCO, 8 MHz) until
 * tiku_cpu_freq_ra8p1_init() raises it; hal/tiku_cpu.c calls this hook on
 * every port.
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
 * @return The core rate of the established rung, or the MOCO rate while the
 *         tree runs on MOCO, in Hz
 */
unsigned long tiku_cpu_ra8p1_clock_get_hz(void);

/**
 * @brief Auxiliary low-speed clock rate in Hz -- the LOCO.
 *
 * @return 32768, which no rung change alters
 */
unsigned long tiku_cpu_ra8p1_aclk_get_hz(void);

/**
 * @brief Peripheral clock A rate in Hz.
 *
 * @return The rate implied by the core rate and SCKDIVCR/SCKDIVCR2
 */
unsigned long tiku_cpu_ra8p1_pclka_get_hz(void);

/**
 * @brief Peripheral clock B rate in Hz; the CAC can measure PCLKB.
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
 * Not the core rate: ICLK is 240 MHz at the 240 and 480 rungs and 250 MHz at
 * 1000, while CPUCLK0 runs at the rung rate.  A tick reload computed from the
 * core rate runs slow by their ratio.
 *
 * @return The rate ICLK is running at
 */
unsigned long tiku_cpu_ra8p1_iclk_get_hz(void);

/**
 * @brief External bus clock (BCLK) rate in Hz; BCLK also clocks the SDRAM.
 *
 * Computed from the established rung and the live SCKDIVCR; the SDRAM
 * timings are derived from it.
 *
 * @return BCLK in Hz
 */
unsigned long tiku_cpu_ra8p1_bclk_get_hz(void);

/**
 * @brief Delay-loop iterations per millisecond.
 *
 * Measured against the kernel tick by the first call made while the tick
 * can advance; until then the compile-time estimate is returned.
 *
 * @return Loop iterations that occupy one millisecond
 */
unsigned long tiku_cpu_ra8p1_spin_per_ms(void);

/**
 * @brief Discard the delay-loop calibration; the next
 *        tiku_cpu_ra8p1_spin_per_ms() call measures again.
 *
 * @note Call after every clock-tree change: a cached figure is wrong by the
 *       ratio of the old and new rates.
 */
void tiku_cpu_ra8p1_spin_invalidate(void);

/**
 * @brief Enter Sleep mode (WFI) until any unmasked interrupt.
 *
 * Clocks keep running, so the tick, console RX and an armed htimer all wake
 * the core.  Above 240 MHz the SCKDIVCR2 clocks (both CPUs, MRAM, NPU) run
 * at ICLK's rate for the sleep.  This port does not enter Software Standby.
 */
void tiku_cpu_boot_ra8p1_power_wfi_enter(void);

#endif /* TIKU_RA8P1_CPU_FREQ_BOOT_ARCH_H_ */
