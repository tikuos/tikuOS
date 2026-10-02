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

/** @brief Handlers run here, not on whichever stack was interrupted. */
static uint32_t isr_stack[TIKU_ESP32C61_ISR_STACK_WORDS]
    __attribute__((aligned(16), used));

/**
 * @brief Trap entry: the whole context to the stack, then C on the ISR stack.
 *
 * Exceptions and non-vectored interrupts both land here, 64-byte aligned as
 * CLIC mode requires.  tiku_esp32c61_trap() returns the frame to resume --
 * the same one, or another thread's -- and that is what mret goes back to.
 */
__attribute__((naked, aligned(64)))
void tiku_esp32c61_trap_entry(void) {
    __asm__ volatile (
        "addi sp, sp, -128\n"
        "sw ra,   0(sp)\n" "sw t0,   4(sp)\n" "sw t1,   8(sp)\n"
        "sw t2,  12(sp)\n" "sw s0,  16(sp)\n" "sw s1,  20(sp)\n"
        "sw a0,  24(sp)\n" "sw a1,  28(sp)\n" "sw a2,  32(sp)\n"
        "sw a3,  36(sp)\n" "sw a4,  40(sp)\n" "sw a5,  44(sp)\n"
        "sw a6,  48(sp)\n" "sw a7,  52(sp)\n" "sw s2,  56(sp)\n"
        "sw s3,  60(sp)\n" "sw s4,  64(sp)\n" "sw s5,  68(sp)\n"
        "sw s6,  72(sp)\n" "sw s7,  76(sp)\n" "sw s8,  80(sp)\n"
        "sw s9,  84(sp)\n" "sw s10, 88(sp)\n" "sw s11, 92(sp)\n"
        "sw t3,  96(sp)\n" "sw t4, 100(sp)\n" "sw t5, 104(sp)\n"
        "sw t6, 108(sp)\n"
        "csrr t0, mepc\n"    "sw t0, 112(sp)\n"
        "csrr t0, mstatus\n" "sw t0, 116(sp)\n"
        "csrr t0, mcause\n"  "sw t0, 120(sp)\n"
        "mv a0, sp\n"
        "la sp, isr_stack + %0\n"
        "call tiku_esp32c61_trap\n"
        "mv sp, a0\n"
        /* mcause after mstatus: with the CLIC it carries the interrupt level
         * mret restores, and aliases the MPIE/MPP bits mstatus just set. */
        "lw t0, 116(sp)\n"   "csrw mstatus, t0\n"
        "lw t0, 120(sp)\n"   "csrw mcause, t0\n"
        "lw t0, 112(sp)\n"   "csrw mepc, t0\n"
        "lw ra,   0(sp)\n" "lw t0,   4(sp)\n" "lw t1,   8(sp)\n"
        "lw t2,  12(sp)\n" "lw s0,  16(sp)\n" "lw s1,  20(sp)\n"
        "lw a0,  24(sp)\n" "lw a1,  28(sp)\n" "lw a2,  32(sp)\n"
        "lw a3,  36(sp)\n" "lw a4,  40(sp)\n" "lw a5,  44(sp)\n"
        "lw a6,  48(sp)\n" "lw a7,  52(sp)\n" "lw s2,  56(sp)\n"
        "lw s3,  60(sp)\n" "lw s4,  64(sp)\n" "lw s5,  68(sp)\n"
        "lw s6,  72(sp)\n" "lw s7,  76(sp)\n" "lw s8,  80(sp)\n"
        "lw s9,  84(sp)\n" "lw s10, 88(sp)\n" "lw s11, 92(sp)\n"
        "lw t3,  96(sp)\n" "lw t4, 100(sp)\n" "lw t5, 104(sp)\n"
        "lw t6, 108(sp)\n"
        "addi sp, sp, 128\n"
        "mret\n"
        :: "i" (TIKU_ESP32C61_ISR_STACK_WORDS * 4));
}

int tiku_esp32c61_in_isr(void) {
    uintptr_t sp;

    __asm__ volatile ("mv %0, sp" : "=r" (sp));
    return sp >= (uintptr_t)isr_stack &&
           sp < (uintptr_t)isr_stack + sizeof isr_stack;
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
uint32_t *tiku_esp32c61_irq_dispatch(uint32_t line, uint32_t *frame) {
    (void)line;
    return frame;
}

/** @brief Default after the dump: park.  The kernel records and resets. */
__attribute__((weak, noreturn))
void tiku_esp32c61_fault(uint32_t *frame, uint32_t cause) {
    (void)frame;
    (void)cause;
    for (;;) {
        __asm__ volatile ("wfi");
    }
}

/**
 * @brief Exceptions are reported and handed on; interrupts are dispatched.
 *
 * @param frame  The context the entry saved (TIKU_ESP32C61_F_* words)
 * @return The frame to resume: @p frame, or a thread switch's choice
 */
__attribute__((used))
uint32_t *tiku_esp32c61_trap(uint32_t *frame) {
    uint32_t cause = frame[TIKU_ESP32C61_F_MCAUSE];

    if (cause & 0x80000000UL) {
        return tiku_esp32c61_irq_dispatch(cause & 0xFFFUL, frame);
    }
    trap_puts("\r\n[TM:FAULT] ");
    trap_puts(trap_kind(cause & 0xFFFUL));
    trap_field("mcause", cause);
    trap_field("mtval", ESP32C61_CSR_READ(mtval));
    trap_field("pc", frame[TIKU_ESP32C61_F_MEPC]);
    trap_field("ra", frame[TIKU_ESP32C61_F_RA]);
    trap_field("sp", (uint32_t)(uintptr_t)frame + TIKU_ESP32C61_FRAME_BYTES);
    trap_puts("\r\n");
    /* Let the dump leave the FIFO before whatever follows resets the chip. */
    for (unsigned long spins = 2000000UL; spins > 0UL; spins--) {
        if (ESP32C61_UART_TXCNT(TIKU_REG32(ESP32C61_UART_STATUS(
                ESP32C61_UART0_BASE))) == 0UL) {
            break;
        }
    }
    tiku_esp32c61_fault(frame, cause);
}
