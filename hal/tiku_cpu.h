/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu.h - platform-agnostic CPU abstraction interface.
 *
 * Atomic sections, IRQ control, clock rates, cache maintenance and idle modes.
 * Most calls go to the active port's arch layer; the atomics and rate tables
 * are in tiku_cpu.c, the saved rate in tiku_cpu_settings.c except on nRF54L.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CPU_H_
#define TIKU_CPU_H_

/*---------------------------------------------------------------------------*/
/* ATOMIC / IRQ CONTROL                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Enter a nested atomic section: disable interrupts, saving the
 *        enable state on the outermost entry.
 */
void tiku_atomic_enter(void);

/**
 * @brief Leave an atomic section; the outermost exit re-enables interrupts
 *        only if they were enabled at the matching entry.
 */
void tiku_atomic_exit(void);

/**
 * @brief Unconditionally enable global interrupts.
 *
 * Boot and the scheduler loop call it before work starts.  It does not save
 * or restore the caller's interrupt state; tiku_atomic_enter() and
 * tiku_atomic_exit() do.
 */
void tiku_cpu_irq_enable(void);

/**
 * @brief Unconditionally disable global interrupts.
 */
void tiku_cpu_irq_disable(void);

/*---------------------------------------------------------------------------*/
/* BOOT / FREQUENCY                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Run the port's one-time CPU bring-up.
 *
 * What it covers is per port: GPIO and LOCKLPM5 on MSP430, the clock tree on
 * RP2350, power domains and caches on Ambiq, the PLL and HFXO on nRF54L, the
 * watchdogs on ESP32-C61.
 *
 * @note Call once at boot, before tiku_cpu_freq_init().
 */
void tiku_cpu_boot_init(void);

/**
 * @brief Apply a core-clock frequency to the platform clock tree.
 *
 * A rate the port cannot produce is clamped or ignored and no error is
 * returned; tiku_cpu_mclk_hz() returns the rate in effect.
 *
 * @param cpu_freq  Requested core frequency in MHz; on MSP430 a DCO preset
 *                  index (CPU_FREQ_* in tiku_cpu_freq_boot_arch.h)
 * @note Call after tiku_cpu_boot_init() and before anything timed off the
 *       core or peripheral clock.
 */
void tiku_cpu_freq_init(unsigned int cpu_freq);

/**
 * @brief "reboot" when more than one rate is selectable, else "fixed".
 *
 * A rate saved with tiku_cpu_freq_target_set() takes effect at the next boot.
 */
const char *tiku_cpu_freq_change_mode(void);
/** @brief Enumerate selectable rates in Hz; zero terminates the list. */
unsigned long tiku_cpu_freq_available(unsigned int index);
/** @brief Saved next-boot rate, or the boot rate when none is saved. */
unsigned long tiku_cpu_freq_target_hz(void);
/**
 * @brief Save @p hz as the rate for the next boot.
 * @return 0, or -1 when @p hz is not selectable or the save fails
 */
int tiku_cpu_freq_target_set(unsigned long hz);
/**
 * @brief Switch to the saved rate when it differs from the boot rate.
 * @note Call once during boot, before peripherals start. No-op on nRF54L.
 */
void tiku_cpu_freq_boot_apply(void);
/**
 * @brief Switch the core clock to @p hz.
 * @note Boot only: tiku_cpu_settings_boot() calls it. MSP430 and STM32N6
 *       divide the running clock; nRF54L ignores the call.
 */
void tiku_cpu_freq_boot_set(unsigned long hz);

/*---------------------------------------------------------------------------*/
/* CLOCK RATE QUERIES                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Current main CPU clock frequency in Hz.
 *
 * MSP430 maps this to MCLK; other platforms map to their primary
 * core clock.
 */
unsigned long tiku_cpu_mclk_hz(void);

/**
 * @brief Current peripheral / sub-system clock frequency in Hz.
 *
 * SMCLK on MSP430 and PCLKA on RA8P1. A port without a separate peripheral
 * clock returns the tiku_cpu_mclk_hz() rate.
 */
unsigned long tiku_cpu_smclk_hz(void);

/**
 * @brief Current low-power / always-on clock frequency in Hz.
 *
 * ACLK on MSP430 (typ. 32.768 kHz crystal or REFOCLK). A port without an
 * always-on low-frequency clock returns 0: RP2350, STM32N6 and ESP32-C61.
 */
unsigned long tiku_cpu_aclk_hz(void);

/**
 * @brief Non-zero if the platform reports a clock-source fault.
 *
 * Read by /sys/boot/clock/fault.  On MSP430 it reports OFIFG, which collects
 * the oscillator-fault flags.  Platforms with no equivalent return 0.
 */
int tiku_cpu_clock_has_fault(void);

/*---------------------------------------------------------------------------*/
/* CPU DATA-CACHE MAINTENANCE                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clean (write back) the data cache over an address range.
 *
 * Pushes dirty lines to memory so an out-of-band reader such as a DMA or ROM
 * agent sees them, e.g. before a staging buffer goes to the NVM programmer.
 * A no-op where the range is not cached.
 */
void tiku_cpu_dcache_clean(const void *addr, unsigned long len);

/**
 * @brief Invalidate the data cache over an address range.
 *
 * Drops cached copies so the next read sees what an out-of-band writer left.
 * Where the controller has no by-range op the whole cache is invalidated.
 */
void tiku_cpu_dcache_invalidate(const void *addr, unsigned long len);

/**
 * @brief Invalidate the entire instruction cache.
 *
 * Invalidates every line. It does nothing on MSP430, which has no cache, or
 * on RP2350 and nRF54L, whose NVM write paths drop the lines they touch.
 *
 * @note Call after an out-of-band write to executable memory and before the
 *       first fetch from the modified range.
 */
void tiku_cpu_icache_invalidate(void);

/*---------------------------------------------------------------------------*/
/* IDLE / LOW-POWER MODES                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Generic idle modes, each mapped by the port to a native state.
 *
 * MSP430: OFF busy-waits, LIGHT is LPM0, DEEP LPM3, DEEPEST LPM4 (GPIO wake).
 * RP2350, Ambiq, nRF54L and RA8P1 use WFI for every mode, ESP32-C61 WFI for
 * LIGHT and PMU light sleep below it; STM32N6 busy-waits in every mode.
 */
typedef enum {
    TIKU_CPU_IDLE_OFF      = 0,
    TIKU_CPU_IDLE_LIGHT    = 1,
    TIKU_CPU_IDLE_DEEP     = 2,
    TIKU_CPU_IDLE_DEEPEST  = 3,
} tiku_cpu_idle_mode_t;

/** Function pointer signature for idle-entry hooks. */
typedef void (*tiku_cpu_idle_enter_t)(void);

/**
 * @brief Return the platform's entry function for the given mode.
 * @param mode  Idle mode
 * @return Hook callable as the scheduler's idle hook; NULL for OFF and for
 *         every mode on a port with no idle entry (STM32N6)
 */
tiku_cpu_idle_enter_t tiku_cpu_idle_hook(tiku_cpu_idle_mode_t mode);

/**
 * @brief Report whether the tick interrupt wakes the core from @p mode.
 *
 * The scheduler sleeps with timers armed only in a mode where this is
 * non-zero. It is zero for MSP430 LPM4 and non-zero for every other mode.
 *
 * @return Non-zero if the tick wakes the CPU out of @p mode
 */
int tiku_cpu_idle_mode_wakes_on_tick(tiku_cpu_idle_mode_t mode);

/**
 * @brief Wake sources that end @p mode.
 *
 * An armed source outside the mask does not wake the core from @p mode.
 * Without TIKU_WAKE_UART_RX, console input sent during the sleep is lost.
 *
 * @return Mask of TIKU_WAKE_* bits from hal/tiku_wake_hal.h
 */
unsigned int tiku_cpu_idle_mode_wakes(tiku_cpu_idle_mode_t mode);

/**
 * @brief Short, platform-specific name for the mode.
 *        e.g. on MSP430: "off", "LPM0", "LPM3", "LPM4".
 */
const char *tiku_cpu_idle_mode_name(tiku_cpu_idle_mode_t mode);

/**
 * @brief Long, descriptive name for the mode.
 *        e.g. on MSP430: "LPM3 (CPU+SMCLK off, ACLK on)".
 */
const char *tiku_cpu_idle_mode_desc(tiku_cpu_idle_mode_t mode);

#endif /* TIKU_CPU_H_ */
