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

/* External IRQ handlers wired as the port grows.  GRTC (IRQn 227) drives
 * the kernel tick; the rest default until their subsystem lands. */
void tiku_nordic_grtc_isr(void)            __attribute__((weak, alias("nordic_default_handler")));

/*---------------------------------------------------------------------------*/
/* Factory trim application (minimal SystemInit)                             */
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

    tiku_nordic_apply_trims();

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

    /* External interrupts (nrf54l15_application_vectors.h) ------------- */
    [16 + 227] = tiku_nordic_grtc_isr,         /* IRQ 227  GRTC_0       */

    /* Fill every remaining external slot with the default handler so no
     * slot dispatches through a NULL pointer. */
    [16 +   0 ... 16 + 226] = nordic_default_handler,
    [16 + 228 ... 16 + 271] = nordic_default_handler,
};
