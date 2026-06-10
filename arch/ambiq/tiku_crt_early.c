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
 *   5. SystemInit() (CMSIS: TrustZone SAU + SystemCoreClock).  @ambiq-sdk
 *   6. main().
 *
 * The only AmbiqSuite dependency here is SystemInit() (a single source
 * file, CMSIS/AmbiqMicro/Source/system_apollo510.c); everything else is
 * bare-metal register access. @ambiq-sdk marks it for the de-SDK pass.
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
extern uint32_t __stack;

/*---------------------------------------------------------------------------*/
/* External entry points                                                     */
/*---------------------------------------------------------------------------*/

extern int  main(void);
extern void SystemInit(void);   /* @ambiq-sdk: CMSIS system_apollo510.c */

/* Forward decl of the vector table (defined below). Apollo510 has 135
 * external IRQs (0..134, see AmbiqSuite startup_gcc.c). */
typedef void (*ambiq_isr_t)(void);
#define AMBIQ_NUM_EXT_IRQS  135
extern const ambiq_isr_t tiku_ambiq_vectors[16 + AMBIQ_NUM_EXT_IRQS];

/*---------------------------------------------------------------------------*/
/* Default + weak handlers (override with a same-named non-weak symbol)       */
/*---------------------------------------------------------------------------*/

/* Spin on WFE so a debugger halt lands on something recognisable. */
static void ambiq_default_handler(void) {
    while (1) {
        __asm__ volatile ("wfe");
    }
}

void tiku_ambiq_nmi_handler(void)          __attribute__((weak, alias("ambiq_default_handler")));
void tiku_ambiq_hard_fault_handler(void)   __attribute__((weak, alias("ambiq_default_handler")));
void tiku_ambiq_mem_fault_handler(void)    __attribute__((weak, alias("ambiq_default_handler")));
void tiku_ambiq_bus_fault_handler(void)    __attribute__((weak, alias("ambiq_default_handler")));
void tiku_ambiq_usage_fault_handler(void)  __attribute__((weak, alias("ambiq_default_handler")));
void tiku_ambiq_secure_fault_handler(void) __attribute__((weak, alias("ambiq_default_handler")));
void tiku_ambiq_svc_handler(void)          __attribute__((weak, alias("ambiq_default_handler")));
void tiku_ambiq_pendsv_handler(void)       __attribute__((weak, alias("ambiq_default_handler")));

/* SysTick is the system tick; tiku_timer_arch.c provides the real one. */
void tiku_ambiq_systick_handler(void)      __attribute__((weak, alias("ambiq_default_handler")));

/* Peripheral IRQs tikuOS drivers may claim later (UART console, STIMER /
 * TIMER for the htimer, GPIO0 for edge IRQs). Weak now; the arch driver
 * that handles each one provides a strong definition of the same symbol. */
void tiku_ambiq_uart0_isr(void)            __attribute__((weak, alias("ambiq_default_handler")));
void tiku_ambiq_stimer_cmpr0_isr(void)     __attribute__((weak, alias("ambiq_default_handler")));
void tiku_ambiq_gpio0_isr(void)            __attribute__((weak, alias("ambiq_default_handler")));
void tiku_ambiq_timer0_isr(void)           __attribute__((weak, alias("ambiq_default_handler")));

/*---------------------------------------------------------------------------*/
/* Reset handler                                                             */
/*---------------------------------------------------------------------------*/

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
    __asm__ volatile ("dsb");
    __asm__ volatile ("isb");

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

    /* CMSIS system init: TrustZone SAU setup + SystemCoreClock. */
    SystemInit();   /* @ambiq-sdk */

    (void)main();

    /* Should never return. */
    while (1) {
        __asm__ volatile ("wfe");
    }
}

/*---------------------------------------------------------------------------*/
/* Vector table                                                              */
/*---------------------------------------------------------------------------*/

/*
 * 16 system exceptions + 135 external IRQs = 151 entries. Lives in the
 * .vectors section, which the linker places at MRAM origin (0x410000),
 * naturally satisfying the M55 VTOR alignment requirement. Peripheral
 * slots default to ambiq_default_handler; the four named slots point at
 * weak symbols that tikuOS arch drivers override.
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
    [16 + 15] = tiku_ambiq_uart0_isr,        /* IRQ 15  UART0            */
    [16 + 32] = tiku_ambiq_stimer_cmpr0_isr, /* IRQ 32  STIMER Compare0  */
    [16 + 56] = tiku_ambiq_gpio0_isr,        /* IRQ 56  GPIO N0 pins0-31 */
    [16 + 67] = tiku_ambiq_timer0_isr,       /* IRQ 67  TIMER0           */

    /* Everything else spins in the default handler (NULL would hard-fault
     * if dispatched). Ranges chosen to skip the four named slots above. */
    [16 +  0 ... 16 + 14] = ambiq_default_handler,
    [16 + 16 ... 16 + 31] = ambiq_default_handler,
    [16 + 33 ... 16 + 55] = ambiq_default_handler,
    [16 + 57 ... 16 + 66] = ambiq_default_handler,
    [16 + 68 ... 16 + 134] = ambiq_default_handler,
};
