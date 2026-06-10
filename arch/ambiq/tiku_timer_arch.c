/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_timer_arch.c - Apollo 510 system tick (Cortex-M SysTick)
 *
 * Drives the system clock at TIKU_CLOCK_ARCH_SECOND Hz from the core
 * clock. SysTick is a Cortex-M core peripheral (same SCS registers on
 * M55 as M33), so this is bare-metal — no AmbiqSuite dependency except
 * the coarse busy-delay helper.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "tiku_timer_arch.h"
#include "kernel/scheduler/tiku_sched.h"
#include "am_util.h"   /* @ambiq-sdk: am_util_delay_us for the coarse delay */

/* Cortex-M SysTick (System Control Space). */
#define SYST_CSR  (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR  (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR  (*(volatile uint32_t *)0xE000E018UL)
#define SYST_CSR_ENABLE     (1u << 0)
#define SYST_CSR_TICKINT    (1u << 1)
#define SYST_CSR_CLKSOURCE  (1u << 2)   /* processor clock */

static volatile unsigned long  s_ticks   = 0;
static volatile unsigned long  s_seconds = 0;
static volatile unsigned int   s_subsec  = 0;

void tiku_clock_arch_init(void) {
    SYST_RVR = (uint32_t)(TIKU_CLOCK_ARCH_INTERVAL - 1u);
    SYST_CVR = 0u;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
}

/* SysTick exception handler (vector slot 15 in tiku_crt_early.c). */
void tiku_ambiq_systick_handler(void) {
    s_ticks++;
    if (++s_subsec >= TIKU_CLOCK_ARCH_SECOND) {
        s_subsec = 0;
        s_seconds++;
    }
    tiku_sched_notify();
}

tiku_clock_arch_time_t tiku_clock_arch_time(void) {
    return (tiku_clock_arch_time_t)s_ticks;
}

unsigned long tiku_clock_arch_seconds(void)        { return s_seconds; }
void          tiku_clock_arch_set_seconds(unsigned long sec) { s_seconds = sec; }

void tiku_clock_arch_wait(tiku_clock_arch_time_t t) {
    tiku_clock_arch_time_t target = (tiku_clock_arch_time_t)s_ticks + t;
    while ((long)(target - (tiku_clock_arch_time_t)s_ticks) > 0) {
        /* spin — relies on SysTick advancing s_ticks */
    }
}

void tiku_clock_arch_delay(unsigned int us) {
    am_util_delay_us(us);   /* @ambiq-sdk */
}

/* Sub-tick resolution not modelled yet (coarse). Safe non-zero max. */
unsigned short tiku_clock_arch_fine(void)     { return 0; }
int            tiku_clock_arch_fine_max(void) { return 1; }
unsigned char  tiku_clock_arch_fault(void)    { return 0; }
