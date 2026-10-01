/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_crt_early.c - ESP32-C61 (RV32IMAC) startup and the trap entry.
 *
 * The ROM jumps to the entry on its own stack with no global pointer, so the
 * entry sets both before any C runs; traps enter at the CLIC vector base.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <reent.h>
#include "tiku_esp32c61_regs.h"
#include "tiku_crt_early.h"

/* Linker-script symbols. */
extern uint32_t __data_load;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;

extern int main(void);

/*---------------------------------------------------------------------------*/
/* ENTRY                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Image entry: global pointer and stack, then C.
 *
 * gp is loaded with relaxation off, or the linker would relax the load
 * itself into a gp-relative one before gp holds anything.
 */
__attribute__((naked, section(".text.entry")))
void tiku_esp32c61_reset_handler(void) {
    __asm__ volatile (
        ".option push\n"
        ".option norelax\n"
        "la gp, __global_pointer$\n"
        ".option pop\n"
        "la sp, __stack\n"
        "tail tiku_esp32c61_c_start\n");
}

/** @brief .data, .bss, the trap vector, then main(). */
__attribute__((noreturn, used))
void tiku_esp32c61_c_start(void) {
    uint32_t *src = &__data_load;
    uint32_t *dst = &__data_start;

    if (src != dst) {
        while (dst < &__data_end) {
            *dst++ = *src++;
        }
    }
    for (dst = &__bss_start; dst < &__bss_end; dst++) {
        *dst = 0UL;
    }

    ESP32C61_CSR_WRITE(mtvec, (uint32_t)(uintptr_t)tiku_esp32c61_trap_entry |
                              ESP32C61_MTVEC_MODE_CLIC);

    (void)main();
    for (;;) {
        __asm__ volatile ("wfi");
    }
}

/**
 * @brief The C library's state, for errno and friends.
 *
 * Espressif's newlib asks the OS for it per thread; one core and one
 * context here, so it is always the static one.  libnosys's stub faults.
 */
struct _reent *__getreent(void) {
    return _impure_ptr;
}

/*---------------------------------------------------------------------------*/
/* TRAPS                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Trap entry: the caller-saved registers to the stack, then C.
 *
 * Exceptions and non-vectored interrupts both land here, 64-byte aligned as
 * CLIC mode requires; tiku_esp32c61_trap() decides which it was.
 */
__attribute__((naked, aligned(64)))
void tiku_esp32c61_trap_entry(void) {
    __asm__ volatile (
        "addi sp, sp, -64\n"
        "sw ra,  0(sp)\n"  "sw t0,  4(sp)\n"  "sw t1,  8(sp)\n"
        "sw t2, 12(sp)\n"  "sw a0, 16(sp)\n"  "sw a1, 20(sp)\n"
        "sw a2, 24(sp)\n"  "sw a3, 28(sp)\n"  "sw a4, 32(sp)\n"
        "sw a5, 36(sp)\n"  "sw a6, 40(sp)\n"  "sw a7, 44(sp)\n"
        "sw t3, 48(sp)\n"  "sw t4, 52(sp)\n"  "sw t5, 56(sp)\n"
        "sw t6, 60(sp)\n"
        "mv a0, sp\n"
        "call tiku_esp32c61_trap\n"
        "lw ra,  0(sp)\n"  "lw t0,  4(sp)\n"  "lw t1,  8(sp)\n"
        "lw t2, 12(sp)\n"  "lw a0, 16(sp)\n"  "lw a1, 20(sp)\n"
        "lw a2, 24(sp)\n"  "lw a3, 28(sp)\n"  "lw a4, 32(sp)\n"
        "lw a5, 36(sp)\n"  "lw a6, 40(sp)\n"  "lw a7, 44(sp)\n"
        "lw t3, 48(sp)\n"  "lw t4, 52(sp)\n"  "lw t5, 56(sp)\n"
        "lw t6, 60(sp)\n"
        "addi sp, sp, 64\n"
        "mret\n");
}

/** @brief One character straight into UART0's FIFO, bounded. */
static void trap_putc(char c) {
    for (unsigned long spins = 200000UL; spins > 0UL; spins--) {
        uint32_t st = TIKU_REG32(ESP32C61_UART_STATUS(ESP32C61_UART0_BASE));
        if (ESP32C61_UART_TXCNT(st) < ESP32C61_UART_FIFO_DEPTH - 1UL) {
            TIKU_REG32(ESP32C61_UART_FIFO(ESP32C61_UART0_BASE)) =
                (uint32_t)(unsigned char)c;
            return;
        }
    }
}

/** @brief A string, character by character. */
static void trap_puts(const char *s) {
    while (*s != '\0') {
        trap_putc(*s++);
    }
}

/** @brief " name=0x" and the value in eight hex digits. */
static void trap_field(const char *name, uint32_t v) {
    static const char hx[] = "0123456789abcdef";

    trap_putc(' ');
    trap_puts(name);
    trap_puts("=0x");
    for (int i = 28; i >= 0; i -= 4) {
        trap_putc(hx[(v >> i) & 0xFU]);
    }
}

/** @brief What an exception code means, for the dump's first word. */
static const char *trap_kind(uint32_t code) {
    switch (code) {
    case 0:  return "fetch-misaligned";
    case 1:  return "fetch-fault";
    case 2:  return "illegal";
    case 3:  return "breakpoint";
    case 4:  return "load-misaligned";
    case 5:  return "load-fault";
    case 6:  return "store-misaligned";
    case 7:  return "store-fault";
    case 11: return "ecall";
    default: return "exception";
    }
}

/** @brief Default for interrupts until the interrupt layer claims them. */
__attribute__((weak))
void tiku_esp32c61_irq_dispatch(uint32_t line, uint32_t *frame) {
    (void)line;
    (void)frame;
}

/**
 * @brief Exceptions are reported and parked; interrupts are dispatched.
 *
 * @param frame  The registers the entry saved: ra first, t6 last
 */
__attribute__((used))
void tiku_esp32c61_trap(uint32_t *frame) {
    uint32_t cause = ESP32C61_CSR_READ(mcause);

    if (cause & 0x80000000UL) {
        tiku_esp32c61_irq_dispatch(cause & 0xFFFUL, frame);
        return;
    }
    trap_puts("\r\n[TM:FAULT] ");
    trap_puts(trap_kind(cause & 0xFFFUL));
    trap_field("mcause", cause);
    trap_field("mtval", ESP32C61_CSR_READ(mtval));
    trap_field("pc", ESP32C61_CSR_READ(mepc));
    trap_field("ra", frame[0]);
    trap_field("sp", (uint32_t)(uintptr_t)frame + 64UL);
    trap_puts("\r\n");
    for (;;) {
        __asm__ volatile ("wfi");
    }
}
