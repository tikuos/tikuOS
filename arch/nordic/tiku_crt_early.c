/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - Nordic nRF54L15 (Cortex-M33) startup
 *
 * The nRF54L boots directly from RRAM at 0x0 -- no XIP, no second-stage
 * bootloader, no image header (unlike RP2350).  The CPU loads SP from
 * word 0 and PC from word 1 of the vector table, which the linker places
 * at the very start of RRAM.  This file provides:
 *   1. Vector table (16 system exceptions + 272 external IRQ slots), all
 *      defaulting to a debuggable spin; real handlers override the weak
 *      aliases by name.
 *   2. Reset handler: mask IRQs, set VTOR, apply factory FICR trims (the
 *      one mandatory bit of the MDK SystemInit -- analog/clock trims), copy
 *      .data, zero .bss, leave .uninit for warm-reset state, call main().
 *
 * The port is All-Secure and built WITHOUT -mcmse, so the TrustZone parts
 * of the MDK SystemInit (SAU / KMU / TAMPC) are intentionally skipped;
 * only the trim application (its non-CMSE path) is reproduced here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <arch/nordic/mdk/nrf54l15.h>

/*---------------------------------------------------------------------------*/
/* Linker-script symbols                                                     */
/*---------------------------------------------------------------------------*/

extern uint32_t __data_load;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern uint32_t __stack;

extern int main(void);

/** @brief ISR function-pointer type used throughout the vector table. */
typedef void (*nordic_isr_t)(void);

/* nRF54L15 exposes external IRQs 0..271 (nrf54l15_application_vectors.h).
 * 16 system exceptions + 272 external = 288 slots (incl. the initial SP). */
#define NORDIC_NUM_EXT_IRQS  272
extern const nordic_isr_t tiku_nordic_vectors[16 + NORDIC_NUM_EXT_IRQS];

/*---------------------------------------------------------------------------*/
/* Default + weak exception handlers                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default ISR handler -- spin on WFE so an unhandled exception lands
 *        on a recognisable PC under a debugger rather than running wild.
 */
static void nordic_default_handler(void)
{
    for (;;) {
        __asm__ volatile ("wfe");
    }
}

/*
 * Weak aliases: each can be overridden by a non-weak definition of the same
 * name in any driver/kernel file.  SysTick and GRTC live here so the vector
 * table is complete before the timer driver installs the real handler.
 */
void tiku_nordic_nmi_handler(void)         __attribute__((weak, alias("nordic_default_handler")));
void tiku_nordic_hard_fault_handler(void)  __attribute__((weak, alias("nordic_default_handler")));
void tiku_nordic_mem_fault_handler(void)   __attribute__((weak, alias("nordic_default_handler")));
void tiku_nordic_bus_fault_handler(void)   __attribute__((weak, alias("nordic_default_handler")));
void tiku_nordic_usage_fault_handler(void) __attribute__((weak, alias("nordic_default_handler")));
void tiku_nordic_secure_fault_handler(void)__attribute__((weak, alias("nordic_default_handler")));
void tiku_nordic_svc_handler(void)         __attribute__((weak, alias("nordic_default_handler")));
void tiku_nordic_pendsv_handler(void)      __attribute__((weak, alias("nordic_default_handler")));
void tiku_nordic_systick_handler(void)     __attribute__((weak, alias("nordic_default_handler")));

/* External IRQ handlers wired as the port grows.  GRTC_0 (IRQn 226) drives
 * the low-power kernel tick; TIMER10 (IRQn 133) is the tick fallback.  The
 * rest default until their subsystem lands. */
void tiku_nordic_timer10_isr(void)         __attribute__((weak, alias("nordic_default_handler")));
void tiku_nordic_grtc_isr(void)            __attribute__((weak, alias("nordic_default_handler")));
/* Console UARTE RX -- SERIAL20 (198, UARTE20) or SERIAL30 (260, UARTE30); the
 * table wires both, only the selected console's line is NVIC-enabled. */
void tiku_nordic_uart_console_isr(void)    __attribute__((weak, alias("nordic_default_handler")));
/* Hardware one-shot htimer -- TIMER20 COMPARE (IRQn 202). */
void tiku_nordic_timer20_isr(void)         __attribute__((weak, alias("nordic_default_handler")));
/* GPIO edge interrupts -- GPIOTE20 line 0 (218, P1/P2), GPIOTE30 line 0
 * (268, P0); each posts TIKU_EVENT_GPIO for the pin that fired. */
void tiku_nordic_gpiote20_isr(void)        __attribute__((weak, alias("nordic_default_handler")));
void tiku_nordic_gpiote30_isr(void)        __attribute__((weak, alias("nordic_default_handler")));
/* FLPR coprocessor doorbell -- VPR00 EVENTS_TRIGGERED (IRQn 76). */
void tiku_nordic_flpr_isr(void)            __attribute__((weak, alias("nordic_default_handler")));

/*---------------------------------------------------------------------------*/
/* Factory trim application + silicon errata (minimal SystemInit)            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Apply factory analog/clock trims from FICR->TRIMCNF.
 *
 * The nRF54L ships per-die trim values in FICR->TRIMCNF[] as (register
 * address, value) pairs, terminated by an ADDR of 0xFFFFFFFF (or 0).  The
 * MDK SystemInit copies each value into its target register; without this
 * the HFXO / regulators / ADC run untrimmed.  This reproduces the MDK's
 * non-TrustZone (non-CMSE) path exactly.
 */
static void tiku_nordic_apply_trims(void)
{
    uint32_t i;

    for (i = 0u; i < FICR_TRIMCNF_MaxCount; i++) {
        uint32_t addr = NRF_FICR_NS->TRIMCNF[i].ADDR;
        if (addr == 0xFFFFFFFFul || addr == 0x00000000ul) {
            break;
        }
        *((volatile uint32_t *)addr) = NRF_FICR_NS->TRIMCNF[i].DATA;
    }
}

/* FICR silicon-identification words used to gate revision-specific errata
 * (nRF54L15 errata sheet 4503_401 + the MDK's nrf54l_erratas.h convention):
 * 0x00FFC340 = part marker (0x1C on nRF54L15), 0x00FFC344 = revision code
 * (0x01 gates the workarounds MDK applies conditionally), 0x00FFC334 = trim
 * version (extra gate on erratum 32). */
#define NORDIC_FICR_PART  (*(volatile uint32_t *)0x00FFC340ul)
#define NORDIC_FICR_REV   (*(volatile uint32_t *)0x00FFC344ul)
#define NORDIC_FICR_TRIMV (*(volatile uint32_t *)0x00FFC334ul)

/**
 * @brief Silicon errata workarounds the stock MDK SystemInit applies and a
 *        from-scratch startup must reproduce (raw register pokes from the
 *        public nRF54L15 errata sheet; every Nordic-SDK app gets these).
 *
 * Split around the trim loop to preserve the MDK's ordering: erratum 37
 * (TAD poke, all revisions) runs before trims; the regulator/RADIO pokes
 * run after.  Miss these and the core boots fine but analog behaviour is
 * off -- erratum 40 in particular parks a reserved RADIO register at a
 * value the RF front-end needs.
 */
static void tiku_nordic_sysinit_errata_early(void)
{
    /* Erratum 37 (all revs): current can stay high after pin reset or power
     * cycle unless this TAD register is set. */
    *(volatile uint32_t *)(NRF_TAD_S_BASE + 0x40Cul) = 1ul;
}

static void tiku_nordic_sysinit_errata(void)
{
    /* ES-PDK regulator configuration (MDK SystemInit, all nRF54L15): prime
     * 0x50120440 when it reads as unprogrammed. */
    if (*(volatile uint32_t *)0x50120440ul == 0ul) {
        *(volatile uint32_t *)0x50120440ul = 0xC8ul;
    }

    if (NORDIC_FICR_PART != 0x1Cul || NORDIC_FICR_REV != 0x01ul) {
        return;                        /* remaining pokes are rev-1-gated   */
    }

    /* Erratum 32: regulator trim correction, additionally gated on the
     * FICR trim version (later lots carry corrected trims). */
    if (NORDIC_FICR_TRIMV <= 0x180A1D00ul) {
        *(volatile uint32_t *)0x50120640ul = 0x1EA9E040ul;
    }

    /* Erratum 40: RADIO band-edge transient power -- reserved RADIO
     * register (RADIO base + 0x7AC) must hold this value. */
    *(volatile uint32_t *)0x5008A7ACul = 0x040A0078ul;

    /* Erratum 31: sleep-current regulator configuration (must run at
     * start-up, before any DC/DC use). */
    *(volatile uint32_t *)0x50120624ul = (20ul | (1ul << 5));
    *(volatile uint32_t *)0x5012063Cul &= ~(1ul << 19);
}

/*---------------------------------------------------------------------------*/
/* Reset handler                                                             */
/*---------------------------------------------------------------------------*/

void tiku_nordic_reset_handler(void) __attribute__((naked, section(".text"), used));

/**
 * @brief nRF54L15 reset handler: C-runtime init and entry to main().
 *
 * SP is already loaded by the CPU from vector[0].  Mask IRQs immediately
 * (Cortex-M resets with PRIMASK=0; an early-armed source such as SysTick
 * must not fire before the scheduler builds its queues -- the scheduler
 * re-enables IRQs at the top of its loop).  Then point VTOR at our table,
 * apply factory trims, copy .data, zero .bss (leaving .uninit for
 * warm-reset state), and call main().  Naked: no compiler prologue that
 * would touch uninitialised call-saved registers.
 */
void tiku_nordic_reset_handler(void)
{
    __asm__ volatile ("cpsid i" ::: "memory");

    /* VTOR -> our vector table (RRAM base). The CPU boots with VTOR=0 which
     * already points here, but set it explicitly so a relocated table or a
     * warm reboot lands deterministically. */
    *(volatile uint32_t *)0xE000ED08U = (uint32_t)tiku_nordic_vectors;

    /* Enable the FPU (CPACR CP10/CP11 full access) up front: the softfp +
     * fpv5-sp-d16 build can compile single-precision float ops to VFP
     * instructions, which fault (NOCP UsageFault -> HardFault) with the FPU
     * off.  The nRF54L has no bootloader to do this and the crt runs first.
     * CPACR is at 0xE000ED88; DSB+ISB so it takes effect before any FP op. */
    *(volatile uint32_t *)0xE000ED88U |= (0xFU << 20);
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");

    tiku_nordic_sysinit_errata_early();
    tiku_nordic_apply_trims();
    tiku_nordic_sysinit_errata();

    /* Copy .data (RRAM load image -> SRAM). */
    {
        uint32_t *src = &__data_load;
        uint32_t *dst = &__data_start;
        while (dst < &__data_end) {
            *dst++ = *src++;
        }
    }

    /* Zero .bss. */
    {
        uint32_t *dst = &__bss_start;
        while (dst < &__bss_end) {
            *dst++ = 0U;
        }
    }

    /* .uninit is intentionally left untouched (warm-reset survivor state). */

    (void)main();

    for (;;) {
        __asm__ volatile ("wfe");
    }
}

/*---------------------------------------------------------------------------*/
/* Vector table                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Cortex-M33 vector table for nRF54L15.
 *
 * Placed in .vectors, which the linker locates at the base of RRAM (0x0)
 * aligned to the VTOR requirement.  Index 0 is the initial SP; 1..15 are
 * the ARMv8-M system exceptions; 16.. are the 272 external IRQs.  Unused
 * external slots are filled with nordic_default_handler so a spurious IRQ
 * spins in a debuggable loop rather than dispatching through a NULL slot.
 */
const nordic_isr_t tiku_nordic_vectors[16 + NORDIC_NUM_EXT_IRQS]
__attribute__((section(".vectors"), used)) = {
    /* System exceptions ------------------------------------------------ */
    (nordic_isr_t)(&__stack),                  /*  0  Initial SP        */
    tiku_nordic_reset_handler,                 /*  1  Reset             */
    tiku_nordic_nmi_handler,                   /*  2  NMI               */
    tiku_nordic_hard_fault_handler,            /*  3  HardFault         */
    tiku_nordic_mem_fault_handler,             /*  4  MemManage         */
    tiku_nordic_bus_fault_handler,             /*  5  BusFault          */
    tiku_nordic_usage_fault_handler,           /*  6  UsageFault        */
    tiku_nordic_secure_fault_handler,          /*  7  SecureFault (v8M) */
    nordic_default_handler,                    /*  8  Reserved          */
    nordic_default_handler,                    /*  9  Reserved          */
    nordic_default_handler,                    /* 10  Reserved          */
    tiku_nordic_svc_handler,                   /* 11  SVC               */
    nordic_default_handler,                    /* 12  DebugMon          */
    nordic_default_handler,                    /* 13  Reserved          */
    tiku_nordic_pendsv_handler,                /* 14  PendSV            */
    tiku_nordic_systick_handler,               /* 15  SysTick           */

    /* External interrupts -- IRQ numbers are the MDK IRQn enum values
     * (nrf54l15_application.h), NOT the vector-array position. */
    [16 +  76] = tiku_nordic_flpr_isr,         /* VPR00_IRQn      = 76  */
    [16 + 133] = tiku_nordic_timer10_isr,      /* TIMER10_IRQn    = 133 */
    [16 + 198] = tiku_nordic_uart_console_isr, /* SERIAL20_IRQn   = 198 */
    [16 + 202] = tiku_nordic_timer20_isr,      /* TIMER20_IRQn    = 202 (htimer)  */
    [16 + 218] = tiku_nordic_gpiote20_isr,     /* GPIOTE20_0_IRQn = 218 (P1/P2)   */
    [16 + 226] = tiku_nordic_grtc_isr,         /* GRTC_0_IRQn     = 226 */
    [16 + 260] = tiku_nordic_uart_console_isr, /* SERIAL30_IRQn   = 260 */
    [16 + 268] = tiku_nordic_gpiote30_isr,     /* GPIOTE30_0_IRQn = 268 (P0)      */

    /* Fill every remaining external slot with the default handler so no
     * slot dispatches through a NULL pointer.  Ranges are split around the
     * explicitly-wired IRQs above (no overlapping designated initializers). */
    [16 +   0 ... 16 +  75] = nordic_default_handler,
    [16 +  77 ... 16 + 132] = nordic_default_handler,
    [16 + 134 ... 16 + 197] = nordic_default_handler,
    [16 + 199 ... 16 + 201] = nordic_default_handler,
    [16 + 203 ... 16 + 217] = nordic_default_handler,
    [16 + 219 ... 16 + 225] = nordic_default_handler,
    [16 + 227 ... 16 + 259] = nordic_default_handler,
    [16 + 261 ... 16 + 267] = nordic_default_handler,
    [16 + 269 ... 16 + 271] = nordic_default_handler,
};
