/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_fault_arch.c - C5 exception record and reset, and fatal-error halts.
 * Fault handling does not write flash or return to the faulting instruction.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_fault_arch.h"
#include "tiku_timer_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_cpu_common.h"
#include <kernel/memory/tiku_mem.h>
#include <services/console/tiku_usb_serial_jtag.h>

/* Retained SRAM keeps the record across the reset that follows the fault. */
static TIKU_RETAINED tiku_c5_fault_record_t fault_rec;

const tiku_c5_fault_record_t *tiku_c5_fault_last(void)
{
    return &fault_rec;
}

void tiku_c5_fault_clear(void)
{
    fault_rec.magic = 0;
    fault_rec.count = 0;
}

const char *tiku_c5_fault_kind_name(uint32_t code)
{
    switch (code) {
    case 0:
        return "fetch-misaligned";
    case 1:
        return "fetch-fault";
    case 2:
        return "illegal";
    case 3:
        return "breakpoint";
    case 4:
        return "load-misaligned";
    case 5:
        return "load-fault";
    case 6:
        return "store-misaligned";
    case 7:
        return "store-fault";
    case 11:
        return "ecall";
    default:
        return "exception";
    }
}

/** @brief Write terminal fault bytes without libc or the interrupted TX queue.
 */
static void fault_text(const char *text)
{
    unsigned tries = 0;
    while (*text && tries++ < 100000u) {
        if (tiku_usb_serial_jtag_write((const uint8_t *)text, 1) == 1) {
            text++;
            tries = 0;
        }
    }
}

/** @brief Print a register value without allocating C-library formatting state.
 */
static void fault_hex(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char text[9];
    unsigned i;
    for (i = 0; i < 8; i++) {
        text[i] = digits[(value >> (28u - i * 4u)) & 15u];
    }
    text[8] = 0;
    fault_text(text);
}

/** @brief Halt with interrupts disabled after direct FIFO reporting. */
static void halt(void) __attribute__((noreturn));
static void halt(void)
{
    (void)TIKU_C5_IRQ_SAVE();
    for (;;) {
        __asm__ volatile("wfi");
    }
}
void tiku_c5_fatal(const char *reason)
{
    (void)TIKU_C5_IRQ_SAVE();
    fault_text("\r\nC5 HALT: ");
    fault_text(reason);
    fault_text("\r\n");
    halt();
}
/** @brief Report and record the machine exception registers, then reset. */
void tiku_esp32c5_diagnostic_fault(uint32_t cause, uint32_t pc, uint32_t value)
{
    (void)TIKU_C5_IRQ_SAVE();
    fault_text("\r\nTRAP cause=");
    fault_hex(cause);
    fault_text(" pc=");
    fault_hex(pc);
    fault_text(" value=");
    fault_hex(value);
    fault_text("\r\n");
    if (fault_rec.magic != TIKU_C5_FAULT_MAGIC) {
        fault_rec.magic = TIKU_C5_FAULT_MAGIC;
        fault_rec.count = 0;
    }
    fault_rec.count++;
    fault_rec.mcause = cause;
    fault_rec.mtval = value;
    fault_rec.pc = pc;
    tiku_cpu_c5_restart();
}
