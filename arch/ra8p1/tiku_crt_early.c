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
 * and calls main.  The image runs from MRAM, with .data copied out to SRAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <arch/ra8p1/tiku_device_select.h>
#include "tiku_ra8p1_regs.h"
#include "tiku_fault_arch.h"

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
 * @brief Default handler: record an exception no other handler claims.
 *
 * Captures the stacked frame pointer, the exception code and EXC_RETURN, then
 * branches to the fault recorder instead of parking the core.
 */
__attribute__((naked)) static void ra8p1_default_handler(void)
{
    /*
     * Record, never park: an exception no handler claims would otherwise
     * spin here silently, preserving nothing, and a misfetched vector lands
     * here too carrying the stacked frame and the exception number.  Naked
     * like the fault shims -- no C prologue may run before the frame pointer
     * is captured, or a stack fault loses the frame.
     */
    __asm__ volatile (
        "tst  lr, #4\n"
        "ite  eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov  r1, %0\n"
        "mov  r2, lr\n"
        "b    tiku_ra8p1_fault_body\n"
        :: "I"(TIKU_RA8P1_FAULT_UNEXPECTED));
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
void tiku_ra8p1_ipc_handler(void)         __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_usbhs_handler(void)       __attribute__((weak, alias("ra8p1_default_handler")));
void tiku_ra8p1_npu_handler(void)         __attribute__((weak, alias("ra8p1_default_handler")));

void tiku_ra8p1_startup(void);

/**
 * @brief Image entry point: establish the stack, then run the C startup.
 *
 * Naked and assembly-only.  Entry carries whatever SP the ROM or a debugger
 * left, so SP is loaded from __stack unconditionally rather than trusted.
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

    /* .data runs from SRAM and loads from MRAM, so the copy always runs; the
     * guard only skips it if a link ever makes the two addresses coincide. */
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
__attribute__((section(".vectors"), used, aligned(512)))
const ra8p1_isr_t tiku_ra8p1_vectors[] = {
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
     * Every other slot must carry the default handler, because a zero entry
     * vectors an unexpected interrupt to address 0 instead of recording it.
     * The assert below is what makes a short initialiser a build error
     * rather than a hole at the top of the table. */
    tiku_ra8p1_sci_rxi_handler,
    tiku_ra8p1_sci_eri_handler,
    tiku_ra8p1_gpt0_ccmpa_handler,
    tiku_ra8p1_dmac0_handler,
    tiku_ra8p1_usbhs_handler,
    tiku_ra8p1_ipc_handler,
    tiku_ra8p1_npu_handler,
    DFL, DFL, DFL, DFL4,
    DFL16, DFL16, DFL16, DFL16, DFL16,
    DFL, DFL,
};

_Static_assert(sizeof(tiku_ra8p1_vectors) / sizeof(tiku_ra8p1_vectors[0]) ==
               16U + TIKU_RA8P1_NUM_EXT_IRQS,
               "vector table does not cover every external IRQ");
