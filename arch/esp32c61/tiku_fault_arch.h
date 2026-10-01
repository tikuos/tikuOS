/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fault_arch.h - ESP32-C61 exception record across the reset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_FAULT_ARCH_H_
#define TIKU_ESP32C61_FAULT_ARCH_H_

#include <stdint.h>

#define TIKU_ESP32C61_FAULT_MAGIC   0x546B464CUL    /* "TkFL" */

/** @brief The last exception, kept in the durable region. */
typedef struct {
    uint32_t magic;     /**< TIKU_ESP32C61_FAULT_MAGIC when the rest is valid */
    uint32_t count;     /**< exceptions recorded since the record was cleared */
    uint32_t mcause;    /**< exception code in the low bits                   */
    uint32_t mtval;     /**< faulting address or instruction, by cause        */
    uint32_t pc;        /**< mepc: the instruction that faulted               */
    uint32_t ra;        /**< return address at the fault                      */
    uint32_t sp;        /**< stack pointer at the fault                       */
} tiku_esp32c61_fault_record_t;

/** @brief The record; check its magic before trusting the rest. */
const tiku_esp32c61_fault_record_t *tiku_esp32c61_fault_last(void);

/** @brief Forget the record. */
void tiku_esp32c61_fault_clear(void);

/** @brief A name for an exception code. @param code  mcause & 0xfff */
const char *tiku_esp32c61_fault_kind_name(uint32_t code);

#endif /* TIKU_ESP32C61_FAULT_ARCH_H_ */
