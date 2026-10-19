/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fault_probe.c - Explicit exception injection for the P4 RAM profiles.
 *
 * tiku_p4_fault_command() raises a breakpoint, illegal instruction, load
 * fault, bad-SP trap or in-interrupt breakpoint on request, and prints or
 * clears the retained fault record.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_fault_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_alarm_arch.h"
#include "tiku_systimer_arch.h"

/** @brief Trigger a breakpoint while the interrupt dispatcher owns its stack. */
static void irq_fault(void)
{
    __asm__ volatile("ebreak");
}

void tiku_p4_fault_command(int command)
{
    uint32_t state;
    uint64_t now;
    if (command == 'd' || command == 'D') {
        state = TIKU_P4_IRQ_SAVE();
        if (command == 'D') tiku_p4_fault_clear();
        tiku_p4_fault_report();
        TIKU_P4_IRQ_RESTORE(state);
    } else if (command == 't') {
        __asm__ volatile("ebreak");
    } else if (command == 'x') {
        __asm__ volatile(".word 0");
    } else if (command == 'v') {
        __asm__ volatile("lw zero, 0(zero)");
    } else if (command == 'b') {
        __asm__ volatile("csrci mstatus, 8; li sp, 16; ebreak");
        __builtin_unreachable();
    } else if (command == 'n') {
        tiku_p4_alarm_stop();
        if (tiku_p4_alarm_init(irq_fault) == 0 &&
            tiku_p4_systimer_read(&now) == 0)
            (void)tiku_p4_alarm_schedule(now + 16000);
    }
}
