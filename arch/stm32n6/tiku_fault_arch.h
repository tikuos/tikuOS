/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fault_arch.h - STM32N6 CPU fault capture.
 *
 * The fault handlers print the fault status over the UART, keep a record in
 * the NOR-mirrored durable region, and reset the part.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_FAULT_ARCH_H_
#define TIKU_STM32N6_FAULT_ARCH_H_

#include <stdint.h>

/** @brief Value of magic in a record the fault handler wrote. */
#define TIKU_STM32N6_FAULT_MAGIC    0x546B464CUL    /* "TkFL" */

/** @brief The last fault, as the handler recorded it. */
typedef struct {
    uint32_t magic;     /**< TIKU_STM32N6_FAULT_MAGIC when the rest is valid */
    uint32_t count;     /**< faults recorded since the record was last clear */
    uint32_t kind;      /**< tiku_stm32n6_fault_kind_t of the last one       */
    uint32_t cfsr;      /**< configurable fault status                       */
    uint32_t hfsr;      /**< hard fault status                               */
    uint32_t addr;      /**< MMFAR or BFAR, 0 when neither was valid         */
    uint32_t pc;        /**< stacked PC, 0 if the frame push itself failed   */
    uint32_t lr;        /**< stacked LR, 0 if the frame push failed          */
    uint32_t psr;       /**< stacked xPSR, 0 if the frame push failed        */
    uint32_t sp;        /**< address of the stacked frame                    */
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
 * @brief Enable the MemManage, BusFault and UsageFault handlers.
 *
 * Each such fault then reaches its own handler and is recorded under its own
 * kind; a fault inside a fault handler still escalates to HardFault.
 */
void tiku_stm32n6_fault_init(void);

/**
 * @brief The last recorded fault.
 *
 * @return The record; its other fields are valid only when magic equals
 *         TIKU_STM32N6_FAULT_MAGIC
 */
const tiku_stm32n6_fault_record_t *tiku_stm32n6_fault_last(void);

/**
 * @brief Clear magic and count, marking the record empty.
 *
 * @note The NOR mirror keeps the old record, and boot restores it, until the
 *       durable region is next flushed.
 */
void tiku_stm32n6_fault_clear(void);

/**
 * @brief Name of a fault kind, for printing.
 *
 * @param kind  A tiku_stm32n6_fault_kind_t value
 * @return A static string; "unknown" for any other value
 */
const char *tiku_stm32n6_fault_kind_name(uint32_t kind);

#endif /* TIKU_STM32N6_FAULT_ARCH_H_ */
