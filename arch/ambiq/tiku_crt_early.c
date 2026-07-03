/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - Apollo 510 (Cortex-M55) startup
 *
 * Modelled on arch/arm-rp2350/tiku_crt_early.c but with the M55/Apollo
 * specifics from AmbiqSuite's startup_gcc.c. Unlike RP2350 there is NO
 * .boot2 / .image_def header: the on-silicon Secure Boot Loader transfers
 * control straight to the vector table at MRAM 0x410000.
 *
 * Reset flow:
 *   1. Mask IRQs (the scheduler re-enables them in tiku_sched_loop()).
 *   2. Set SP and VTOR explicitly.
 *   3. Enable the FPU (CPACR) — the build uses the hard-float ABI.
 *   4. Copy .data (MRAM->DTCM), zero .bss.
 *   5. M55 finishing touches (the functional bits of CMSIS SystemInit):
 *      EPU (FP/MVE) power state + Low-Overhead-Branch enable.
 *   6. main().
 *
 * Fully bare-metal: no AmbiqSuite dependency. CMSIS SystemInit() is gone —
 * its VTOR/FPU were already done here, its TrustZone SAU setup is compiled
 * out in our non-cmse build, its SystemCoreClock stub is unused (we read the
 * perf-mode register), and the remaining two register writes are in step 5.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "apollo510.h"   /* PWRCTRL (shared-SRAM power-enable) */

/*---------------------------------------------------------------------------*/
/* Linker-script symbols                                                     */
/*---------------------------------------------------------------------------*/

extern uint32_t __data_load;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern uint32_t __ssram_start;
extern uint32_t __ssram_end;
extern uint32_t __stack;

/*---------------------------------------------------------------------------*/
/* External entry points                                                     */
/*---------------------------------------------------------------------------*/

extern int  main(void);

/* Forward decl of the vector table (defined below). Apollo510 has 135
 * external IRQs (0..134, see AmbiqSuite startup_gcc.c). */
typedef void (*ambiq_isr_t)(void);
#define AMBIQ_NUM_EXT_IRQS  135
extern const ambiq_isr_t tiku_ambiq_vectors[16 + AMBIQ_NUM_EXT_IRQS];

/*---------------------------------------------------------------------------*/
/* Default + weak handlers (override with a same-named non-weak symbol)       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default (catch-all) exception and IRQ handler
 *
 * Spins on WFE so a debugger halt lands on something recognisable
 * rather than hard-faulting into an unknown location. All vector
 * table slots that tikuOS has not claimed are aliased to this via
 * weak attributes; override by providing a non-weak definition of
 * the corresponding named handler symbol.
 */
static void ambiq_default_handler(void) {
    while (1) {
        __asm__ volatile ("wfe");
    }
}

/** @brief NMI handler — weak alias to ambiq_default_handler */
void tiku_ambiq_nmi_handler(void)          __attribute__((weak, alias("ambiq_default_handler")));
/** @brief HardFault handler — weak alias to ambiq_default_handler */
void tiku_ambiq_hard_fault_handler(void)   __attribute__((weak, alias("ambiq_default_handler")));
/** @brief MemManage fault handler — weak alias to ambiq_default_handler */
void tiku_ambiq_mem_fault_handler(void)    __attribute__((weak, alias("ambiq_default_handler")));
/** @brief BusFault handler — weak alias to ambiq_default_handler */
void tiku_ambiq_bus_fault_handler(void)    __attribute__((weak, alias("ambiq_default_handler")));
/** @brief UsageFault handler — weak alias to ambiq_default_handler */
void tiku_ambiq_usage_fault_handler(void)  __attribute__((weak, alias("ambiq_default_handler")));
/** @brief SecureFault (ARMv8-M) handler — weak alias to ambiq_default_handler */
void tiku_ambiq_secure_fault_handler(void) __attribute__((weak, alias("ambiq_default_handler")));
/** @brief SVC handler — weak alias to ambiq_default_handler */
void tiku_ambiq_svc_handler(void)          __attribute__((weak, alias("ambiq_default_handler")));
/** @brief PendSV handler — weak alias to ambiq_default_handler */
void tiku_ambiq_pendsv_handler(void)       __attribute__((weak, alias("ambiq_default_handler")));

/** @brief SysTick handler — weak; tiku_timer_arch.c provides the real one */
void tiku_ambiq_systick_handler(void)      __attribute__((weak, alias("ambiq_default_handler")));

/**
 * @brief UART0 ISR — weak alias to ambiq_default_handler
 *
 * Peripheral IRQs tikuOS drivers may claim later (UART console, STIMER /
 * TIMER for the htimer, GPIO0 for edge IRQs). The arch driver that handles
 * each one provides a strong definition of the same symbol.
 */
void tiku_ambiq_uart0_isr(void)            __attribute__((weak, alias("ambiq_default_handler")));
/** @brief STIMER Compare0 ISR (htimer source) — weak alias */
void tiku_ambiq_stimer_cmpr0_isr(void)     __attribute__((weak, alias("ambiq_default_handler")));
/** @brief STIMER Compare1 ISR (kernel tick, IRQ 33) — weak alias */
void tiku_ambiq_stimer_cmpr1_isr(void)     __attribute__((weak, alias("ambiq_default_handler")));
/** @brief GPIO N0 ISR (pins 0-31) — weak alias */
void tiku_ambiq_gpio0_isr(void)            __attribute__((weak, alias("ambiq_default_handler")));
/** @brief TIMER0 ISR — weak alias */
void tiku_ambiq_timer0_isr(void)           __attribute__((weak, alias("ambiq_default_handler")));

/*---------------------------------------------------------------------------*/
/* Reset handler                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Apollo510 (Cortex-M55) reset handler — bare-metal startup entry
 *
 * Executed immediately after the SBL transfers control. Performs:
 *   1. Mask maskable IRQs (CPSID i) — prevents spurious interrupts
 *      during kernel init before the scheduler queue is built.
 *   2. Set SP from __stack linker symbol — robust to alternate entries.
 *   3. Set VTOR to tiku_ambiq_vectors (1024-aligned at MRAM 0x410000).
 *   4. Enable FPU (CPACR CP10/CP11) — required by -mfloat-abi=hard.
 *   5. Set M55 EPU power state = ON/clock-off (PWRMODCTL.CPDLPSTATE
 *      ELPSTATE[5:4] = 0b01) and enable ARMv8.1-M LOB (SCB.CCR bit 19).
 *   6. Power up the 3 MB shared SRAM (PWRCTRL.SSRAMPWREN, three groups)
 *      with a bounded wait — .ssram tier buffers live there.
 *   7. Copy .data MRAM->DTCM, zero .bss, zero .ssram.
 *   8. Call main(); hang on WFE if main() returns.
 *
 * Fully bare-metal: no AmbiqSuite dependency. CMSIS SystemInit() is
 * not called (its work is done inline above).
 */
void tiku_ambiq_reset_handler(void) __attribute__((naked, section(".text"), used));

void tiku_ambiq_reset_handler(void) {
    /* Mask maskable IRQs immediately. Cortex-M resets with PRIMASK = 0;
     * something that programs an IRQ source during kernel init (e.g.
     * SysTick.TICKINT in tiku_clock_arch_init()) would otherwise fire
     * before tiku_sched_init() builds the process queue. The scheduler
     * re-enables IRQs at the top of tiku_sched_loop(). */
    __asm__ volatile ("cpsid i" ::: "memory");

    /* The SBL loads SP from vector[0], but set it explicitly so this
     * handler is robust to alternate entry paths. */
    __asm__ volatile ("ldr sp, =__stack");

    /* Point VTOR at our table (the table address is 1024-aligned because
     * it sits at MRAM origin 0x410000). */
    *(volatile uint32_t *)0xE000ED08U = (uint32_t)tiku_ambiq_vectors;

    /* Enable the FPU: CPACR grants full access to CP10/CP11. Required
     * because the build uses -mfloat-abi=hard and libam_hal is built the
     * same way. */
    *(volatile uint32_t *)0xE000ED88U |= (0xFU << 20);

    /* The two functional bits CMSIS SystemInit() set that the steps above do
     * not: the M55 EPU (FP/MVE unit) power state = ON, clock-off (best FP/MVE
     * performance, avoids power-up stalls) via PWRMODCTL.CPDLPSTATE
     * ELPSTATE[5:4] = 0b01; and the ARMv8.1-M Low-Overhead-Branch extension
     * via SCB.CCR.LOB (bit 19), used by -mcpu=cortex-m55 loop instructions. */
    {
        volatile uint32_t *cpdlpstate = (volatile uint32_t *)0xE001E300U; /* PWRMODCTL */
        *cpdlpstate = (*cpdlpstate & ~(0x3U << 4)) | (0x1U << 4);
    }
    *(volatile uint32_t *)0xE000ED14U |= (1U << 19);   /* SCB->CCR, LOB */
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");

    /* Power up the 3 MB shared SRAM (three 1 MB groups). The SBL leaves it
     * off; the large volatile tier buffers (.ssram) live there. Bounded wait so
     * a stuck power FSM can't hang the boot. The cache is still off here, so
     * this and the later .ssram zero-init reach the SSRAM directly. */
    PWRCTRL->SSRAMPWREN_b.PWRENSSRAM = 0x7u;
    {
        uint32_t guard = 1000000u;
        while ((PWRCTRL->SSRAMPWRST_b.SSRAMPWRST != 0x7u) && --guard) {
        }
    }

    /* Copy .data from its MRAM load address to DTCM. */
    uint32_t *src = &__data_load;
    uint32_t *dst = &__data_start;
    while (dst < &__data_end) {
        *dst++ = *src++;
    }

    /* Zero .bss. (.uninit is intentionally left untouched.) */
    dst = &__bss_start;
    while (dst < &__bss_end) {
        *dst++ = 0U;
    }

    /* Zero the SSRAM-resident static buffers (.ssram). Separate loop because
     * they live in the just-powered SSRAM bank, not in DTCM .bss. */
    dst = &__ssram_start;
    while (dst < &__ssram_end) {
        *dst++ = 0U;
    }

    (void)main();

    /* Should never return. */
    while (1) {
        __asm__ volatile ("wfe");
    }
}

/*---------------------------------------------------------------------------*/
/* Vector table                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Apollo510 interrupt vector table
 *
 * 16 system exceptions + 135 external IRQs = 151 entries. Placed in
 * .vectors by the linker at MRAM origin (0x410000), naturally satisfying
 * the M55 VTOR 1024-byte alignment requirement. Peripheral slots
 * default to ambiq_default_handler; the four named driver slots point
 * at weak symbols that tikuOS arch drivers override with strong
 * definitions.
 */
const ambiq_isr_t tiku_ambiq_vectors[16 + AMBIQ_NUM_EXT_IRQS]
__attribute__((section(".vectors"), used)) = {
    /* System exceptions ------------------------------------------------ */
    (ambiq_isr_t)(&__stack),            /*  0  Initial SP        */
    tiku_ambiq_reset_handler,           /*  1  Reset             */
    tiku_ambiq_nmi_handler,             /*  2  NMI               */
    tiku_ambiq_hard_fault_handler,      /*  3  HardFault         */
    tiku_ambiq_mem_fault_handler,       /*  4  MemManage         */
    tiku_ambiq_bus_fault_handler,       /*  5  BusFault          */
    tiku_ambiq_usage_fault_handler,     /*  6  UsageFault        */
    tiku_ambiq_secure_fault_handler,    /*  7  SecureFault (v8M) */
    ambiq_default_handler,              /*  8  Reserved          */
    ambiq_default_handler,              /*  9  Reserved          */
    ambiq_default_handler,              /* 10  Reserved          */
    tiku_ambiq_svc_handler,             /* 11  SVC               */
    ambiq_default_handler,              /* 12  DebugMon          */
    ambiq_default_handler,              /* 13  Reserved          */
    tiku_ambiq_pendsv_handler,          /* 14  PendSV            */
    tiku_ambiq_systick_handler,         /* 15  SysTick           */

    /* External interrupts (AmbiqSuite startup_gcc.c numbering) --------- */
    /* Console UART: UART0 (IRQ 15) on the base Apollo510 EVB, or UART1 (IRQ 16)
     * on the Apollo510 Blue EVB (apollo510b) -- gated on TIKU_CONSOLE_UART1. */
#if defined(TIKU_CONSOLE_UART1)
    [16 + 16] = tiku_ambiq_uart0_isr,        /* IRQ 16  UART1 (Blue EVB) */
#else
    [16 + 15] = tiku_ambiq_uart0_isr,        /* IRQ 15  UART0            */
#endif
    [16 + 32] = tiku_ambiq_stimer_cmpr0_isr, /* IRQ 32  STIMER Compare0  */
    [16 + 33] = tiku_ambiq_stimer_cmpr1_isr, /* IRQ 33  STIMER Compare1 (tick) */
    [16 + 56] = tiku_ambiq_gpio0_isr,        /* IRQ 56  GPIO N0 pins0-31 */
    [16 + 67] = tiku_ambiq_timer0_isr,       /* IRQ 67  TIMER0           */

    /* Everything else spins in the default handler (NULL would hard-fault
     * if dispatched). Ranges chosen to skip the named slots above -- including
     * the console UART, whose slot moves with TIKU_CONSOLE_UART1 (IRQ 16 on the
     * Blue EVB, IRQ 15 otherwise). The range MUST skip whichever slot the UART
     * override set, or (being a later initializer) it would clobber it. */
#if defined(TIKU_CONSOLE_UART1)
    [16 +  0 ... 16 + 15] = ambiq_default_handler,   /* skip IRQ 16 (UART1) */
    [16 + 17 ... 16 + 31] = ambiq_default_handler,
#else
    [16 +  0 ... 16 + 14] = ambiq_default_handler,   /* skip IRQ 15 (UART0) */
    [16 + 16 ... 16 + 31] = ambiq_default_handler,
#endif
    [16 + 34 ... 16 + 55] = ambiq_default_handler,
    [16 + 57 ... 16 + 66] = ambiq_default_handler,
    [16 + 68 ... 16 + 134] = ambiq_default_handler,
};
