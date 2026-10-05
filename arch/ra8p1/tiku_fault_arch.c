/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fault_arch.c - RA8P1 fault handlers: dump, record, reset.
 *
 * The record is TIKU_RETAINED: it survives the warm reset the handler
 * requests, and it is not MPU-protected, so a handler writes it without
 * opening an MPU window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fault_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_uart_arch.h"
#include "tiku_cache_arch.h"

#include <kernel/memory/tiku_mem.h>

/* The last fault, in retained SRAM: it survives the handler's warm reset
 * and is lost at power-off. */
static TIKU_RETAINED tiku_ra8p1_fault_record_t fault_rec;

void tiku_ra8p1_fault_init(void)
{
    /* With these enabled a MemManage, BusFault or UsageFault is recorded
     * under its own kind; disabled, it escalates and is recorded as
     * TIKU_RA8P1_FAULT_HARD. */
    TIKU_REG32(RA8P1_SCB_SHCSR) |= RA8P1_SCB_SHCSR_MEMFAULTENA |
                                   RA8P1_SCB_SHCSR_BUSFAULTENA |
                                   RA8P1_SCB_SHCSR_USGFAULTENA;
}

const tiku_ra8p1_fault_record_t *tiku_ra8p1_fault_last(void)
{
    return &fault_rec;
}

void tiku_ra8p1_fault_clear(void)
{
    fault_rec.magic = 0UL;
    fault_rec.count = 0UL;
}

const char *tiku_ra8p1_fault_kind_name(uint32_t kind)
{
    switch (kind) {
        case TIKU_RA8P1_FAULT_HARD:  return "hard";
        case TIKU_RA8P1_FAULT_MEM:   return "memmanage";
        case TIKU_RA8P1_FAULT_BUS:   return "bus";
        case TIKU_RA8P1_FAULT_USAGE: return "usage";
        case TIKU_RA8P1_FAULT_UNEXPECTED: return "unexpected";
        default:                     return "unknown";
    }
}

/** @brief Write one string straight to the console, CR-expanded. */
static void fault_putstr(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n') {
            tiku_uart_putc('\r');
        }
        tiku_uart_putc(*s++);
    }
}

/** @brief Print " name=XXXXXXXX" in hex, without printf. */
static void fault_putfield(const char *name, uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;

    fault_putstr(" ");
    fault_putstr(name);
    fault_putstr("=");
    for (i = 28; i >= 0; i -= 4) {
        tiku_uart_putc(hex[(v >> i) & 0xFU]);
    }
}

__attribute__((used, noreturn))
void tiku_ra8p1_fault_body(const uint32_t *frame, uint32_t kind,
                           uint32_t exc_return)
{
    uint32_t cfsr = TIKU_REG32(RA8P1_SCB_CFSR);
    uint32_t hfsr = TIKU_REG32(RA8P1_SCB_HFSR);
    uint32_t addr = 0UL;
    int frame_ok;

    if (cfsr & RA8P1_CFSR_MMARVALID) {
        addr = TIKU_REG32(RA8P1_SCB_MMFAR);
    } else if (cfsr & RA8P1_CFSR_BFARVALID) {
        addr = TIKU_REG32(RA8P1_SCB_BFAR);
    }

    /* After a stacking error the frame was not written, so pc, lr, psr and
     * raw are recorded as zero. */
    frame_ok = (frame != NULL) && ((cfsr & RA8P1_CFSR_STKERR_MSK) == 0UL);

    if (fault_rec.magic != TIKU_RA8P1_FAULT_MAGIC) {
        fault_rec.magic = TIKU_RA8P1_FAULT_MAGIC;
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
    fault_rec.exc  = exc_return;
    __asm__ volatile ("mrs %0, msp" : "=r" (fault_rec.msp));
    __asm__ volatile ("mrs %0, psp" : "=r" (fault_rec.psp));
    {
        /* Twelve words from the frame address: the eight-word basic
         * frame and the four words above it. */
        uint32_t i;
        for (i = 0U; i < 12U; i++) {
            fault_rec.raw[i] = frame_ok ? frame[i] : 0UL;
        }
    }

    fault_putstr("\n[TM:FAULT] ");
    fault_putstr(tiku_ra8p1_fault_kind_name(kind));
    fault_putfield("cfsr", cfsr);
    fault_putfield("hfsr", hfsr);
    fault_putfield("addr", addr);
    fault_putfield("pc", fault_rec.pc);
    fault_putfield("lr", fault_rec.lr);
    fault_putstr("\n");

    /*
     * Clean the record out of the D-cache before the reset: SYSRESETREQ
     * discards dirty lines, and a record still in one would be lost.
     */
    tiku_ra8p1_dcache_clean(&fault_rec, sizeof(fault_rec));

    /*
     * Reset: returning would re-execute the faulting instruction.  The reset
     * re-enters the MRAM image, and the record survives in retained SRAM.
     */
    TIKU_REG32(RA8P1_SCB_AIRCR) = RA8P1_AIRCR_VECTKEY |
                                  RA8P1_AIRCR_SYSRESETREQ;
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
            "mov  r2, lr\n"                                                   \
            "b    tiku_ra8p1_fault_body\n"                                    \
            :: "I"(kindval));                                                 \
    }

FAULT_SHIM(tiku_ra8p1_hard_fault_handler,  TIKU_RA8P1_FAULT_HARD)
FAULT_SHIM(tiku_ra8p1_mem_fault_handler,   TIKU_RA8P1_FAULT_MEM)
FAULT_SHIM(tiku_ra8p1_bus_fault_handler,   TIKU_RA8P1_FAULT_BUS)
FAULT_SHIM(tiku_ra8p1_usage_fault_handler, TIKU_RA8P1_FAULT_USAGE)
