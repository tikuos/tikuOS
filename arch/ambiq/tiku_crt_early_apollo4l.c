/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early_apollo4l.c - Apollo4 (Cortex-M4F) startup.
 *
 * The vector table of the Apollo4 Lite and Plus, and the reset handler the
 * boot ROM starts from it.  The handler sets up the core, copies .data,
 * zeroes .bss, powers and zeroes the shared SRAM, then calls main.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "apollo4l.h"   /* PWRCTRL: shared-SRAM power enable */

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

/* The vector table, defined at the end of this file.  Apollo4 has 84
 * external IRQs (0..83; MAX_IRQn = 84 in apollo4l.h). */
typedef void (*ambiq_isr_t)(void);
#define AMBIQ_NUM_EXT_IRQS  84
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

/** @brief NMI handler -- weak alias to ambiq_default_handler */
void tiku_ambiq_nmi_handler(void)          __attribute__((weak, alias("ambiq_default_handler")));
/** @brief HardFault handler -- weak alias to ambiq_default_handler */
void tiku_ambiq_hard_fault_handler(void)   __attribute__((weak, alias("ambiq_default_handler")));
/** @brief MemManage fault handler -- weak alias to ambiq_default_handler */
void tiku_ambiq_mem_fault_handler(void)    __attribute__((weak, alias("ambiq_default_handler")));
/** @brief BusFault handler -- weak alias to ambiq_default_handler */
void tiku_ambiq_bus_fault_handler(void)    __attribute__((weak, alias("ambiq_default_handler")));
/** @brief UsageFault handler -- weak alias to ambiq_default_handler */
void tiku_ambiq_usage_fault_handler(void)  __attribute__((weak, alias("ambiq_default_handler")));
/** @brief SVC handler -- weak alias to ambiq_default_handler */
void tiku_ambiq_svc_handler(void)          __attribute__((weak, alias("ambiq_default_handler")));
/** @brief PendSV handler -- weak alias to ambiq_default_handler */
void tiku_ambiq_pendsv_handler(void)       __attribute__((weak, alias("ambiq_default_handler")));
/** @brief SysTick handler -- weak; tiku_timer_apollo4l.c defines it */
void tiku_ambiq_systick_handler(void)      __attribute__((weak, alias("ambiq_default_handler")));

/* Peripheral IRQs the arch drivers claim (apollo4l IRQ map). The driver that
 * handles each one provides a strong definition of the same symbol. */
/**
 * @brief Console UART ISR -- weak alias
 *
 * Serves UART0 (IRQ 15) with TIKU_CONSOLE_UART0 and UART2 (IRQ 17) otherwise.
 */
void tiku_ambiq_uart2_isr(void)            __attribute__((weak, alias("ambiq_default_handler")));
/** @brief STIMER Compare0 ISR (IRQ 32, htimer source) -- weak alias */
void tiku_ambiq_stimer_cmpr0_isr(void)     __attribute__((weak, alias("ambiq_default_handler")));
/** @brief STIMER Compare1 ISR (IRQ 33, kernel tick) -- weak alias */
void tiku_ambiq_stimer_cmpr1_isr(void)     __attribute__((weak, alias("ambiq_default_handler")));
/** @brief GPIO0 pins0-31 ISR (IRQ 56) -- weak alias */
void tiku_ambiq_gpio0_isr(void)            __attribute__((weak, alias("ambiq_default_handler")));

/*---------------------------------------------------------------------------*/
/* RESET HANDLER                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Reset handler: the first code to run after the boot ROM.
 *
 * Masks IRQs, sets SP and VTOR, enables the FPU, copies .data, zeroes .bss,
 * powers both shared SRAM banks and zeroes .ssram, then calls main().  If
 * main() returns it waits on WFE forever.
 */
void tiku_ambiq_reset_handler(void) __attribute__((naked, section(".text"), used));

void tiku_ambiq_reset_handler(void) {
    /* IRQs stay masked until the end of tiku_cpu_full_init()
     * (boot/tiku_boot.c), after kernel init has built the process queue. */
    __asm__ volatile ("cpsid i" ::: "memory");

    /* The boot ROM loads SP from vector[0]; this sets it again for an entry
     * that does not go through the vector table. */
    __asm__ volatile ("ldr sp, =__stack");

    /* Point VTOR at this table (512-aligned at MRAM origin 0x18000). */
    *(volatile uint32_t *)0xE000ED08U = (uint32_t)tiku_ambiq_vectors;

    /* Enable the FPU: CPACR grants full access to CP10/CP11, which the
     * -mfloat-abi=hard build needs before its first FP instruction. */
    *(volatile uint32_t *)0xE000ED88U |= (0xFU << 20);
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");

    /* Copy .data from its MRAM load address to TCM. */
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

    /* Power both shared SRAM banks even when .ssram is empty: the SRAM tier
     * span lies outside that section.  The wait is bounded, so a power FSM
     * that never reports ready does not hang the boot. */
    {
        PWRCTRL->SSRAMPWREN_b.PWRENSSRAM = 0x3u;   /* both 1 MB groups */
        {
            uint32_t guard = 1000000u;
            while ((PWRCTRL->SSRAMPWRST_b.SSRAMPWRST != 0x3u) && --guard) {
            }
        }
    }

    /* Zero the static buffers in shared SRAM (.ssram); a MINIMAL=1 build
     * has none. */
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
 * @brief Apollo4 interrupt vector table
 *
 * 16 system exceptions and 84 external IRQs, 100 entries, in .vectors at MRAM
 * 0x18000, which meets the 512-byte VTOR alignment.  The console UART, STIMER
 * and GPIO0 slots point at weak handlers; the rest at ambiq_default_handler.
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
    ambiq_default_handler,              /*  7  Reserved (no v8M SecureFault) */
    ambiq_default_handler,              /*  8  Reserved          */
    ambiq_default_handler,              /*  9  Reserved          */
    ambiq_default_handler,              /* 10  Reserved          */
    tiku_ambiq_svc_handler,             /* 11  SVC               */
    ambiq_default_handler,              /* 12  DebugMon          */
    ambiq_default_handler,              /* 13  Reserved          */
    tiku_ambiq_pendsv_handler,          /* 14  PendSV            */
    tiku_ambiq_systick_handler,         /* 15  SysTick           */

    /* External interrupts (apollo4l IRQn numbering) -------------------- */
    [16 + 32] = tiku_ambiq_stimer_cmpr0_isr, /* IRQ 32  STIMER Compare0  */
    [16 + 33] = tiku_ambiq_stimer_cmpr1_isr, /* IRQ 33  STIMER tick      */
    [16 + 56] = tiku_ambiq_gpio0_isr,        /* IRQ 56  GPIO0 pins0-31   */

    /* Everything else spins in the default handler. */
    [16 +  0 ... 16 + 16] = ambiq_default_handler,
    [16 + 18 ... 16 + 31] = ambiq_default_handler,
    [16 + 34 ... 16 + 55] = ambiq_default_handler,
    [16 + 57 ... 16 + AMBIQ_NUM_EXT_IRQS - 1] = ambiq_default_handler,

    /* The console UART slot comes after the default ranges: with
     * TIKU_CONSOLE_UART0 it is IRQ 15, inside the first range, and the later
     * initializer wins.  Referencing the ISR here also keeps it from
     * --gc-sections. */
#if defined(TIKU_CONSOLE_UART0)
    [16 + 15] = tiku_ambiq_uart2_isr,        /* IRQ 15  UART0 (Plus EVB) */
#else
    [16 + 17] = tiku_ambiq_uart2_isr,        /* IRQ 17  UART2 (Lite EVB) */
#endif
};
