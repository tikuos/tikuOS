/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fault_arch.h - CPU0 retained exception record and bounded reporting.
 *
 * tiku_crt_early.S passes the TIKU_P4_FAULT_* flags to
 * tiku_p4_fault_capture(); tiku_p4_fault_last() reads the record back
 * after a warm reset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_FAULT_ARCH_H_
#define TIKU_ESP32P4_FAULT_ARCH_H_

/** @brief Interrupted SP fails alignment or frame-range validation. */
#define TIKU_P4_FAULT_BAD_SP 1
/** @brief Trap occurred while an interrupt frame was being dispatched. */
#define TIKU_P4_FAULT_IN_IRQ 2

#ifndef __ASSEMBLER__
#include <stdint.h>

/** @brief Fixed 64-byte little-endian warm-reset record; CRC covers words 1..14. */
typedef struct {
    uint32_t magic, version, sequence, cause, pc, value, sp, status;
    uint32_t hart, reset, ra, flags, reserved[3], crc;
} tiku_p4_fault_record_t;

/** @brief Copy a validated record; return zero for absent or corrupt data.
 * @note CPU0 foreground or fault context with interrupts masked.
 */
int tiku_p4_fault_last(tiku_p4_fault_record_t *record);
/** @brief Invalidate the retained record; does not alter flash. */
void tiku_p4_fault_clear(void);
/** @brief Print the retained record or NONE with a bounded USB polling budget. */
void tiku_p4_fault_report(void);
/** @brief Record and report an exception, then halt on the dedicated fault stack.
 * @note Called by trap assembly with interrupts masked; does not return.
 */
void tiku_p4_fault_capture(uint32_t cause, uint32_t pc, uint32_t value,
                           uint32_t sp, uint32_t status, uint32_t hart,
                           uint32_t ra, uint32_t flags) __attribute__((noreturn));
/** @brief Handle explicit RAM-diagnostic fault/query commands; ignore other input. */
void tiku_p4_fault_command(int command);
#endif
#endif
