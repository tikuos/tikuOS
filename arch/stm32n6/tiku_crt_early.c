/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - STM32N6 (Cortex-M55) startup.
 *
 * A vector table at the image base, which the boot ROM reads for the initial
 * SP and entry point, and a reset handler that runs .data/.bss and calls main.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "tiku_stm32n6_regs.h"
#include "tiku_sram_arch.h"
#include "tiku_cache_arch.h"
#include "tiku_fault_arch.h"

/* Shorthand for filling the external-IRQ span with the default handler. */
#define DFL     stm32n6_default_handler
#define DFL4    DFL, DFL, DFL, DFL
#define DFL16   DFL4, DFL4, DFL4, DFL4

/* Linker-script symbols. */
extern uint32_t __data_load;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern uint32_t __stack;
extern uint32_t __axisram_start;
extern uint32_t __axisram_end;

extern int main(void);

typedef void (*stm32n6_isr_t)(void);

/* Vector table size drives its own alignment: 176 entries is 704 bytes, so the
 * table is aligned to 1024, which VTOR requires and the load address already
 * satisfies. */
#define STM32N6_NUM_EXT_IRQS    160

/**
 * @brief Default handler: park the core on an unhandled exception.
 *
 * Spinning on WFE keeps the core quiet and lands a debugger halt on a
 * recognisable PC instead of a random instruction stream.
 */
static void stm32n6_default_handler(void) {
    while (1) {
        __asm__ volatile ("wfe");
    }
}

/**
 * @defgroup stm32n6_exception_stubs Cortex-M55 weak exception stubs
 * @brief Weak aliases resolving to stm32n6_default_handler.
 *
 * A non-weak definition of the same name anywhere in the kernel or a driver
 * replaces the stub, because the table references these symbols by name.
 */
void tiku_stm32n6_nmi_handler(void)         __attribute__((weak, alias("stm32n6_default_handler")));
void tiku_stm32n6_hard_fault_handler(void)  __attribute__((weak, alias("stm32n6_default_handler")));
void tiku_stm32n6_mem_fault_handler(void)   __attribute__((weak, alias("stm32n6_default_handler")));
void tiku_stm32n6_bus_fault_handler(void)   __attribute__((weak, alias("stm32n6_default_handler")));
void tiku_stm32n6_usage_fault_handler(void) __attribute__((weak, alias("stm32n6_default_handler")));
void tiku_stm32n6_secure_fault_handler(void) __attribute__((weak, alias("stm32n6_default_handler")));
void tiku_stm32n6_svc_handler(void)         __attribute__((weak, alias("stm32n6_default_handler")));
void tiku_stm32n6_debug_handler(void)       __attribute__((weak, alias("stm32n6_default_handler")));
void tiku_stm32n6_pendsv_handler(void)      __attribute__((weak, alias("stm32n6_default_handler")));
void tiku_stm32n6_systick_handler(void)     __attribute__((weak, alias("stm32n6_default_handler")));

/* External IRQs the port wires. The timer driver supplies the real LPTIM1
 * handler; the weak stub keeps builds that leave it out linking. */
void tiku_stm32n6_lptim1_isr(void)          __attribute__((weak, alias("stm32n6_default_handler")));
void tiku_stm32n6_gpdma_ch0_isr(void)       __attribute__((weak, alias("stm32n6_default_handler")));

/* One EXTI vector per line, so a handler never has to scan for its own line. */
#define EXTI_WEAK(n) \
    void tiku_stm32n6_exti##n##_isr(void) __attribute__((weak, alias("stm32n6_default_handler")));
EXTI_WEAK(0)  EXTI_WEAK(1)  EXTI_WEAK(2)  EXTI_WEAK(3)
EXTI_WEAK(4)  EXTI_WEAK(5)  EXTI_WEAK(6)  EXTI_WEAK(7)
EXTI_WEAK(8)  EXTI_WEAK(9)  EXTI_WEAK(10) EXTI_WEAK(11)
EXTI_WEAK(12) EXTI_WEAK(13) EXTI_WEAK(14) EXTI_WEAK(15)


void tiku_stm32n6_startup(void);

/**
 * @brief Image entry point: establish the stack, then run the C startup.
 *
 * The boot ROM jumps here without loading SP from vector word 0, so SP must
 * be set before any compiler-generated prologue can push to it.
 *
 * @note Naked and assembly-only, the sole defined use of the attribute.
 */
__attribute__((naked, section(".text"), used))
void tiku_stm32n6_reset_handler(void) {
    __asm__ volatile (
        "ldr  r0, =__stack\n"
        "mov  sp, r0\n"
        "bl   tiku_stm32n6_startup\n"
        "b    .\n"
        ".ltorg\n");
}

void tiku_stm32n6_startup(void) {
    /* The core resets with interrupts enabled; mask them until the kernel is
     * ready to take one. */
    __asm__ volatile ("cpsid i" ::: "memory");

    extern const stm32n6_isr_t tiku_stm32n6_vectors[];
    *(volatile uint32_t *)0xE000ED08UL = (uint32_t)(uintptr_t)tiku_stm32n6_vectors;
    __asm__ volatile ("dsb\n\tisb" ::: "memory");

    /* The whole image is loaded into SRAM, so .data usually needs no copy;
     * the loop covers the case where the linker splits load and run. */
    const uint32_t *src = &__data_load;
    uint32_t *dst = &__data_start;
    if (src != dst) {
        while (dst < &__data_end) {
            *dst++ = *src++;
        }
    }

    for (uint32_t *b = &__bss_start; b < &__bss_end; b++) {
        *b = 0UL;
    }

    /* Before the arena is zeroed, not after: the banks it lives in come out of
     * reset shut down, and a write to a shut-down bank is swallowed silently
     * rather than faulting, so the zeroing would simply not happen. */
    tiku_stm32n6_sram_init();

    /* Both caches: the ROM's dev-boot path hands over with them off, and the
     * flash-boot path keeps only the I-cache -- so neither state can be
     * assumed. Before the arena zero loop, which then runs write-allocated. */
    tiku_stm32n6_cache_enable();

    /* Before any driver runs: a bus or usage error that escalates to
     * HardFault loses the status bits that say what it actually was. */
    tiku_stm32n6_fault_init();

    for (uint32_t *a = &__axisram_start; a < &__axisram_end; a++) {
        *a = 0UL;
    }

    (void)main();

    while (1) {
        __asm__ volatile ("wfe");
    }
}

/**
 * @brief Cortex-M55 vector table, placed at the image base.
 *
 * Word 0 is the initial SP and word 1 the reset handler; the boot ROM reads
 * both, and the function pointer carries the Thumb bit the core requires.
 */
/* The named handlers below deliberately override the default fill at their
 * index, which is exactly what -Woverride-init warns about. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
__attribute__((section(".vectors"), used, aligned(1024)))
const stm32n6_isr_t tiku_stm32n6_vectors[16 + STM32N6_NUM_EXT_IRQS] = {
    (stm32n6_isr_t)(uintptr_t)&__stack,
    tiku_stm32n6_reset_handler,
    tiku_stm32n6_nmi_handler,
    tiku_stm32n6_hard_fault_handler,
    tiku_stm32n6_mem_fault_handler,
    tiku_stm32n6_bus_fault_handler,
    tiku_stm32n6_usage_fault_handler,
    tiku_stm32n6_secure_fault_handler,
    0, 0, 0,
    tiku_stm32n6_svc_handler,
    tiku_stm32n6_debug_handler,
    0,
    tiku_stm32n6_pendsv_handler,
    tiku_stm32n6_systick_handler,

    /* Every external IRQ gets the default handler. A zero-filled tail would
     * send an unexpected interrupt to address 0 instead of parking it. */
    DFL16, DFL16, DFL16, DFL16, DFL16,
    DFL16, DFL16, DFL16, DFL16, DFL16,

    /* Named handlers last: a designated initializer overrides the positional
     * default already written at that index. */
    [16 + STM32N6_IRQ_LPTIM1]     = tiku_stm32n6_lptim1_isr,
    [16 + STM32N6_IRQ_GPDMA1_CH0] = tiku_stm32n6_gpdma_ch0_isr,

    [16 + STM32N6_IRQ_EXTI0 +  0] = tiku_stm32n6_exti0_isr,
    [16 + STM32N6_IRQ_EXTI0 +  1] = tiku_stm32n6_exti1_isr,
    [16 + STM32N6_IRQ_EXTI0 +  2] = tiku_stm32n6_exti2_isr,
    [16 + STM32N6_IRQ_EXTI0 +  3] = tiku_stm32n6_exti3_isr,
    [16 + STM32N6_IRQ_EXTI0 +  4] = tiku_stm32n6_exti4_isr,
    [16 + STM32N6_IRQ_EXTI0 +  5] = tiku_stm32n6_exti5_isr,
    [16 + STM32N6_IRQ_EXTI0 +  6] = tiku_stm32n6_exti6_isr,
    [16 + STM32N6_IRQ_EXTI0 +  7] = tiku_stm32n6_exti7_isr,
    [16 + STM32N6_IRQ_EXTI0 +  8] = tiku_stm32n6_exti8_isr,
    [16 + STM32N6_IRQ_EXTI0 +  9] = tiku_stm32n6_exti9_isr,
    [16 + STM32N6_IRQ_EXTI0 + 10] = tiku_stm32n6_exti10_isr,
    [16 + STM32N6_IRQ_EXTI0 + 11] = tiku_stm32n6_exti11_isr,
    [16 + STM32N6_IRQ_EXTI0 + 12] = tiku_stm32n6_exti12_isr,
    [16 + STM32N6_IRQ_EXTI0 + 13] = tiku_stm32n6_exti13_isr,
    [16 + STM32N6_IRQ_EXTI0 + 14] = tiku_stm32n6_exti14_isr,
    [16 + STM32N6_IRQ_EXTI0 + 15] = tiku_stm32n6_exti15_isr,
};
#pragma GCC diagnostic pop
