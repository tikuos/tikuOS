/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu.c - Platform-agnostic CPU abstraction implementation
 *
 * Provides atomic section entry/exit, IRQ control, clock-rate
 * queries, and idle-mode hooks. On MSP430 these forward to the
 * tiku_cpu_msp430_* / tiku_cpu_boot_msp430_* arch functions; the ARM
 * ports (RP2350, Apollo510) share the PRIMASK/cpsid atomics and forward
 * boot/clock/idle to their own arch backends.
 *
 * The atomic functions use MSP430 intrinsics directly (<msp430.h>) rather
 * than including tiku.h, because tiku.h defines PLATFORM_MSP430 which
 * would also activate tiku_cpu_boot_init() and tiku_cpu_freq_init() —
 * those require proper clock/pin configuration that is not yet ready
 * at the HAL level.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include "tiku_cpu.h"

#if defined(PLATFORM_MSP430)
#include <msp430.h>    /* MSP430 intrinsics for interrupt state management */
#include "arch/msp430/tiku_cpu_freq_boot_arch.h"
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC) || defined(PLATFORM_NORDIC)
#if defined(PLATFORM_RP2350)
#include "arch/arm-rp2350/tiku_cpu_freq_boot_arch.h"
#elif defined(PLATFORM_AMBIQ)
#include "arch/ambiq/tiku_cpu_freq_boot_arch.h"
#else
#include "arch/nordic/tiku_cpu_freq_boot_arch.h"
#endif
#include <stdint.h>

/* ARM Cortex-M PRIMASK helpers. We intentionally avoid pulling in
 * <cmsis_gcc.h> or <core_cm33.h> to keep tikuOS dependency-free. The
 * instructions are identical on Cortex-M33 (RP2350 / nRF54L) and M55
 * (Apollo510). */
static inline uint32_t tiku_arm_get_primask(void) {
    uint32_t v;
    __asm__ volatile ("mrs %0, primask" : "=r"(v));
    return v;
}
static inline void tiku_arm_set_primask(uint32_t v) {
    __asm__ volatile ("msr primask, %0" : : "r"(v) : "memory");
}
static inline void tiku_arm_disable_irq(void) {
    __asm__ volatile ("cpsid i" ::: "memory");
}
static inline void tiku_arm_enable_irq(void) {
    __asm__ volatile ("cpsie i" ::: "memory");
}
#endif

/*---------------------------------------------------------------------------*/
/* ATOMIC NESTING                                                            */
/*---------------------------------------------------------------------------*/

/*
 * Atomic section nesting depth and saved GIE state.
 *
 * On the outermost tiku_atomic_enter() (nesting == 0) we snapshot
 * the current GIE bit BEFORE disabling interrupts.  On the matching
 * outermost tiku_atomic_exit() we only re-enable interrupts if GIE
 * was originally set.  This prevents atomic sections from
 * unconditionally enabling interrupts as a side-effect.
 *
 * ISR safety: if an ISR fires between __get_interrupt_state() and
 * __disable_interrupt() during the outermost enter, the ISR runs its
 * own balanced enter/exit pair (which sees nesting == 0, saves
 * GIE == 0 since the hardware clears GIE on ISR entry, and does not
 * re-enable on exit).  When the ISR returns via RETI, GIE is
 * restored from the stacked SR, and the main context continues with
 * its local `sr` still valid on the stack.
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
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC)
  /* PRIMASK = 0 means IRQs enabled; PRIMASK = 1 means masked. We
   * snapshot the bit on the outermost entry and restore on the
   * outermost exit, mirroring the MSP430 GIE handling above. */
  uint32_t pm = tiku_arm_get_primask();
  tiku_arm_disable_irq();
  if (tiku_atomic_nesting == 0) {
    tiku_atomic_gie_saved = (pm == 0);  /* 1 if IRQs were enabled */
  }
  tiku_atomic_nesting++;
#endif
}

void tiku_atomic_exit(void) {
  if (--tiku_atomic_nesting == 0 && tiku_atomic_gie_saved) {
#if defined(PLATFORM_MSP430)
    __enable_interrupt();
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC)
    tiku_arm_enable_irq();
#endif
  }
}

/*---------------------------------------------------------------------------*/
/* IRQ CONTROL                                                               */
/*---------------------------------------------------------------------------*/

void tiku_cpu_irq_enable(void) {
#if defined(PLATFORM_MSP430)
    __enable_interrupt();
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC)
    tiku_arm_enable_irq();
#endif
}

void tiku_cpu_irq_disable(void) {
#if defined(PLATFORM_MSP430)
    __disable_interrupt();
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC)
    tiku_arm_disable_irq();
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
#endif
}

void tiku_cpu_dcache_clean(const void *addr, unsigned long len) {
#if defined(PLATFORM_MSP430)
    (void)addr; (void)len;            /* no data cache */
#elif defined(PLATFORM_RP2350)
    (void)addr; (void)len;            /* XIP cache: no D-side coherency op needed */
#elif defined(PLATFORM_AMBIQ)
    tiku_cpu_ambiq_dcache_clean(addr, len);
#elif defined(PLATFORM_NORDIC)
    (void)addr; (void)len;            /* nRF54L M33: no data cache */
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
    (void)addr; (void)len;            /* nRF54L M33: no data cache */
#endif
}

void tiku_cpu_icache_invalidate(void) {
#if defined(PLATFORM_AMBIQ)
    tiku_cpu_ambiq_icache_invalidate();
#endif
    /* MSP430 / RP2350 / nRF54L M33: no instruction cache -- no-op. */
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
             * UART RX still wake us. */
            return tiku_cpu_boot_rp2350_power_wfi_enter;
        case TIKU_CPU_IDLE_DEEPEST:
            /* Dormant mode would be deeper but is harder to bring back
             * without losing state — skip for the first port. */
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
            /* Plain WFI on Cortex-M55 — SysTick / STIMER / peripherals
             * still wake us. Deeper Ambiq sleep modes land later. */
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
            /* Plain WFI on Cortex-M33 — the TIMER10 tick / any enabled IRQ
             * still wakes us.  Deeper nRF54L System OFF sleep lands later. */
            return tiku_cpu_boot_nordic_power_wfi_enter;
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
     * tick can never fire, let alone wake us. */
    return mode != TIKU_CPU_IDLE_DEEPEST;
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC)
    /* Every supported mode is a WFI variant; any enabled interrupt
     * (SysTick / STIMER tick included) wakes the core. */
    (void)mode;
    return 1;
#else
    (void)mode;
    return 1;
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
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC)
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
#else
    (void)mode;
    return "off (busy-wait)";
#endif
}
