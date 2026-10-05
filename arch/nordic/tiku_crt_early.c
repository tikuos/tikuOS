/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - Nordic nRF54L (Cortex-M33) startup.
 *
 * The part boots directly from RRAM at 0x0, with no XIP, bootloader or image
 * header.  This file supplies the vector table of weak spin handlers and the
 * reset handler: errata, FICR trims, .data copy, .bss zeroing, then main().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <arch/nordic/tiku_nordic_mdk.h>

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

/* External IRQ count differs per device (highest MDK IRQn + 1):
 *   nRF54L15   : IRQs 0..271 (max = GPIOTE30_1 269)  -> 272 external
 *   nRF54LM20A : IRQs 0..289 (max = VREGUSB 289)     -> 290 external
 * 16 system exceptions + N external + the initial SP form the table.  The
 * named handlers below sit at identical IRQn indices on both parts (the IRQ
 * enum values match); only the array length and the trailing default-fill
 * range change. */
#if defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
#define NORDIC_NUM_EXT_IRQS  290
#else
#define NORDIC_NUM_EXT_IRQS  272
#endif
extern const nordic_isr_t tiku_nordic_vectors[16 + NORDIC_NUM_EXT_IRQS];

/*---------------------------------------------------------------------------*/
/* Default + weak exception handlers                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default ISR handler: spin on WFE, so an unhandled exception stops
 *        at a recognisable PC under a debugger.
 */
static void nordic_default_handler(void)
{
    for (;;) {
        __asm__ volatile ("wfe");
    }
}

/*
 * Weak aliases: a non-weak definition of the same name in any driver or
 * kernel file overrides each one, so the table links in every build,
 * including one without the driver that handles a line.
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

/* External IRQ handlers.  GRTC_0 (IRQn 226) drives the low-power kernel
 * tick; TIMER10 (IRQn 133) is the alternate tick (TIKU_NORDIC_TICK_TIMER10).
 * A line whose driver is not in the build keeps the default handler. */
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
/* 2.4 GHz RADIO -- RADIO_0 (IRQn 138 = peripheral 0x8A at 0x5008A000), used
 * by the radio driver's interrupt-driven scan. */
void tiku_nordic_radio_isr(void)           __attribute__((weak, alias("nordic_default_handler")));
/* Axon NPU (nRF54LM20B only; IRQn 86).  The slot is wired on every Nordic
 * device and keeps the default handler unless the Axon platform overrides
 * it. */
void tiku_nordic_axons_isr(void)           __attribute__((weak, alias("nordic_default_handler")));
/* USB high speed: the DWC2 core's interrupt (90) and the VBUS regulator's
 * (289, nRF54LM20 only -- past the nRF54L15's table). */
void tiku_nordic_usbhs_isr(void)           __attribute__((weak, alias("nordic_default_handler")));
void tiku_nordic_vregusb_isr(void)         __attribute__((weak, alias("nordic_default_handler")));

/*---------------------------------------------------------------------------*/
/* Factory trim application + silicon errata (minimal SystemInit)            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Apply factory analog/clock trims from FICR->TRIMCNF.
 *
 * The nRF54L ships per-die trim values as (register address, value) pairs
 * terminated by an ADDR of 0xFFFFFFFF or 0.  Without copying each into its
 * target register the HFXO, regulators and ADC run untrimmed.
 *
 * @note Follows the MDK SystemInit non-TrustZone (non-CMSE) path.
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

/* FICR silicon-identification words that gate revision-specific errata
 * (nRF54L errata sheet 4503_401 + the MDK's nrf54l_erratas.h convention):
 * 0x00FFC340 = part marker (0x1C on nRF54L15, 0x29 on nRF54LM20A),
 * 0x00FFC344 = revision code (0x01/0x02 on nRF54L15), 0x00FFC334 = trim
 * version (extra gate on erratum 32). */
#define NORDIC_FICR_PART  (*(volatile uint32_t *)0x00FFC340ul)
#define NORDIC_FICR_REV   (*(volatile uint32_t *)0x00FFC344ul)
#define NORDIC_FICR_TRIMV (*(volatile uint32_t *)0x00FFC334ul)

/**
 * @brief Errata workarounds that run before the factory trims (erratum 37).
 *
 * Register writes from the nRF54L errata sheet and nrfx system_nrf54l.c, in
 * the MDK's order around the trim loop.  Erratum 37 applies to the nRF54L15
 * (part 0x1C) and nRF54LM20A (0x29) alike.
 */
static void tiku_nordic_sysinit_errata_early(void)
{
    /* Erratum 37 (nRF54L15 + nRF54LM20A): current can stay high after pin
     * reset or power cycle unless this TAD register is set. */
    *(volatile uint32_t *)(NRF_TAD_S_BASE + 0x40Cul) = 1ul;
}

/*---------------------------------------------------------------------------*/
/* Debug-access (TAMPC) unlock -- SystemInit parity                          */
/*---------------------------------------------------------------------------*/

/* TAMPC signal-control words (MDK system_nrf54l_approtect.h vocabulary).
 * Every CTRL register shares the DBGEN field layout, so one set of
 * constants serves DBGEN/NIDEN/SPIDEN/SPNIDEN and the AP DBGEN. */
#define NORDIC_TAMPC_LOCKED \
    (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_LOCK_Enabled \
         << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_LOCK_Pos)
#define NORDIC_TAMPC_CLEAR_WP \
    ((TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_WRITEPROTECTION_Clear \
         << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_WRITEPROTECTION_Pos) | \
     (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_KEY_KEY \
         << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_KEY_Pos))
#define NORDIC_TAMPC_OPEN \
    ((TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_VALUE_High \
         << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_VALUE_Pos) | \
     (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_LOCK_Disabled \
         << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_LOCK_Pos) | \
     (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_KEY_KEY \
         << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_KEY_Pos))

/**
 * @brief Drive one TAMPC debug signal open (unless a prior session locked it).
 *
 * The MDK's nrf54l_handle_approtect_signal() default branch: clear the write
 * protection, then set VALUE=High with LOCK=Disabled.  A signal locked by
 * hardware or UICR is left alone; there is no ENABLE_APPROTECT branch.
 */
static void tiku_nordic_tampc_open(volatile uint32_t *sig)
{
    if ((*sig & NORDIC_TAMPC_LOCKED) != 0ul) {
        return;
    }
    *sig = NORDIC_TAMPC_CLEAR_WP;
    *sig = NORDIC_TAMPC_OPEN;
}

/**
 * @brief Re-open the debug port at every boot (MDK nrf54l_handle_approtect).
 *
 * With UICR.APPROTECT erased the debug enables are under firmware control;
 * this drives TAMPC DBGEN/NIDEN/SPIDEN/SPNIDEN and AUX AP DBGEN high.  Left
 * low after a watchdog reset (nRF54LM20-DK), AP-Protect needs a chip erase.
 */
static void tiku_nordic_debug_unlock(void)
{
    tiku_nordic_tampc_open(&NRF_TAMPC_S->PROTECT.DOMAIN[0].DBGEN.CTRL);
    tiku_nordic_tampc_open(&NRF_TAMPC_S->PROTECT.DOMAIN[0].NIDEN.CTRL);
    tiku_nordic_tampc_open(&NRF_TAMPC_S->PROTECT.DOMAIN[0].SPIDEN.CTRL);
    tiku_nordic_tampc_open(&NRF_TAMPC_S->PROTECT.DOMAIN[0].SPNIDEN.CTRL);
    tiku_nordic_tampc_open(&NRF_TAMPC_S->PROTECT.AP[0].DBGEN.CTRL);
}

/**
 * @brief Errata workarounds that run after the factory trims: the ES-PDK
 *        regulator prime and errata 31/32/40, on the nRF54L15 only.
 */
static void tiku_nordic_sysinit_errata(void)
{
#if defined(TIKU_DEVICE_NRF54L15)
    /* ES-PDK regulator configuration (MDK SystemInit, nRF54L05/L10/L15 only):
     * prime 0x50120440 when it reads as unprogrammed. */
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
#else
    /* nRF54LM20A: errata 31/32/40 and the ES-PDK regulator prime do not
     * apply to part 0x29 (see nrf54l_erratas.h); only erratum 37 (applied
     * above) and the FICR trims do. */
    (void)NORDIC_FICR_PART; (void)NORDIC_FICR_REV; (void)NORDIC_FICR_TRIMV;
#endif
}

/*---------------------------------------------------------------------------*/
/* Reset handler                                                             */
/*---------------------------------------------------------------------------*/

void tiku_nordic_reset_handler(void) __attribute__((naked, section(".text"), used));

/**
 * @brief nRF54L reset handler: C-runtime init and entry to main().
 *
 * SP comes from vector[0].  Masks IRQs, sets VTOR, enables the FPU, applies
 * errata, debug unlock and factory trims, copies .data, zeroes .bss (and
 * .ram2 on the LM20) and leaves .uninit, then calls main().
 *
 * @note Cortex-M resets with PRIMASK=0; IRQs stay masked until
 *       tiku_sched_loop() enables them, so an early-armed source such as
 *       SysTick cannot fire before the scheduler's queues exist.  Naked, so
 *       no compiler prologue touches uninitialised call-saved registers.
 */
void tiku_nordic_reset_handler(void)
{
    __asm__ volatile ("cpsid i" ::: "memory");

    /* VTOR resets to 0, the table's address; the explicit write keeps a
     * relocated table or a warm reboot pointing at this one. */
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
    tiku_nordic_debug_unlock();     /* keep the J-Link port open (TAMPC) */
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

#if defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
    /* Zero the .ram2 statics in the upper SRAM bank, [__ram2_start,
     * __ram2_end) from nrf54lm20a.ld.  The SRAM tier above them is not
     * zeroed. */
    {
        extern uint32_t __ram2_start;
        extern uint32_t __ram2_end;
        uint32_t *dst = &__ram2_start;
        while (dst < &__ram2_end) {
            *dst++ = 0U;
        }
    }
#endif

    /* .uninit is not touched: it carries warm-reset survivor state. */

    (void)main();

    for (;;) {
        __asm__ volatile ("wfe");
    }
}

/*---------------------------------------------------------------------------*/
/* Vector table                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Cortex-M33 vector table for the nRF54L parts.
 *
 * Placed in .vectors at the RRAM base.  Index 0 is the initial SP, 1..15 the
 * ARMv8-M system exceptions, 16.. the external IRQs (272 on nRF54L15, 290 on
 * nRF54LM20A); unused external slots hold nordic_default_handler.
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
     * (nrf54l15_application.h), not the vector-array position. */
    [16 +  76] = tiku_nordic_flpr_isr,         /* VPR00_IRQn      = 76  */
    [16 + 133] = tiku_nordic_timer10_isr,      /* TIMER10_IRQn    = 133 */
    [16 + 138] = tiku_nordic_radio_isr,        /* RADIO_0_IRQn    = 138 */
    [16 + 198] = tiku_nordic_uart_console_isr, /* SERIAL20_IRQn   = 198 */
    [16 + 202] = tiku_nordic_timer20_isr,      /* TIMER20_IRQn = 202 (htimer) */
    [16 + 218] = tiku_nordic_gpiote20_isr,     /* GPIOTE20_0 = 218 (P1/P2)    */
    [16 + 226] = tiku_nordic_grtc_isr,         /* GRTC_0_IRQn     = 226 */
    [16 + 260] = tiku_nordic_uart_console_isr, /* SERIAL30_IRQn   = 260 */
    [16 + 268] = tiku_nordic_gpiote30_isr,     /* GPIOTE30_0 = 268 (P0)       */

    /* Fill every remaining external slot with the default handler so no
     * slot dispatches through a NULL pointer.  Ranges are split around the
     * explicitly-wired IRQs above (no overlapping designated initializers). */
    [16 +   0 ... 16 +  75] = nordic_default_handler,
    [16 +  86] = tiku_nordic_axons_isr,        /* AXONS_IRQn = 86 (LM20B)     */
    [16 +  77 ... 16 +  85] = nordic_default_handler,
    [16 +  90] = tiku_nordic_usbhs_isr,        /* USBHS_IRQn      = 90        */
    [16 +  87 ... 16 +  89] = nordic_default_handler,
    [16 +  91 ... 16 + 132] = nordic_default_handler,
    [16 + 134 ... 16 + 137] = nordic_default_handler,
    [16 + 139 ... 16 + 197] = nordic_default_handler,
    [16 + 199 ... 16 + 201] = nordic_default_handler,
    [16 + 203 ... 16 + 217] = nordic_default_handler,
    [16 + 219 ... 16 + 225] = nordic_default_handler,
    [16 + 227 ... 16 + 259] = nordic_default_handler,
    [16 + 261 ... 16 + 267] = nordic_default_handler,
    /* Upper bound tracks the device IRQ count (271 on nRF54L15, 289 on the
     * nRF54LM20A) so every remaining external slot is filled. */
#if defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
    [16 + 289] = tiku_nordic_vregusb_isr,      /* VREGUSB_IRQn    = 289       */
    [16 + 269 ... 16 + 288] = nordic_default_handler,
#else
    [16 + 269 ... 16 + (NORDIC_NUM_EXT_IRQS - 1)] = nordic_default_handler,
#endif
};
