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

/* Linker-script symbols. */
extern uint32_t __data_load;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern uint32_t __stack;

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

void tiku_stm32n6_reset_handler(void) __attribute__((naked, section(".text"), used));

/**
 * @brief Reset handler: C runtime setup, then main().
 *
 * Masks interrupts, points VTOR at the table, copies .data when its load and
 * run addresses differ, zeroes .bss and calls main().
 *
 * @note Naked, so the compiler emits no prologue touching call-saved registers
 *       before the stack pointer is known good.
 */
void tiku_stm32n6_reset_handler(void) {
    /* The core resets with interrupts enabled; mask them until the kernel is
     * ready to take one. */
    __asm__ volatile ("cpsid i" ::: "memory");

    /* The boot ROM loads SP from vector word 0, but a warm entry may not have,
     * so set it explicitly before anything uses the stack. */
    __asm__ volatile ("ldr r0, =__stack\n\tmov sp, r0" ::: "r0", "memory");

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
    /* External IRQs default to the weak handler via the tail zero-fill; the
     * kernel installs real ones by overriding the named stubs. */
};
