/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - Apollo510 (Cortex-M55) startup.
 *
 * The vector table, and the reset handler the secure bootloader starts from
 * it.  The handler sets up the core and the shared SRAM, copies .data,
 * zeroes .bss and .ssram, then calls main.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "apollo510.h"   /* PWRCTRL (shared-SRAM power-enable) */

/**
 * @brief EPU (FP/MVE) state in low-power modes: PWRMODCTL ELPSTATE[5:4].
 *
 * 1 keeps the unit on with its clock stopped (the CMSIS SystemInit choice);
 * 2, the default, retains its state with the unit powered down.
 *
 * @note 3 (off) fails the build: it discards FP/MVE register state, which
 *       this hard-float kernel keeps across sleeps.
 */
#ifndef TIKU_AMBIQ_ELP_STATE
#define TIKU_AMBIQ_ELP_STATE 2u
#endif
_Static_assert(TIKU_AMBIQ_ELP_STATE <= 2u,
               "TIKU_AMBIQ_ELP_STATE=3 (EPU OFF) discards FP/MVE register state "
               "and this build is hard-float -- it boot-loops the board");

/*---------------------------------------------------------------------------*/
/* LINKER-SCRIPT SYMBOLS                                                     */
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
/* EXTERNAL ENTRY POINTS                                                     */
/*---------------------------------------------------------------------------*/

extern int  main(void);

/* The vector table, defined at the end of this file.  Apollo510 has 135
 * external IRQs (0..134, as numbered in AmbiqSuite startup_gcc.c). */
typedef void (*ambiq_isr_t)(void);
#define AMBIQ_NUM_EXT_IRQS  135
extern const ambiq_isr_t tiku_ambiq_vectors[16 + AMBIQ_NUM_EXT_IRQS];

/*---------------------------------------------------------------------------*/
/* DEFAULT AND WEAK HANDLERS                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Catch-all exception and IRQ handler: waits on WFE forever.
 *
 * Every vector slot without a driver points here, directly or through a weak
 * alias; a driver takes a slot by defining the named handler strongly.
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
/** @brief SecureFault handler — weak alias to ambiq_default_handler */
void tiku_ambiq_secure_fault_handler(void) __attribute__((weak, alias("ambiq_default_handler")));
/** @brief SVC handler — weak alias to ambiq_default_handler */
void tiku_ambiq_svc_handler(void)          __attribute__((weak, alias("ambiq_default_handler")));
/** @brief PendSV handler — weak alias to ambiq_default_handler */
void tiku_ambiq_pendsv_handler(void)       __attribute__((weak, alias("ambiq_default_handler")));

/** @brief SysTick handler — weak; tiku_timer_arch.c defines it */
void tiku_ambiq_systick_handler(void)      __attribute__((weak, alias("ambiq_default_handler")));

/**
 * @brief Console UART ISR — weak alias to ambiq_default_handler
 *
 * Serves UART1 with TIKU_CONSOLE_UART1 and UART0 otherwise.  Like the
 * peripheral handlers below, the driver that owns the IRQ defines it.
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
/** @brief GPU ISR (IRQ 28) — weak; tiku_gpu_arch.c defines it */
void tiku_ambiq_gpu_isr(void)              __attribute__((weak, alias("ambiq_default_handler")));
/** @brief USB device ISR (IRQ 27) — weak; tiku_usb_arch.c defines it */
void tiku_ambiq_usb_isr(void)              __attribute__((weak, alias("ambiq_default_handler")));

/*---------------------------------------------------------------------------*/
/* RESET HANDLER                                                             */
/*---------------------------------------------------------------------------*/

/*
 * Reset handler: the first code to run after the secure bootloader.
 *   1. Mask maskable IRQs (CPSID i) until kernel init has built the
 *      scheduler queue.
 *   2. Set SP from the __stack linker symbol.
 *   3. Set VTOR to tiku_ambiq_vectors (1024-aligned at MRAM 0x410000).
 *   4. Enable the FPU (CPACR CP10/CP11), which -mfloat-abi=hard needs.
 *   5. Set the EPU (FP/MVE) sleep state, PWRMODCTL.CPDLPSTATE ELPSTATE[5:4],
 *      from TIKU_AMBIQ_ELP_STATE, and enable ARMv8.1-M LOB (SCB.CCR bit 19).
 *   6. Power up the 3 MB shared SRAM (PWRCTRL.SSRAMPWREN, three groups)
 *      with a bounded wait; the .ssram buffers live there.
 *   7. Copy .data MRAM->DTCM, zero .bss, zero .ssram.
 *   8. Call main(); wait on WFE forever if main() returns.
 *
 * CMSIS SystemInit() is not called; steps 4 and 5 cover the part of its work
 * this port needs.
 */
void tiku_ambiq_reset_handler(void) __attribute__((naked, section(".text"), used));

void tiku_ambiq_reset_handler(void) {
    /* Cortex-M resets with PRIMASK = 0, and kernel init arms IRQ sources
     * (the STIMER tick in tiku_clock_arch_init()) before tiku_sched_init()
     * builds the process queue.  IRQs stay masked until the end of
     * tiku_cpu_full_init() (boot/tiku_boot.c). */
    __asm__ volatile ("cpsid i" ::: "memory");

    /* The SBL loads SP from vector[0]; this sets it again for an entry that
     * does not go through the vector table. */
    __asm__ volatile ("ldr sp, =__stack");

    /* Point VTOR at this table (the table address is 1024-aligned because
     * it sits at MRAM origin 0x410000). */
    *(volatile uint32_t *)0xE000ED08U = (uint32_t)tiku_ambiq_vectors;

    /* Enable the FPU: CPACR grants full access to CP10/CP11, which the
     * -mfloat-abi=hard build needs before its first FP instruction. */
    *(volatile uint32_t *)0xE000ED88U |= (0xFU << 20);

    /* The EPU (FP/MVE) sleep state and the ARMv8.1-M low-overhead-branch
     * extension (SCB.CCR.LOB, used by the M55's loop instructions).
     * ELPSTATE sets what the FP/MVE unit does when the core enters a
     * low-power state: 1 keeps it on with the clock stopped, which avoids a
     * power-up stall on wake; 2, the default, powers it down with its state
     * retained, which lowers idle current.  3 (off) discards FP/MVE
     * registers that the hard-float kernel holds across sleeps; the
     * _Static_assert above refuses it, and so does `power cpdlp elp 3`. */
    {
        /* PWRMODCTL.CPDLPSTATE */
        volatile uint32_t *cpdlpstate = (volatile uint32_t *)0xE001E300U;
        *cpdlpstate = (*cpdlpstate & ~(0x3U << 4)) |
                      ((uint32_t)TIKU_AMBIQ_ELP_STATE << 4);
    }
    *(volatile uint32_t *)0xE000ED14U |= (1U << 19);   /* SCB->CCR, LOB */
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");

    /* Power up the 3 MB shared SRAM (three 1 MB groups), which the SBL
     * leaves off; the .ssram buffers live there.  The wait is bounded, so a
     * power FSM that never reports ready does not hang the boot.  The
     * D-cache is still off, so the .ssram zeroing below reaches the SSRAM
     * directly. */
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

    /* Zero .bss.  .uninit is left for tiku_mem_arch_init() to restore. */
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

    /* main() does not return; if it does, wait here. */
    while (1) {
        __asm__ volatile ("wfe");
    }
}

/*---------------------------------------------------------------------------*/
/* VECTOR TABLE                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Apollo510 interrupt vector table
 *
 * 16 system exceptions and 135 external IRQs, 151 entries, in .vectors at
 * MRAM 0x410000, which meets the M55's 1024-byte VTOR alignment.  Named
 * peripheral slots point at weak handlers; the rest at ambiq_default_handler.
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
    /* Console UART: UART1 (IRQ 16) with TIKU_CONSOLE_UART1, which the Blue
     * EVB build sets, and UART0 (IRQ 15) otherwise. */
#if defined(TIKU_CONSOLE_UART1)
    [16 + 16] = tiku_ambiq_uart0_isr,        /* IRQ 16  UART1 (Blue EVB) */
#else
    [16 + 15] = tiku_ambiq_uart0_isr,        /* IRQ 15  UART0            */
#endif
    [16 + 27] = tiku_ambiq_usb_isr,          /* IRQ 27  USB0 device      */
    [16 + 28] = tiku_ambiq_gpu_isr,          /* IRQ 28  GPU (Nema)       */
    [16 + 32] = tiku_ambiq_stimer_cmpr0_isr, /* IRQ 32  STIMER Compare0  */
    [16 + 33] = tiku_ambiq_stimer_cmpr1_isr, /* IRQ 33  STIMER tick      */
    [16 + 56] = tiku_ambiq_gpio0_isr,        /* IRQ 56  GPIO N0 pins0-31 */
    [16 + 67] = tiku_ambiq_timer0_isr,       /* IRQ 67  TIMER0           */

    /* Every other slot gets the default handler; a NULL slot hard-faults
     * when dispatched.  The ranges skip every named slot above, including
     * the console UART slot that TIKU_CONSOLE_UART1 moves, because a later
     * initializer overwrites an earlier one. */
#if defined(TIKU_CONSOLE_UART1)
    [16 +  0 ... 16 + 15] = ambiq_default_handler,   /* skip IRQ 16 (UART1) */
    [16 + 17 ... 16 + 26] = ambiq_default_handler,   /* skip IRQ 27 (USB0)  */
    [16 + 29 ... 16 + 31] = ambiq_default_handler,   /* skip IRQ 28 (GPU)   */
#else
    [16 +  0 ... 16 + 14] = ambiq_default_handler,   /* skip IRQ 15 (UART0) */
    [16 + 16 ... 16 + 26] = ambiq_default_handler,   /* skip IRQ 27 (USB0)  */
    [16 + 29 ... 16 + 31] = ambiq_default_handler,   /* skip IRQ 28 (GPU)   */
#endif
    [16 + 34 ... 16 + 55] = ambiq_default_handler,
    [16 + 57 ... 16 + 66] = ambiq_default_handler,
    [16 + 68 ... 16 + 134] = ambiq_default_handler,
};
