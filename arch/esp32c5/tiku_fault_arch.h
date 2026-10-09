/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_fault_arch.h - C5 exception record kept across the reset it causes.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_FAULT_ARCH_H_
#define TIKU_ESP32C5_FAULT_ARCH_H_

#include <stdint.h>

#define TIKU_C5_FAULT_MAGIC 0x546B464Cu    /* "TkFL" */

/** @brief The last exception, kept in retained SRAM. */
typedef struct {
    uint32_t magic;     /**< TIKU_C5_FAULT_MAGIC when the rest is valid    */
    uint32_t count;     /**< exceptions recorded since the last clear      */
    uint32_t mcause;    /**< exception code in the low bits                */
    uint32_t mtval;     /**< faulting address or instruction, by cause     */
    uint32_t pc;        /**< mepc: the instruction that faulted            */
} tiku_c5_fault_record_t;

/** @brief The record; check its magic before trusting the rest. */
const tiku_c5_fault_record_t *tiku_c5_fault_last(void);

/** @brief Clear the record. */
void tiku_c5_fault_clear(void);

/** @brief A name for an exception code. @param code  mcause & 0xfff */
const char *tiku_c5_fault_kind_name(uint32_t code);

#endif /* TIKU_ESP32C5_FAULT_ARCH_H_ */
