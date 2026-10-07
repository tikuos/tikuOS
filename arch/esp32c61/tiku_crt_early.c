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
#include <string.h>
#if (TIKU_THREADS_ENABLE + 0)
#include <kernel/threads/tiku_thread.h>
#endif
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

#if (TIKU_THREADS_ENABLE + 0)
static struct _reent worker_reent[TIKU_THREADS_MAX];
static struct _reent irq_reent;
static struct _reent *kernel_reent;
static uint8_t worker_reent_ready[TIKU_THREADS_MAX];

/** @brief Reset a worker slot's C-library state before starting that worker. */
void tiku_esp32c61_reent_init(uint8_t slot)
{
    if (slot >= TIKU_THREADS_MAX) { return; }
    if (kernel_reent == NULL) {
        kernel_reent = _impure_ptr;
        _REENT_INIT_PTR(&irq_reent);
    }
    if (worker_reent_ready[slot]) {
        _reclaim_reent(&worker_reent[slot]);
    }
    _REENT_INIT_PTR(&worker_reent[slot]);
    worker_reent_ready[slot] = 1u;
}

/** @brief Select the C-library state of the thread about to resume. */
static struct _reent *reent_current(void)
{
    tiku_thread_t *thread = tiku_thread_self();
    return thread != NULL && thread->slot < TIKU_THREADS_MAX ?
        &worker_reent[thread->slot] : kernel_reent;
}
#endif

/**
 * @brief The C library's reentrancy state, for errno and stdio.
 *
 * Trap entry selects interrupt state; trap exit selects the kernel or
 * worker state. Both dynamic-reentrancy and _impure_ptr callers use it.
 */
struct _reent *__getreent(void) {
    return _impure_ptr;
}

/*---------------------------------------------------------------------------*/
/* TRAPS                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief The stack every trap handler runs on. */
static uint32_t isr_stack[TIKU_ESP32C61_ISR_STACK_WORDS]
    __attribute__((aligned(16), used));

/**
 * @brief Trap entry: save the context on the interrupted stack, then run C
 *        on the ISR stack.
 *
 * Exceptions and non-vectored interrupts land here; CLIC mode requires the
 * 64-byte alignment.  mret resumes the frame tiku_esp32c61_trap() returns:
 * this one, or another thread's.
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
         * mret restores, and aliases the MPIE/MPP bits the mstatus write
         * set. */
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

/** @brief Write one character to UART0's FIFO; a FIFO that stays full drops
 *         it. */
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

/** @brief Write a string through trap_putc(). */
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

/** @brief Weak default: resumes @p frame.  tiku_irq_arch.c's definition
 *         replaces it. */
__attribute__((weak))
uint32_t *tiku_esp32c61_irq_dispatch(uint32_t line, uint32_t *frame) {
    (void)line;
    return frame;
}

/** @brief Weak default after the dump: parks in wfi.  tiku_fault_arch.c's
 *         definition, where linked, records the fault and resets. */
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
#if (TIKU_THREADS_ENABLE + 0)
        if (kernel_reent != NULL) {
            _impure_ptr = &irq_reent;
            frame = tiku_esp32c61_irq_dispatch(cause & 0xFFFUL, frame);
            _impure_ptr = reent_current();
            return frame;
        }
#endif
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
