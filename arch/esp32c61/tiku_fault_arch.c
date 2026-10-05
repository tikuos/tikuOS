/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fault_arch.c - ESP32-C61 exception record across the reset.
 *
 * The crt prints the dump; this file records it in the durable region,
 * flushes the flash mirror and resets.  `diag fault` shows the record after
 * the reboot.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fault_arch.h"
#include "tiku_cpu_common.h"
#include "tiku_crt_early.h"
#include "tiku_mem_arch.h"
#include "tiku_esp32c61_regs.h"
#include <kernel/memory/tiku_mem.h>

/* The boot restores the durable region from the flash mirror, so a record
 * kept there only survives if it reaches the mirror before the reset. */
static TIKU_DURABLE tiku_esp32c61_fault_record_t fault_rec;

const tiku_esp32c61_fault_record_t *tiku_esp32c61_fault_last(void) {
    return &fault_rec;
}

void tiku_esp32c61_fault_clear(void) {
    fault_rec.magic = 0UL;
    fault_rec.count = 0UL;
    (void)tiku_mem_arch_nvm_flush_status();
}

const char *tiku_esp32c61_fault_kind_name(uint32_t code) {
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

/**
 * @brief The crt's exception hook: record, flush, reset.
 *
 * @param frame  The context the trap entry saved
 * @param cause  mcause
 */
__attribute__((noreturn))
void tiku_esp32c61_fault(uint32_t *frame, uint32_t cause) {
    if (fault_rec.magic != TIKU_ESP32C61_FAULT_MAGIC) {
        fault_rec.magic = TIKU_ESP32C61_FAULT_MAGIC;
        fault_rec.count = 0UL;
    }
    fault_rec.count++;
    fault_rec.mcause = cause;
    fault_rec.mtval  = ESP32C61_CSR_READ(mtval);
    fault_rec.pc     = frame[TIKU_ESP32C61_F_MEPC];
    fault_rec.ra     = frame[TIKU_ESP32C61_F_RA];
    fault_rec.sp     = (uint32_t)(uintptr_t)frame + TIKU_ESP32C61_FRAME_BYTES;
    /* A failed flush costs only the record: the dump is already out. */
    (void)tiku_mem_arch_nvm_flush_status();
    /* Returning would run the faulting instruction again, and fault again. */
    tiku_cpu_esp32c61_restart(0);
}
