/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - STM32N6 (Cortex-M55) startup.
 *
 * The vector table at the image base and the reset handler: it sets SP,
 * prepares .data, .bss and .axisram, enables the caches and the fault
 * handlers, and calls main().
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

/* 160 external IRQs after the 16 system vectors: 176 entries, 704 bytes.
 * VTOR needs the table aligned to the next power of two, 1024, which the
 * image base meets. */
#define STM32N6_NUM_EXT_IRQS    160

/**
 * @brief Default handler: park the core on an unhandled exception.
 *
 * Spins on WFE, so a debugger halt lands on this function.
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

/* External IRQs the port wires.  tiku_timer_arch.c and tiku_dma_arch.c define
 * the real handlers; a build without one links the default handler. */
void tiku_stm32n6_lptim1_isr(void)          __attribute__((weak, alias("stm32n6_default_handler")));
void tiku_stm32n6_gpdma_ch0_isr(void)       __attribute__((weak, alias("stm32n6_default_handler")));

/* One EXTI vector per line; tiku_gpio_irq_arch.c defines the handlers. */
#define EXTI_WEAK(n) \
    void tiku_stm32n6_exti##n##_isr(void) __attribute__((weak, alias("stm32n6_default_handler")));
EXTI_WEAK(0)  EXTI_WEAK(1)  EXTI_WEAK(2)  EXTI_WEAK(3)
EXTI_WEAK(4)  EXTI_WEAK(5)  EXTI_WEAK(6)  EXTI_WEAK(7)
EXTI_WEAK(8)  EXTI_WEAK(9)  EXTI_WEAK(10) EXTI_WEAK(11)
EXTI_WEAK(12) EXTI_WEAK(13) EXTI_WEAK(14) EXTI_WEAK(15)


void tiku_stm32n6_startup(void);

/**
 * @brief Image entry point: set the stack, then run the C startup.
 *
 * The boot ROM jumps here without loading SP from vector word 0, so SP must
 * be set before any compiler-generated prologue can push to it.
 *
 * @note Naked: the body is basic assembly only, the one use GCC defines for
 *       the attribute.
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

/** @brief C startup, entered from the reset handler; calls main(). */
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

    /* The SRAM banks come out of reset shut down, and a write to a shut-down
     * bank is dropped without a fault, so they are powered before the
     * .axisram zero loop below. */
    tiku_stm32n6_sram_init();

    /* The ROM's dev-boot path hands over with both caches off and its
     * flash-boot path with only the I-cache on; this enables whichever is off.
     * The .axisram zero loop below then runs through the D-cache. */
    tiku_stm32n6_cache_enable();

    /* Before any driver runs, so a driver's MemManage, BusFault or
     * UsageFault reaches its own handler. */
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
 * Word 0 is the initial SP and word 1 the reset handler, the entry the boot
 * ROM jumps to; the function pointer carries the Thumb bit the core requires.
 */
/* The named handlers below override the default fill at their index, which
 * -Woverride-init warns about. */
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

    /* Every external IRQ defaults to stm32n6_default_handler; a zero entry
     * would send an unexpected interrupt to address 0. */
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
