/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fault_arch.c - STM32N6 CPU fault capture.
 *
 * The handlers print through a spin-bounded UART, push a record into the NOR
 * mirror -- SRAM does not survive the reset they force -- and then reset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include "tiku_fault_arch.h"
#include "tiku_mem_arch.h"
#include "tiku_stm32n6_regs.h"
#include "tiku_uart_arch.h"
#include <kernel/memory/tiku_mem.h>

/* SRAM does not survive the reset the handler forces -- the boot ROM clears
 * it, which was measured after a record left in the image window and then one
 * left in AXISRAM1 both came back blank. So the record rides the durable
 * mirror instead: .persistent, flushed to the NOR before the reset and
 * restored from it on the way back up. */
static TIKU_DURABLE tiku_stm32n6_fault_record_t fault_rec;

void tiku_stm32n6_fault_init(void) {
    /* Without these a bus or usage error escalates straight to HardFault and
     * its own status bits never get written, which loses the one field that
     * says what actually went wrong. */
    TIKU_REG32(STM32N6_SCB_SHCSR) |= STM32N6_SCB_SHCSR_MEMFAULTENA |
                                     STM32N6_SCB_SHCSR_BUSFAULTENA |
                                     STM32N6_SCB_SHCSR_USGFAULTENA;
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}

const tiku_stm32n6_fault_record_t *tiku_stm32n6_fault_last(void) {
    return &fault_rec;
}

void tiku_stm32n6_fault_clear(void) {
    fault_rec.magic = 0UL;
    fault_rec.count = 0UL;
}

const char *tiku_stm32n6_fault_kind_name(uint32_t kind) {
    switch (kind) {
    case TIKU_STM32N6_FAULT_HARD:   return "hardfault";
    case TIKU_STM32N6_FAULT_MEM:    return "memmanage";
    case TIKU_STM32N6_FAULT_BUS:    return "busfault";
    case TIKU_STM32N6_FAULT_USAGE:  return "usagefault";
    case TIKU_STM32N6_FAULT_SECURE: return "securefault";
    default:                        return "unknown";
    }
}

/** @brief Emit one 32-bit value as eight hex digits. */
static void fault_puthex(uint32_t v) {
    static const char hx[] = "0123456789abcdef";
    for (int i = 28; i >= 0; i -= 4) {
        tiku_uart_putc(hx[(v >> i) & 0xFU]);
    }
}

/** @brief Emit a plain string without touching the printf machinery. */
static void fault_putstr(const char *s) {
    while (*s != '\0') {
        tiku_uart_putc(*s++);
    }
}

/** @brief Emit " name=" followed by the value in hex. */
static void fault_putfield(const char *name, uint32_t v) {
    tiku_uart_putc(' ');
    fault_putstr(name);
    fault_putstr("=0x");
    fault_puthex(v);
}

/** @brief Wait for the last character to leave the shifter, bounded. */
static void fault_drain(void) {
    /* putc returns when the holding register is free, not when the line is
     * idle, so a reset here would truncate the dump the host is reading. */
    for (unsigned long spins = 200000UL; spins > 0UL; spins--) {
        if (TIKU_REG32(STM32N6_USART_ISR(STM32N6_USART1_BASE)) &
            STM32N6_USART_ISR_TC) {
            return;
        }
    }
}

/**
 * @brief Shared body: record what is known, print it, reset.
 *
 * @param frame  Stacked exception frame, or NULL if the push itself failed
 * @param kind   Which handler ran
 */
__attribute__((used, noreturn))
void tiku_stm32n6_fault_body(const uint32_t *frame, uint32_t kind) {
    uint32_t cfsr = TIKU_REG32(STM32N6_SCB_CFSR);
    uint32_t hfsr = TIKU_REG32(STM32N6_SCB_HFSR);
    uint32_t addr = 0UL;

    if (cfsr & STM32N6_CFSR_MMARVALID) {
        addr = TIKU_REG32(STM32N6_SCB_MMFAR);
    } else if (cfsr & STM32N6_CFSR_BFARVALID) {
        addr = TIKU_REG32(STM32N6_SCB_BFAR);
    }

    /* A stacking error means the frame push faulted, so the words it points at
     * are whatever was already there; recorded as zero rather than as fiction. */
    int frame_ok = (frame != NULL) && ((cfsr & STM32N6_CFSR_STKERR_MSK) == 0UL);

    if (fault_rec.magic != TIKU_STM32N6_FAULT_MAGIC) {
        fault_rec.magic = TIKU_STM32N6_FAULT_MAGIC;
        fault_rec.count = 0UL;
    }
    fault_rec.count++;
    fault_rec.kind = kind;
    fault_rec.cfsr = cfsr;
    fault_rec.hfsr = hfsr;
    fault_rec.addr = addr;
    fault_rec.pc   = frame_ok ? frame[6] : 0UL;
    fault_rec.lr   = frame_ok ? frame[5] : 0UL;
    fault_rec.psr  = frame_ok ? frame[7] : 0UL;
    fault_rec.sp   = (uint32_t)(uintptr_t)frame;

    fault_putstr("\r\n[TM:FAULT] ");
    fault_putstr(tiku_stm32n6_fault_kind_name(kind));
    fault_putfield("cfsr", cfsr);
    fault_putfield("hfsr", hfsr);
    fault_putfield("addr", addr);
    fault_putfield("pc", fault_rec.pc);
    fault_putfield("lr", fault_rec.lr);
    fault_putstr("\r\n");
    fault_drain();

    /* Push the record to the NOR before resetting: SRAM will not survive what
     * comes next. The flush is the same bounded, polled path the durable store
     * uses, and a failure here costs only the record -- the dump is already
     * out on the wire. */
    tiku_mem_arch_nvm_flush();

    /* The faulting instruction cannot be stepped over, so returning would
     * fault again forever; the record and the dump are the whole yield. */
    TIKU_REG32(STM32N6_SCB_AIRCR) = STM32N6_SCB_AIRCR_VECTKEY |
                                    STM32N6_SCB_AIRCR_SYSRESETREQ;
    __asm__ volatile ("dsb" ::: "memory");
    for (;;) {
    }
}

/* Naked entry shims. EXC_RETURN bit 2 says which stack holds the frame, and
 * no C prologue may run first: if the fault was a stack overflow, the
 * prologue's own push would fault again and lose the original. */
#define FAULT_SHIM(fn, kindval)                                               \
    __attribute__((naked)) void fn(void) {                                    \
        __asm__ volatile (                                                    \
            "tst  lr, #4\n"                                                   \
            "ite  eq\n"                                                       \
            "mrseq r0, msp\n"                                                 \
            "mrsne r0, psp\n"                                                 \
            "mov  r1, %0\n"                                                   \
            "b    tiku_stm32n6_fault_body\n"                                  \
            :: "I"(kindval));                                                 \
    }

FAULT_SHIM(tiku_stm32n6_hard_fault_handler,   TIKU_STM32N6_FAULT_HARD)
FAULT_SHIM(tiku_stm32n6_mem_fault_handler,    TIKU_STM32N6_FAULT_MEM)
FAULT_SHIM(tiku_stm32n6_bus_fault_handler,    TIKU_STM32N6_FAULT_BUS)
FAULT_SHIM(tiku_stm32n6_usage_fault_handler,  TIKU_STM32N6_FAULT_USAGE)
FAULT_SHIM(tiku_stm32n6_secure_fault_handler, TIKU_STM32N6_FAULT_SECURE)
