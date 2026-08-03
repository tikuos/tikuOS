/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fault_arch.h - STM32N6 CPU fault capture.
 *
 * A silent wedge is the usual face of a CPU fault, so the handlers print what
 * they know and keep a record that outlives the reset they then force.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_FAULT_ARCH_H_
#define TIKU_STM32N6_FAULT_ARCH_H_

#include <stdint.h>

/** @brief Marks a record as written rather than left over as SRAM noise. */
#define TIKU_STM32N6_FAULT_MAGIC    0x546B464CUL    /* "TkFL" */

/** @brief What the handler knew at the moment of the fault. */
typedef struct {
    uint32_t magic;     /**< TIKU_STM32N6_FAULT_MAGIC when the rest is valid */
    uint32_t count;     /**< faults recorded since the record was last clear */
    uint32_t kind;      /**< tiku_stm32n6_fault_kind_t of the last one       */
    uint32_t cfsr;      /**< configurable fault status                       */
    uint32_t hfsr;      /**< hard fault status                               */
    uint32_t addr;      /**< MMFAR or BFAR, 0 when neither was valid         */
    uint32_t pc;        /**< stacked PC, 0 if the frame push itself failed   */
    uint32_t lr;        /**< stacked LR, same caveat                         */
    uint32_t psr;       /**< stacked xPSR                                    */
    uint32_t sp;        /**< the frame's own address                         */
} tiku_stm32n6_fault_record_t;

/** @brief Which handler ran. */
typedef enum {
    TIKU_STM32N6_FAULT_HARD   = 0,
    TIKU_STM32N6_FAULT_MEM    = 1,
    TIKU_STM32N6_FAULT_BUS    = 2,
    TIKU_STM32N6_FAULT_USAGE  = 3,
    TIKU_STM32N6_FAULT_SECURE = 4,
} tiku_stm32n6_fault_kind_t;

/**
 * @brief Enable the configurable faults so they do not escalate to HardFault.
 *
 * Escalation still happens for anything genuinely unrecoverable, but a plain
 * bus or usage error then arrives with its own status bits intact.
 */
void tiku_stm32n6_fault_init(void);

/**
 * @brief The last recorded fault.
 *
 * @return The record; check magic before trusting any other field
 */
const tiku_stm32n6_fault_record_t *tiku_stm32n6_fault_last(void);

/** @brief Forget the stored record. */
void tiku_stm32n6_fault_clear(void);

/** @brief Name for a kind, for printing. @param kind Kind @return Static name */
const char *tiku_stm32n6_fault_kind_name(uint32_t kind);

#endif /* TIKU_STM32N6_FAULT_ARCH_H_ */
