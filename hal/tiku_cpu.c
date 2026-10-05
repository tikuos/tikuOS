/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu.c - platform-agnostic CPU abstraction implementation.
 *
 * Atomic sections, IRQ control, clock-rate queries, cache maintenance and
 * idle hooks.  Most calls forward to the active port's arch backend; the
 * atomics and the selectable-rate tables are implemented here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include "tiku_cpu.h"
#include "tiku_wake_hal.h"
#include <kernel/cpu/tiku_cpu_settings.h>

#if defined(PLATFORM_MSP430)
#include <msp430.h>    /* MSP430 intrinsics for interrupt state management */
#include "arch/msp430/tiku_cpu_freq_boot_arch.h"
#include "tiku_htimer_hal.h"   /* TIKU_HTIMER_CLOCK_SOURCE */
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
      defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6) || \
      defined(PLATFORM_RA8P1)
#if defined(PLATFORM_RP2350)
#include "arch/arm-rp2350/tiku_cpu_freq_boot_arch.h"
#elif defined(PLATFORM_AMBIQ)
#include "arch/ambiq/tiku_cpu_freq_boot_arch.h"
#elif defined(PLATFORM_STM32N6)
#include "arch/stm32n6/tiku_cpu_freq_boot_arch.h"
#include "arch/stm32n6/tiku_cache_arch.h"
#elif defined(PLATFORM_RA8P1)
#include "arch/ra8p1/tiku_cpu_freq_boot_arch.h"
#include "arch/ra8p1/tiku_cache_arch.h"
#else
#include "arch/nordic/tiku_cpu_freq_boot_arch.h"
#endif
#include <stdint.h>

/* Cortex-M PRIMASK helpers, written out rather than taken from CMSIS so this
 * file needs no vendor header.  The instructions are the same on every
 * Cortex-M port: M4F (Apollo4), M33, M55 and M85. */

/** @brief Read PRIMASK: 0 when interrupts are enabled, 1 when masked. */
static inline uint32_t tiku_arm_get_primask(void) {
    uint32_t v;
    __asm__ volatile ("mrs %0, primask" : "=r"(v));
    return v;
}
/** @brief Write PRIMASK. */
static inline void tiku_arm_set_primask(uint32_t v) {
    __asm__ volatile ("msr primask, %0" : : "r"(v) : "memory");
}
/** @brief Mask interrupts (cpsid i). */
static inline void tiku_arm_disable_irq(void) {
    __asm__ volatile ("cpsid i" ::: "memory");
}
/** @brief Unmask interrupts (cpsie i). */
static inline void tiku_arm_enable_irq(void) {
    __asm__ volatile ("cpsie i" ::: "memory");
}
#elif defined(PLATFORM_ESP32C61)
#include "arch/esp32c61/tiku_cpu_freq_boot_arch.h"
#include "arch/esp32c61/tiku_cpu_common.h"
#include "arch/esp32c61/tiku_irq_arch.h"
#include "arch/esp32c61/tiku_sleep_arch.h"
#include "arch/esp32c61/tiku_psram_arch.h"
#include <stdint.h>
#endif

/*---------------------------------------------------------------------------*/
/* ATOMIC NESTING                                                            */
/*---------------------------------------------------------------------------*/

/*
 * Atomic section nesting depth and saved interrupt-enable state.
 *
 * The outermost tiku_atomic_enter() (nesting == 0) reads the enable bit (GIE,
 * PRIMASK or mstatus.MIE) before disabling interrupts; the matching outermost
 * exit re-enables only if it was set, so an atomic section never enables
 * interrupts as a side effect.
 *
 * An ISR that fires between the read and the disable runs its own balanced
 * enter/exit pair, which restores the enable state the ISR started with and
 * leaves the nesting count at 0; the interrupted code then saves from its own
 * local copy of the bit.
 */
static volatile unsigned int tiku_atomic_nesting = 0;
static volatile unsigned int tiku_atomic_gie_saved = 0;

void tiku_atomic_enter(void) {
#if defined(PLATFORM_MSP430)
  unsigned int sr = __get_interrupt_state();
  __disable_interrupt();
  if (tiku_atomic_nesting == 0) {
    tiku_atomic_gie_saved = (sr & GIE) != 0;
  }
  tiku_atomic_nesting++;
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
      defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6) || \
      defined(PLATFORM_RA8P1)
  /* PRIMASK = 0 means IRQs enabled; PRIMASK = 1 means masked. The bit is
   * snapshotted on the outermost entry and restored on the outermost
   * exit, mirroring the MSP430 GIE handling above. */
  uint32_t pm = tiku_arm_get_primask();
  tiku_arm_disable_irq();
  if (tiku_atomic_nesting == 0) {
    tiku_atomic_gie_saved = (pm == 0);  /* 1 if IRQs were enabled */
  }
  tiku_atomic_nesting++;
#elif defined(PLATFORM_ESP32C61)
  /* mstatus.MIE plays GIE's part: cleared in one instruction that also
   * returns what it was. */
  uint32_t mie = tiku_esp32c61_mie_off();
  if (tiku_atomic_nesting == 0) {
    tiku_atomic_gie_saved = (mie != 0);
  }
  tiku_atomic_nesting++;
#endif
}

void tiku_atomic_exit(void) {
  if (--tiku_atomic_nesting == 0 && tiku_atomic_gie_saved) {
#if defined(PLATFORM_MSP430)
    __enable_interrupt();
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
      defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6) || \
      defined(PLATFORM_RA8P1)
    tiku_arm_enable_irq();
#elif defined(PLATFORM_ESP32C61)
    tiku_esp32c61_mie_restore(ESP32C61_MSTATUS_MIE);
#endif
  }
}

/*---------------------------------------------------------------------------*/
/* IRQ CONTROL                                                               */
/*---------------------------------------------------------------------------*/

void tiku_cpu_irq_enable(void) {
#if defined(PLATFORM_MSP430)
    __enable_interrupt();
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
      defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6) || \
      defined(PLATFORM_RA8P1)
    tiku_arm_enable_irq();
#elif defined(PLATFORM_ESP32C61)
    tiku_esp32c61_mie_restore(ESP32C61_MSTATUS_MIE);
#endif
}

void tiku_cpu_irq_disable(void) {
#if defined(PLATFORM_MSP430)
    __disable_interrupt();
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
      defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6) || \
      defined(PLATFORM_RA8P1)
    tiku_arm_disable_irq();
#elif defined(PLATFORM_ESP32C61)
    (void)tiku_esp32c61_mie_off();
#endif
}

/*---------------------------------------------------------------------------*/
/* BOOT / FREQUENCY                                                          */
/*---------------------------------------------------------------------------*/

void tiku_cpu_boot_init(void) {
#if defined(PLATFORM_MSP430)
    tiku_cpu_boot_msp430_init();
#elif defined(PLATFORM_RP2350)
    tiku_cpu_boot_rp2350_init();
#elif defined(PLATFORM_AMBIQ)
    tiku_cpu_boot_ambiq_init();
#elif defined(PLATFORM_NORDIC)
    tiku_cpu_boot_nordic_init();
#elif defined(PLATFORM_STM32N6)
    tiku_cpu_boot_stm32n6_init();
#elif defined(PLATFORM_RA8P1)
    tiku_cpu_boot_ra8p1_init();
#elif defined(PLATFORM_ESP32C61)
    tiku_cpu_boot_esp32c61_init();
#endif
}

void tiku_cpu_freq_init(unsigned int cpu_freq) {
#if defined(PLATFORM_MSP430)
    tiku_cpu_freq_msp430_init(cpu_freq);
#elif defined(PLATFORM_RP2350)
    tiku_cpu_freq_rp2350_init(cpu_freq);
#elif defined(PLATFORM_AMBIQ)
    tiku_cpu_freq_ambiq_init(cpu_freq);
#elif defined(PLATFORM_NORDIC)
    tiku_cpu_freq_nordic_init(cpu_freq);
#elif defined(PLATFORM_STM32N6)
    tiku_cpu_freq_stm32n6_init(cpu_freq);
#elif defined(PLATFORM_RA8P1)
    tiku_cpu_freq_ra8p1_init(cpu_freq);
#elif defined(PLATFORM_ESP32C61)
    /* A rate not offered is ignored: the tree keeps what it runs at. */
    if (cpu_freq >= TIKU_ESP32C61_OFFER_MIN_MHZ) {
        (void)tiku_cpu_freq_esp32c61_set(cpu_freq);
    }
#endif
}

const char *tiku_cpu_freq_change_mode(void) {
    return tiku_cpu_freq_available(1) != 0UL ? "reboot" : "fixed";
}

unsigned long tiku_cpu_freq_available(unsigned int index) {
#if defined(PLATFORM_NORDIC)
    return index == 0 ? 64000000UL : index == 1 ? 128000000UL : 0UL;
#elif defined(PLATFORM_RP2350)
    static const unsigned long rates[] = {
        12000000UL, 48000000UL, 100000000UL, 125000000UL,
        133000000UL, 150000000UL
    };
    return index < sizeof rates / sizeof rates[0] ? rates[index] : 0UL;
#elif defined(PLATFORM_AMBIQ)
#if defined(AM_PART_APOLLO4L)
    return index == 0 ? 96000000UL : index == 1 ? 192000000UL : 0UL;
#else
    return index == 0 ? 96000000UL : index == 1 ? 250000000UL : 0UL;
#endif
#elif defined(PLATFORM_RA8P1)
    static const unsigned long rates[] = {
        240000000UL, 480000000UL, 1000000000UL
    };
    return index < sizeof rates / sizeof rates[0] ? rates[index] : 0UL;
#elif defined(PLATFORM_ESP32C61)
    /* The crystal divided, then the PLL divided: every rate the tree makes,
     * from the lowest the build offers. */
    static const unsigned long rates[] = {
        10000000UL, 20000000UL, 40000000UL, 80000000UL, 160000000UL
    };
    unsigned int first = 0U;
    while (rates[first] < TIKU_ESP32C61_OFFER_MIN_MHZ * 1000000UL) {
        first++;
    }
    index += first;
    return index < sizeof rates / sizeof rates[0] ? rates[index] : 0UL;
#elif defined(PLATFORM_MSP430)
    /* Keep the board's 8 MHz UART/peripheral clock untouched. */
    if (tiku_cpu_smclk_hz() != 8000000UL)
        return index == 0 ? tiku_cpu_mclk_hz() : 0UL;
    return index < 4 ? (1000000UL << index) : 0UL;
#elif defined(PLATFORM_STM32N6)
    static const unsigned long rates[] = {
        100000000UL, 150000000UL, 200000000UL, 300000000UL,
        400000000UL, 600000000UL, 800000000UL
    };
    unsigned int i;
    for (i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        if (tiku_cpu_stm32n6_boot_rate_supported(rates[i])) {
            if (index == 0) return rates[i];
            index--;
        }
    }
    return 0UL;
#else
    return index == 0 ? tiku_cpu_mclk_hz() : 0UL;
#endif
}

unsigned long tiku_cpu_freq_target_hz(void) {
#if defined(PLATFORM_NORDIC)
    return tiku_cpu_nordic_target_hz();
#else
    return tiku_cpu_settings_target();
#endif
}

int tiku_cpu_freq_target_set(unsigned long hz) {
#if defined(PLATFORM_NORDIC)
    return tiku_cpu_nordic_target_set(hz);
#else
    return tiku_cpu_settings_save(hz);
#endif
}

void tiku_cpu_freq_boot_apply(void) {
#if !defined(PLATFORM_NORDIC)
    tiku_cpu_settings_boot();
#endif
}

void tiku_cpu_freq_boot_set(unsigned long hz) {
#if defined(PLATFORM_MSP430)
    tiku_cpu_msp430_boot_divide(hz);
#elif defined(PLATFORM_STM32N6)
    tiku_cpu_stm32n6_boot_divide(hz);
#elif !defined(PLATFORM_NORDIC)
    tiku_cpu_freq_init((unsigned int)(hz / 1000000UL));
#else
    (void)hz;
#endif
}

void tiku_cpu_dcache_clean(const void *addr, unsigned long len) {
#if defined(PLATFORM_MSP430)
    (void)addr; (void)len;            /* no data cache */
#elif defined(PLATFORM_RP2350)
    /* SRAM is uncached; the flash write path flushes the XIP cache. */
    (void)addr; (void)len;
#elif defined(PLATFORM_AMBIQ)
    tiku_cpu_ambiq_dcache_clean(addr, len);
#elif defined(PLATFORM_NORDIC)
    /* SRAM is uncached; the NVM cache is write-around and holds no dirty
     * line. */
    (void)addr; (void)len;
#elif defined(PLATFORM_STM32N6)
    tiku_stm32n6_dcache_clean(addr, len);
#elif defined(PLATFORM_RA8P1)
    tiku_ra8p1_dcache_clean(addr, len);
#elif defined(PLATFORM_ESP32C61)
    /* SRAM is uncached and the flash is not written through the cache, so
     * only a PSRAM range has lines to write back. */
    tiku_esp32c61_psram_clean(addr, len);
#endif
}

void tiku_cpu_dcache_invalidate(const void *addr, unsigned long len) {
#if defined(PLATFORM_MSP430)
    (void)addr; (void)len;
#elif defined(PLATFORM_RP2350)
    (void)addr; (void)len;
#elif defined(PLATFORM_AMBIQ)
    tiku_cpu_ambiq_dcache_invalidate(addr, len);
#elif defined(PLATFORM_NORDIC)
    /* An NVM write drops the cache lines it touches. */
    (void)addr; (void)len;
#elif defined(PLATFORM_STM32N6)
    tiku_stm32n6_dcache_invalidate(addr, len);
#elif defined(PLATFORM_RA8P1)
    tiku_ra8p1_dcache_invalidate(addr, len);
#elif defined(PLATFORM_ESP32C61)
    /* The flash driver invalidates what it writes; a PSRAM range is dropped
     * here. */
    tiku_esp32c61_psram_invalidate(addr, len);
#endif
}

void tiku_cpu_icache_invalidate(void) {
#if defined(PLATFORM_AMBIQ)
    tiku_cpu_ambiq_icache_invalidate();
#elif defined(PLATFORM_STM32N6)
    tiku_stm32n6_icache_invalidate();
#elif defined(PLATFORM_RA8P1)
    tiku_ra8p1_icache_invalidate();
#elif defined(PLATFORM_ESP32C61)
    tiku_cpu_esp32c61_icache_invalidate();
#endif
    /* MSP430 has no instruction cache, an nRF54L NVM write drops the lines
     * it touches, and the RP2350 flash write path flushes the XIP cache. */
}

/*---------------------------------------------------------------------------*/
/* CLOCK RATE QUERIES                                                        */
/*---------------------------------------------------------------------------*/

unsigned long tiku_cpu_mclk_hz(void) {
#if defined(PLATFORM_MSP430)
    return tiku_cpu_msp430_clock_get_hz();
#elif defined(PLATFORM_RP2350)
    return tiku_cpu_rp2350_clock_get_hz();
#elif defined(PLATFORM_AMBIQ)
    return tiku_cpu_ambiq_clock_get_hz();
#elif defined(PLATFORM_NORDIC)
    return tiku_cpu_nordic_clock_get_hz();
#elif defined(PLATFORM_STM32N6)
    /* Measured against LPTIM1; decoded from the clock tree before the
     * timer runs. */
    return tiku_cpu_stm32n6_clock_get_hz();
#elif defined(PLATFORM_RA8P1)
    return tiku_cpu_ra8p1_clock_get_hz();
#elif defined(PLATFORM_ESP32C61)
    return tiku_cpu_esp32c61_clock_get_hz();   /* measured against SYSTIMER */
#else
    return 0;
#endif
}

unsigned long tiku_cpu_smclk_hz(void) {
#if defined(PLATFORM_MSP430)
    return tiku_cpu_msp430_smclk_get_hz();
#elif defined(PLATFORM_RP2350)
    return tiku_cpu_rp2350_smclk_get_hz();
#elif defined(PLATFORM_AMBIQ)
    return tiku_cpu_ambiq_smclk_get_hz();
#elif defined(PLATFORM_NORDIC)
    return tiku_cpu_nordic_smclk_get_hz();
#elif defined(PLATFORM_STM32N6)
    return tiku_cpu_stm32n6_smclk_get_hz();
#elif defined(PLATFORM_RA8P1)
    return tiku_cpu_ra8p1_pclka_get_hz();
#elif defined(PLATFORM_ESP32C61)
    return tiku_cpu_esp32c61_smclk_get_hz();
#else
    return 0;
#endif
}

unsigned long tiku_cpu_aclk_hz(void) {
#if defined(PLATFORM_MSP430)
    return tiku_cpu_msp430_aclk_get_hz();
#elif defined(PLATFORM_RP2350)
    return tiku_cpu_rp2350_aclk_get_hz();
#elif defined(PLATFORM_AMBIQ)
    return tiku_cpu_ambiq_aclk_get_hz();
#elif defined(PLATFORM_NORDIC)
    return tiku_cpu_nordic_aclk_get_hz();
#elif defined(PLATFORM_RA8P1)
    return tiku_cpu_ra8p1_aclk_get_hz();
#else
    return 0;
#endif
}

int tiku_cpu_clock_has_fault(void) {
#if defined(PLATFORM_MSP430)
    return tiku_cpu_msp430_clock_has_fault() ? 1 : 0;
#elif defined(PLATFORM_RP2350)
    return tiku_cpu_rp2350_clock_has_fault() ? 1 : 0;
#elif defined(PLATFORM_AMBIQ)
    return tiku_cpu_ambiq_clock_has_fault() ? 1 : 0;
#elif defined(PLATFORM_NORDIC)
    return tiku_cpu_nordic_clock_has_fault() ? 1 : 0;
#elif defined(PLATFORM_STM32N6)
    return tiku_cpu_stm32n6_clock_has_fault() ? 1 : 0;
#elif defined(PLATFORM_RA8P1)
    return tiku_cpu_ra8p1_clock_has_fault() ? 1 : 0;
#elif defined(PLATFORM_ESP32C61)
    return tiku_cpu_esp32c61_clock_has_fault() ? 1 : 0;
#else
    return 0;
#endif
}

/*---------------------------------------------------------------------------*/
/* IDLE / LOW-POWER MODES                                                    */
/*---------------------------------------------------------------------------*/

tiku_cpu_idle_enter_t tiku_cpu_idle_hook(tiku_cpu_idle_mode_t mode) {
#if defined(PLATFORM_MSP430)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
            return tiku_cpu_boot_msp430_power_lpm0_enter;
        case TIKU_CPU_IDLE_DEEP:
            return tiku_cpu_boot_msp430_power_lpm3_enter;
        case TIKU_CPU_IDLE_DEEPEST:
            return tiku_cpu_boot_msp430_power_lpm4_enter;
        case TIKU_CPU_IDLE_OFF:
        default:
            return NULL;
    }
#elif defined(PLATFORM_RP2350)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
        case TIKU_CPU_IDLE_DEEP:
            /* Both map to a plain WFI on Cortex-M33: SysTick / TIMER /
             * UART RX still wake the core. */
            return tiku_cpu_boot_rp2350_power_wfi_enter;
        case TIKU_CPU_IDLE_DEEPEST:
            /* WFI as well: dormant mode, which stops the clocks, is not
             * used. */
            return tiku_cpu_boot_rp2350_power_wfi_enter;
        case TIKU_CPU_IDLE_OFF:
        default:
            return NULL;
    }
#elif defined(PLATFORM_AMBIQ)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
        case TIKU_CPU_IDLE_DEEP:
        case TIKU_CPU_IDLE_DEEPEST:
            /* Plain WFI on every Ambiq part: the STIMER tick and enabled
             * peripheral interrupts wake the core; SysTick stops in WFI. */
            return tiku_cpu_boot_ambiq_power_wfi_enter;
        case TIKU_CPU_IDLE_OFF:
        default:
            return NULL;
    }
#elif defined(PLATFORM_NORDIC)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
        case TIKU_CPU_IDLE_DEEP:
        case TIKU_CPU_IDLE_DEEPEST:
            /* Plain WFI: the GRTC tick (TIMER10 when selected) or any
             * enabled IRQ wakes the core. */
            return tiku_cpu_boot_nordic_power_wfi_enter;
        case TIKU_CPU_IDLE_OFF:
        default:
            return NULL;
    }
#elif defined(PLATFORM_RA8P1)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
        case TIKU_CPU_IDLE_DEEP:
        case TIKU_CPU_IDLE_DEEPEST:
            /* Sleep mode (WFI): SysTick, console RX and an armed htimer all
             * wake the core.  Software Standby is not used: it stops the
             * clocks, and only wake sources set in the ICU end it. */
            return tiku_cpu_boot_ra8p1_power_wfi_enter;
        case TIKU_CPU_IDLE_OFF:
        default:
            return NULL;
    }
#elif defined(PLATFORM_ESP32C61)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
            /* Plain WFI: the SYSTIMER tick and any enabled line wake the
             * core. */
            return tiku_cpu_boot_esp32c61_power_wfi_enter;
        case TIKU_CPU_IDLE_DEEP:
        case TIKU_CPU_IDLE_DEEPEST:
            /* PMU light sleep to the next timer deadline: timers and console
             * bytes end it and other interrupts wait, so it is opt-in rather
             * than the default mode. */
            return tiku_esp32c61_light_idle;
        case TIKU_CPU_IDLE_OFF:
        default:
            return NULL;
    }
#else
    (void)mode;
    return NULL;
#endif
}

int tiku_cpu_idle_mode_wakes_on_tick(tiku_cpu_idle_mode_t mode) {
#if defined(PLATFORM_MSP430)
    /* Timer A0 runs from ACLK, which survives LPM0-LPM3; its ISR
     * clears the LPM bits on exit.  LPM4 stops every clock, so the
     * tick never fires. */
    return mode != TIKU_CPU_IDLE_DEEPEST;
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
      defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6) || \
      defined(PLATFORM_RA8P1) || defined(PLATFORM_ESP32C61)
    /* The tick ends every mode on these ports: WFI wakes on any enabled
     * interrupt, and ESP32-C61 light sleep ends at the next SYSTIMER
     * alarm. */
    (void)mode;
    return 1;
#else
    (void)mode;
    return 1;
#endif
}

/** Every wake source: what ends a WFI-class sleep. */
#define IDLE_WAKES_ALL  (TIKU_WAKE_SYSTICK | TIKU_WAKE_HTIMER |            \
                         TIKU_WAKE_UART_RX | TIKU_WAKE_WDT | TIKU_WAKE_GPIO)

unsigned int tiku_cpu_idle_mode_wakes(tiku_cpu_idle_mode_t mode) {
#if defined(PLATFORM_MSP430)
    switch (mode) {
        case TIKU_CPU_IDLE_DEEP:
            /* LPM3 keeps only ACLK, which runs the tick and, in its ACLK
             * presets, the htimer.  The UART runs from SMCLK, as does the
             * htimer in its SMCLK presets (the default), and the watchdog
             * may too. */
#if TIKU_HTIMER_CLOCK_SOURCE == TIKU_HTIMER_SOURCE_ACLK
            return TIKU_WAKE_SYSTICK | TIKU_WAKE_HTIMER | TIKU_WAKE_GPIO;
#else
            return TIKU_WAKE_SYSTICK | TIKU_WAKE_GPIO;
#endif
        case TIKU_CPU_IDLE_DEEPEST:
            /* LPM4 stops every clock: only a pin edge wakes the core. */
            return TIKU_WAKE_GPIO;
        case TIKU_CPU_IDLE_OFF:
        case TIKU_CPU_IDLE_LIGHT:
        default:
            return IDLE_WAKES_ALL;
    }
#elif defined(PLATFORM_ESP32C61)
    switch (mode) {
        case TIKU_CPU_IDLE_DEEP:
        case TIKU_CPU_IDLE_DEEPEST:
            /* Light sleep ends on a SYSTIMER alarm or a byte at UART0;
             * other interrupts wait for the next alarm. */
            return TIKU_WAKE_SYSTICK | TIKU_WAKE_HTIMER | TIKU_WAKE_UART_RX;
        case TIKU_CPU_IDLE_OFF:
        case TIKU_CPU_IDLE_LIGHT:
        default:
            return IDLE_WAKES_ALL;
    }
#else
    /* Every mode is a WFI variant: any enabled interrupt wakes the core. */
    (void)mode;
    return IDLE_WAKES_ALL;
#endif
}

const char *tiku_cpu_idle_mode_name(tiku_cpu_idle_mode_t mode) {
#if defined(PLATFORM_MSP430)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:   return "LPM0";
        case TIKU_CPU_IDLE_DEEP:    return "LPM3";
        case TIKU_CPU_IDLE_DEEPEST: return "LPM4";
        case TIKU_CPU_IDLE_OFF:
        default:                    return "off";
    }
#elif defined(PLATFORM_ESP32C61)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:   return "WFI";
        case TIKU_CPU_IDLE_DEEP:    return "light sleep";
        case TIKU_CPU_IDLE_DEEPEST: return "light sleep";
        case TIKU_CPU_IDLE_OFF:
        default:                    return "off";
    }
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
      defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6) || \
      defined(PLATFORM_RA8P1)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:   return "WFI";
        case TIKU_CPU_IDLE_DEEP:    return "WFI";
        case TIKU_CPU_IDLE_DEEPEST: return "WFI";
        case TIKU_CPU_IDLE_OFF:
        default:                    return "off";
    }
#else
    (void)mode;
    return "off";
#endif
}

const char *tiku_cpu_idle_mode_desc(tiku_cpu_idle_mode_t mode) {
#if defined(PLATFORM_MSP430)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
            return "LPM0 (CPU off, SMCLK+ACLK on)";
        case TIKU_CPU_IDLE_DEEP:
            return "LPM3 (CPU+SMCLK off, ACLK on)";
        case TIKU_CPU_IDLE_DEEPEST:
            return "LPM4 (all clocks off)";
        case TIKU_CPU_IDLE_OFF:
        default:
            return "off (busy-wait)";
    }
#elif defined(PLATFORM_RP2350)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
        case TIKU_CPU_IDLE_DEEP:
        case TIKU_CPU_IDLE_DEEPEST:
            return "WFI (Cortex-M33 wait-for-interrupt)";
        case TIKU_CPU_IDLE_OFF:
        default:
            return "off (busy-wait)";
    }
#elif defined(PLATFORM_AMBIQ)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
        case TIKU_CPU_IDLE_DEEP:
        case TIKU_CPU_IDLE_DEEPEST:
            return "WFI (Cortex-M55 wait-for-interrupt)";
        case TIKU_CPU_IDLE_OFF:
        default:
            return "off (busy-wait)";
    }
#elif defined(PLATFORM_NORDIC)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
        case TIKU_CPU_IDLE_DEEP:
        case TIKU_CPU_IDLE_DEEPEST:
            return "WFI (Cortex-M33 wait-for-interrupt)";
        case TIKU_CPU_IDLE_OFF:
        default:
            return "off (busy-wait)";
    }
#elif defined(PLATFORM_ESP32C61)
    switch (mode) {
        case TIKU_CPU_IDLE_LIGHT:
            return "WFI (RISC-V wait-for-interrupt)";
        case TIKU_CPU_IDLE_DEEP:
        case TIKU_CPU_IDLE_DEEPEST:
            return "light sleep (core and PLL off; timers and console wake it)";
        case TIKU_CPU_IDLE_OFF:
        default:
            return "off (busy-wait)";
    }
#else
    (void)mode;
    return "off (busy-wait)";
#endif
}
