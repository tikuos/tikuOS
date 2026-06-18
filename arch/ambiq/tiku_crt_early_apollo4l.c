/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early_apollo4l.c - Apollo4 Lite (Cortex-M4F) startup
 *
 * Mirrors arch/ambiq/tiku_crt_early.c (Apollo510 / Cortex-M55) but for the
 * Cortex-M4: ARMv7E-M, single-precision FPU, no Helium, no Low-Overhead-Branch,
 * no EPU power-state register, and 84 external IRQs (vs 135). Apollo4 Lite is
 * not a secure part, so the boot ROM hands control straight to the vector table
 * at user MRAM 0x00018000 the standard Cortex-M way.
 *
 * Reset flow:
 *   1. Mask IRQs (the scheduler re-enables them in tiku_sched_loop()).
 *   2. Set SP and VTOR explicitly.
 *   3. Enable the FPU (CPACR CP10/CP11) -- the build uses the hard-float ABI.
 *   4. Copy .data (MRAM->TCM), zero .bss, zero .ssram.
 *   5. main().
 *
 * Fully bare-metal: no AmbiqSuite dependency.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

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

/* Forward decl of the vector table (defined below). Apollo4 Lite has 84
 * external IRQs (0..83, see apollo4l.h IRQn_Type, MAX_IRQn = 84). */
typedef void (*ambiq_isr_t)(void);
#define AMBIQ_NUM_EXT_IRQS  84
extern const ambiq_isr_t tiku_ambiq_vectors[16 + AMBIQ_NUM_EXT_IRQS];

/*---------------------------------------------------------------------------*/
/* Default + weak handlers (override with a same-named non-weak symbol)       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Default (catch-all) exception and IRQ handler -- spins on WFE so a
 *        debugger halt lands somewhere recognisable.
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
/** @brief SysTick handler -- weak; the timer arch provides the real one */
void tiku_ambiq_systick_handler(void)      __attribute__((weak, alias("ambiq_default_handler")));

/* Peripheral IRQs the arch drivers claim (apollo4l IRQ map). The driver that
 * handles each one provides a strong definition of the same symbol. */
/** @brief UART2 console ISR (IRQ 17) -- weak alias */
void tiku_ambiq_uart2_isr(void)            __attribute__((weak, alias("ambiq_default_handler")));
/** @brief STIMER Compare0 ISR (IRQ 32, htimer source) -- weak alias */
void tiku_ambiq_stimer_cmpr0_isr(void)     __attribute__((weak, alias("ambiq_default_handler")));
/** @brief GPIO0 pins0-31 ISR (IRQ 56) -- weak alias */
void tiku_ambiq_gpio0_isr(void)            __attribute__((weak, alias("ambiq_default_handler")));

/*---------------------------------------------------------------------------*/
/* Reset handler                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Apollo4 Lite (Cortex-M4F) reset handler -- bare-metal startup entry
 *
 * Executed immediately after the boot ROM transfers control. Unlike the
 * Cortex-M55 startup, there is no EPU power-state, no Low-Overhead-Branch, and
 * no SecureFault. The shared SRAM is left untouched (the minimal/smoke build
 * keeps .ssram empty; the full-kernel build powers it when the tier needs it).
 */
void tiku_ambiq_reset_handler(void) __attribute__((naked, section(".text"), used));

void tiku_ambiq_reset_handler(void) {
    /* Mask maskable IRQs immediately; the scheduler re-enables them at the top
     * of tiku_sched_loop(). */
    __asm__ volatile ("cpsid i" ::: "memory");

    /* Set SP explicitly so this handler is robust to alternate entry paths. */
    __asm__ volatile ("ldr sp, =__stack");

    /* Point VTOR at our table (512-aligned at MRAM origin 0x18000). */
    *(volatile uint32_t *)0xE000ED08U = (uint32_t)tiku_ambiq_vectors;

    /* Enable the FPU: CPACR grants full access to CP10/CP11. Required because
     * the build uses -mfloat-abi=hard. */
    *(volatile uint32_t *)0xE000ED88U |= (0xFU << 20);
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");

    /* Copy .data from its MRAM load address to TCM. */
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

    /* Zero the SSRAM-resident static buffers (.ssram). Empty in the minimal
     * build (loop is a no-op); the full-kernel build powers the SRAM first. */
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
 * @brief Apollo4 Lite interrupt vector table
 *
 * 16 system exceptions + 84 external IRQs = 100 entries. Placed in .vectors by
 * the linker at MRAM origin (0x18000), satisfying the 512-byte VTOR alignment.
 * All external IRQ slots default to ambiq_default_handler at this milestone;
 * the full-kernel build adds named driver slots (UART2, STIMER, GPIO) with the
 * apollo4l IRQ numbers.
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
    [16 + 17] = tiku_ambiq_uart2_isr,        /* IRQ 17  UART2            */
    [16 + 32] = tiku_ambiq_stimer_cmpr0_isr, /* IRQ 32  STIMER Compare0  */
    [16 + 56] = tiku_ambiq_gpio0_isr,        /* IRQ 56  GPIO0 pins0-31   */

    /* Everything else spins in the default handler; ranges skip the named slots. */
    [16 +  0 ... 16 + 16] = ambiq_default_handler,
    [16 + 18 ... 16 + 31] = ambiq_default_handler,
    [16 + 33 ... 16 + 55] = ambiq_default_handler,
    [16 + 57 ... 16 + AMBIQ_NUM_EXT_IRQS - 1] = ambiq_default_handler,
};
