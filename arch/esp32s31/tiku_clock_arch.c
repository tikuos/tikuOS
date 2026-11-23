/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_clock_arch.c - S31 CPU rate: the 40 MHz crystal or the 320 MHz CPLL,
 * switched at run time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_clock_arch.h"
#include <stddef.h>
#include <stdint.h>

/* Register access, interrupt masking and the ROM's rate update; a host
 * model defines its own before including this file. */
#ifndef TIKU_S31_REG_READ
#define TIKU_S31_REG_READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define TIKU_S31_REG_WRITE(a, v) (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#endif
#ifndef TIKU_S31_IRQ_SAVE
static uint32_t clock_irq_save(void)
{
    uint32_t state;

    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(state) :: "memory");
    return state;
}
static void clock_irq_restore(uint32_t state)
{
    if (state & 8u) __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
}
#define TIKU_S31_IRQ_SAVE() clock_irq_save()
#define TIKU_S31_IRQ_RESTORE(state) clock_irq_restore(state)
#endif
#ifndef TIKU_S31_ROM_CPU_MHZ
#define TIKU_S31_ROM_CPU_MHZ(mhz) ((void (*)(unsigned))0x2f800044u)(mhz)
#endif
#ifndef TIKU_S31_PSRAM_MHZ
#define TIKU_S31_PSRAM_MHZ 20
#endif

#define CLOCK 0x20587000u          /* HP_SYS_CLKRST */
#define SEL (CLOCK + 0x0u)         /* [1:0] root: 0 crystal, 1 CPLL */
#define CPU (CLOCK + 0x4u)         /* [7:0] divider - 1 */
#define MEM (CLOCK + 0x8u)         /* [0] divider - 1 */
#define SYS (CLOCK + 0xCu)         /* [7:0] divider - 1 */
#define APB (CLOCK + 0x10u)        /* [7:0] divider - 1 */
#define UPDATE (CLOCK + 0x14u)     /* [0] commits SEL and the dividers */
#define CPLL_DIV 0x2070104Cu       /* 0x81: 40 MHz x 8, the 320 MHz CPLL */
#define CPLL_CAL (CLOCK + 0x174u)  /* [2] calibration ended: locked */
#define UART_STATUS 0x2038A01Cu    /* [23:16] bytes in the TX FIFO */
#define UART_FSM 0x2038A070u       /* [7:4] transmitter state, 0 idle */
#define WAIT 100000u

/* Each rate's root and dividers: memory = CPU / mem at most 160 MHz,
 * system = CPU / sys at most 320/3 MHz and APB = system / apb at most
 * 320/6 MHz, the sets ESP-IDF's rtc_clk.c uses. */
typedef struct {
    uint16_t mhz;
    uint8_t sel, cpu, mem, sys, apb;
} s31_rate_t;
static const s31_rate_t rates[] = {
    {  40, 0u, 1u, 1u, 1u, 1u },
    {  80, 1u, 4u, 1u, 1u, 2u },
    { 160, 1u, 2u, 1u, 2u, 2u },
    { 320, 1u, 1u, 2u, 3u, 2u },
};
static unsigned clock_mhz = 40u;   /* boot leaves the crystal at /1 */
static int clock_failed;

static const s31_rate_t *rate_of(unsigned mhz)
{
    for (size_t i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        if (rates[i].mhz == mhz) return &rates[i];
    }
    return NULL;
}

static int cpll_locked(void)
{
    return (TIKU_S31_REG_READ(CPLL_DIV) & 0xfffu) == 0x81u &&
           (TIKU_S31_REG_READ(CPLL_CAL) & 4u) != 0u;
}

int tiku_s31_clock_offers(unsigned mhz)
{
    const s31_rate_t *r = rate_of(mhz);

    /* PSRAM's AXI path underflows below three quarters of its bus rate. */
    if (r == NULL || 4u * mhz < 3u * TIKU_S31_PSRAM_MHZ) return 0;
    return r->sel == 0u || cpll_locked();
}

/** @brief Write @p r's dividers and root and commit them together.
 *  @return 1 when the commit ended and every field reads back */
static int commit(const s31_rate_t *r)
{
    unsigned i;

    TIKU_S31_REG_WRITE(CPU, (TIKU_S31_REG_READ(CPU) & ~0x3fffu) | (r->cpu - 1u));
    TIKU_S31_REG_WRITE(MEM, (TIKU_S31_REG_READ(MEM) & ~1u) | (r->mem - 1u));
    TIKU_S31_REG_WRITE(SYS, (TIKU_S31_REG_READ(SYS) & ~0x3fffu) | (r->sys - 1u));
    TIKU_S31_REG_WRITE(APB, (TIKU_S31_REG_READ(APB) & ~0x3fffu) | (r->apb - 1u));
    TIKU_S31_REG_WRITE(SEL, (TIKU_S31_REG_READ(SEL) & ~3u) | r->sel);
    TIKU_S31_REG_WRITE(UPDATE, TIKU_S31_REG_READ(UPDATE) | 1u);
    for (i = 0; i < WAIT && (TIKU_S31_REG_READ(UPDATE) & 1u); i++) { }
    return i < WAIT && (TIKU_S31_REG_READ(SEL) & 3u) == r->sel &&
           (TIKU_S31_REG_READ(CPU) & 0x3fffu) == r->cpu - 1u &&
           (TIKU_S31_REG_READ(MEM) & 1u) == r->mem - 1u &&
           (TIKU_S31_REG_READ(SYS) & 0x3fffu) == r->sys - 1u &&
           (TIKU_S31_REG_READ(APB) & 0x3fffu) == r->apb - 1u;
}

/** @brief The CPU rate the registers set, in MHz: the root over the CPU
 *  divider. */
static unsigned live_mhz(void)
{
    unsigned root = (TIKU_S31_REG_READ(SEL) & 3u) == 1u ? 320u : 40u;

    return root / ((TIKU_S31_REG_READ(CPU) & 0xffu) + 1u);
}

int tiku_s31_clock_set(unsigned mhz)
{
    const s31_rate_t *r = rate_of(mhz), *was = rate_of(clock_mhz);
    uint32_t state;
    unsigned i;
    int ok;

    if (!tiku_s31_clock_offers(mhz)) return -1;
    if (mhz == clock_mhz) return 0;
    state = TIKU_S31_IRQ_SAVE();
    /* The console sends what it holds first, as ESP-IDF waits for it. */
    for (i = 0; i < 20u * WAIT &&
                ((((TIKU_S31_REG_READ(UART_STATUS) >> 16) & 255u) != 0u) ||
                 (((TIKU_S31_REG_READ(UART_FSM) >> 4) & 15u) != 0u)); i++) { }
    ok = commit(r);
    if (ok) {
        clock_mhz = mhz;
    } else {
        clock_failed = 1;
        clock_mhz = commit(was) ? was->mhz : live_mhz();
    }
    TIKU_S31_ROM_CPU_MHZ(clock_mhz);
    TIKU_S31_IRQ_RESTORE(state);
    return ok ? 0 : -1;
}

unsigned tiku_s31_clock_mhz(void)
{
    return clock_mhz;
}

unsigned long tiku_s31_clock_apb_hz(void)
{
    const s31_rate_t *r = rate_of(clock_mhz);

    if (r == NULL) {
        return (unsigned long)live_mhz() * 1000000UL /
               (((TIKU_S31_REG_READ(SYS) & 0xffu) + 1u) * ((TIKU_S31_REG_READ(APB) & 0xffu) + 1u));
    }
    return (unsigned long)r->mhz * 1000000UL / ((unsigned long)r->sys * r->apb);
}

int tiku_s31_clock_fault(void)
{
    return clock_failed;
}
