/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_crt_early.c - STM32F411RE (Cortex-M4) startup
 *
 * Responsibilities:
 *  1. Copy .data from flash LMA to SRAM VMA
 *  2. Zero .bss
 *  3. Leave .uninit and .mpu_diag untouched (warm-reset persistence)
 *  4. Set the vector table and enable Cortex-M4 FPU access
 *  5. Call main()
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include "tiku_stm32f411_regs.h"

extern uint32_t __data_load;
extern uint32_t __data_start, __data_end;
extern uint32_t __bss_start,  __bss_end;
extern uint32_t __vectors_start;

extern int main(void);

static inline void stm32f411_early_dsb(void)
{
    __asm__ volatile ("dsb" ::: "memory");
}

static inline void stm32f411_early_isb(void)
{
    __asm__ volatile ("isb" ::: "memory");
}

static void stm32f411_early_core_init(void)
{
    __asm__ volatile ("cpsid i" ::: "memory");

    _STM32F411_REG(STM32F411_SCB_VTOR) = (uint32_t)(uintptr_t)&__vectors_start;
    _STM32F411_REG(STM32F411_SCB_CPACR) |=
        STM32F411_SCB_CPACR_CP10_FULL | STM32F411_SCB_CPACR_CP11_FULL;

    stm32f411_early_dsb();
    stm32f411_early_isb();
}

__attribute__((noreturn))
void tiku_stm32f411_reset_handler(void)
{
    stm32f411_early_core_init();

    /* Copy .data */
    uint32_t *src = &__data_load;
    for (uint32_t *dst = &__data_start; dst < &__data_end; )
        *dst++ = *src++;

    /* Zero .bss */
    for (uint32_t *p = &__bss_start; p < &__bss_end; )
        *p++ = 0;

    main();
    for (;;) {
        __asm__ volatile ("wfe" ::: "memory");
    }
}
