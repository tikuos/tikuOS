/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - RA8P1 (Cortex-M85) startup.
 *
 * A vector table at the image base and a reset handler that runs .data/.bss
 * and calls main.  R2 images are loaded into SRAM by the debugger, which sets
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <arch/ra8p1/tiku_device_select.h>
#include "tiku_ra8p1_regs.h"

/* Shorthand for filling the external-IRQ span with the default handler. */
#define DFL     ra8p1_default_handler
#define DFL4    DFL, DFL, DFL, DFL
#define DFL16   DFL4, DFL4, DFL4, DFL4

/* Linker-script symbols. */
extern uint32_t __data_load;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern uint32_t __stack;

extern int main(void);

typedef void (*ra8p1_isr_t)(void);

/**
 * @brief Default handler: park the core on an unhandled exception.
 *
 * Spinning on WFE keeps the core quiet and lands a debugger halt on a
 * recognisable PC instead of a random instruction stream.
 */
static void ra8p1_default_handler(void)
{
    while (1) {
        __asm__ volatile ("wfe");
    }
}

/**
 * @defgroup ra8p1_exception_stubs Cortex-M85 weak exception stubs
 * @brief Weak aliases resolving to ra8p1_default_handler.
 *
 * A non-weak definition of the same name anywhere in the kernel or a driver
 * replaces the stub, because the table references these symbols by name.
 */
void tiku_ra8p1_nmi_handler(void)         __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_hard_fault_handler(void)  __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_mem_fault_handler(void)   __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_bus_fault_handler(void)   __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_usage_fault_handler(void) __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_secure_fault_handler(void) __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_svc_handler(void)         __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_debug_handler(void)       __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_pendsv_handler(void)      __attribute__((weak, alias("ra8p1_default_handler")));

/* The kernel clock supplies the real SysTick handler; the weak stub keeps
 * builds that leave the timer out linking. */
void tiku_ra8p1_systick_handler(void)     __attribute__((weak, alias("ra8p1_default_handler")));

/* Console receive and error; weak so a build without the UART still links. */
void tiku_ra8p1_sci_rxi_handler(void)     __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_sci_eri_handler(void)     __attribute__((weak, alias("ra8p1_default_handler")));

/* htimer compare; weak so a build without it still links. */
void tiku_ra8p1_gpt0_ccmpa_handler(void)  __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_dmac0_handler(void)       __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_usbhs_handler(void)       __attribute__((weak, alias("ra8p1_default_handler")));

void tiku_ra8p1_startup(void);

/**
 * @brief Image entry point: establish the stack, then run the C startup.
 *
 * Naked and assembly-only.  The debugger sets SP before starting an SRAM
 * image, but a future MRAM boot enters here with whatever the ROM left, so
 * SP is set unconditionally rather than trusting the caller.
 */
__attribute__((naked, section(".text"), used))
void tiku_ra8p1_reset_handler(void)
{
    __asm__ volatile (
        "ldr  r0, =__stack\n"
        "mov  sp, r0\n"
        "bl   tiku_ra8p1_startup\n"
        "b    .\n"
        ".ltorg\n");
}

void tiku_ra8p1_startup(void)
{
    /* The core resets with interrupts enabled; mask them until the kernel is
     * ready to take one. */
    __asm__ volatile ("cpsid i" ::: "memory");

    extern const ra8p1_isr_t tiku_ra8p1_vectors[];
    TIKU_REG32(RA8P1_SCB_VTOR) = (uint32_t)(uintptr_t)tiku_ra8p1_vectors;
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
 * @brief Cortex-M85 vector table, placed at the image base.
 *
 * Word 0 is the initial SP and word 1 the reset handler; the function pointer
 * carries the Thumb bit the core requires.
 */
/* The named handlers below deliberately override the default fill at their
 * index, which is exactly what -Woverride-init warns about. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
__attribute__((section(".vectors"), used, aligned(512)))
const ra8p1_isr_t tiku_ra8p1_vectors[16 + TIKU_RA8P1_NUM_EXT_IRQS] = {
    (ra8p1_isr_t)(uintptr_t)&__stack,
    tiku_ra8p1_reset_handler,
    tiku_ra8p1_nmi_handler,
    tiku_ra8p1_hard_fault_handler,
    tiku_ra8p1_mem_fault_handler,
    tiku_ra8p1_bus_fault_handler,
    tiku_ra8p1_usage_fault_handler,
    tiku_ra8p1_secure_fault_handler,
    0, 0, 0,
    tiku_ra8p1_svc_handler,
    tiku_ra8p1_debug_handler,
    0,
    tiku_ra8p1_pendsv_handler,
    tiku_ra8p1_systick_handler,

    /* External IRQ 0 carries the console receive event; the ICU decides that
     * at run time (tiku_uart_arch.c links it), but the VECTOR is a build-time
     * choice and has to agree with UART_RXI_SLOT there.
     *
     * The rest get the default handler.  A zero-filled tail would send an
     * unexpected interrupt to address 0 instead of parking it. */
    tiku_ra8p1_sci_rxi_handler,
    tiku_ra8p1_sci_eri_handler,
    tiku_ra8p1_gpt0_ccmpa_handler,
    tiku_ra8p1_dmac0_handler,
    tiku_ra8p1_usbhs_handler,
    DFL4, DFL4,
    DFL16, DFL16, DFL16, DFL16, DFL16,
};
#pragma GCC diagnostic pop
