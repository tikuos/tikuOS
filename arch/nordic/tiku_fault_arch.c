/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fault_arch.c - nRF54L fault reporters (Cortex-M33).
 *
 * Replaces the weak spin handlers with ones that print which fault fired, the
 * fault-status registers and the faulting PC and LR over the polled console,
 * then park.  A fault before UART init hangs in the first tiku_uart_putc().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <arch/nordic/tiku_uart_arch.h>

/* ARMv8-M (main) fault status/address registers. */
#define SCB_CFSR   (*(volatile uint32_t *)0xE000ED28UL)
#define SCB_HFSR   (*(volatile uint32_t *)0xE000ED2CUL)
#define SCB_MMFAR  (*(volatile uint32_t *)0xE000ED34UL)
#define SCB_BFAR   (*(volatile uint32_t *)0xE000ED38UL)
#define SCB_SFSR   (*(volatile uint32_t *)0xE000EDE4UL)
#define SCB_SFAR   (*(volatile uint32_t *)0xE000EDE8UL)

/** @brief Print @p s on the polled console, with CR before each LF. */
static void fault_puts(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n') {
            tiku_uart_putc('\r');
        }
        tiku_uart_putc(*s++);
    }
}

/** @brief Print @p v as eight hex digits. */
static void fault_put_hex(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;
    for (i = 28; i >= 0; i -= 4) {
        tiku_uart_putc(hex[(v >> i) & 0xFu]);
    }
}

/** @brief Print the label @p k followed by @p v in hex. */
static void fault_kv(const char *k, uint32_t v)
{
    fault_puts(k);
    fault_put_hex(v);
}

/**
 * @brief Common fault reporter: dump status registers + stacked PC/LR.
 *
 * @param frame  Exception frame (r0-r3, r12, LR, PC, xPSR) on the stack
 *               that was active when the fault fired.
 * @param ipsr   Exception number (3 hard, 4 mem, 5 bus, 6 usage, 7 secure).
 */
void tiku_nordic_fault_report(uint32_t *frame, uint32_t ipsr)
{
    fault_puts("\n!! FAULT exc=");
    fault_put_hex(ipsr & 0x1FFu);
    fault_kv(" CFSR=", SCB_CFSR);
    fault_kv(" HFSR=", SCB_HFSR);
    fault_kv(" BFAR=", SCB_BFAR);
    fault_kv(" MMFAR=", SCB_MMFAR);
    fault_kv(" SFSR=", SCB_SFSR);
    fault_kv(" SFAR=", SCB_SFAR);
    fault_kv("\n   PC=", frame[6]);
    fault_kv(" LR=", frame[5]);
    fault_kv(" xPSR=", frame[7]);
    fault_puts("  (parked; reset to recover)\n");

    for (;;) {
        __asm__ volatile ("wfe");
    }
}

/**
 * @brief Naked trampoline shared by every fault vector: select the stack in
 *        use (EXC_RETURN bit 2) and pass the frame and IPSR to the reporter.
 *
 * One body under five names; the vector table wires each alias separately.
 */
__attribute__((naked)) static void nordic_fault_entry(void)
{
    __asm__ volatile (
        "tst   lr, #4                    \n"
        "ite   eq                        \n"
        "mrseq r0, msp                   \n"
        "mrsne r0, psp                   \n"
        "mrs   r1, ipsr                  \n"
        "b     tiku_nordic_fault_report  \n");
}

void tiku_nordic_hard_fault_handler(void)
    __attribute__((alias("nordic_fault_entry")));
void tiku_nordic_mem_fault_handler(void)
    __attribute__((alias("nordic_fault_entry")));
void tiku_nordic_bus_fault_handler(void)
    __attribute__((alias("nordic_fault_entry")));
void tiku_nordic_usage_fault_handler(void)
    __attribute__((alias("nordic_fault_entry")));
void tiku_nordic_secure_fault_handler(void)
    __attribute__((alias("nordic_fault_entry")));
