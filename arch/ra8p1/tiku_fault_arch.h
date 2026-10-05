/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fault_arch.h - RA8P1 fault record.
 *
 * The fault handlers print the fault, keep a record in retained SRAM, and
 * request a system reset, which re-enters the MRAM image; the record
 * survives that reset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_FAULT_ARCH_H_
#define TIKU_RA8P1_FAULT_ARCH_H_

#include <stdint.h>

/** @brief Value of magic in a record the fault handler wrote. */
#define TIKU_RA8P1_FAULT_MAGIC      0x546B4652UL    /* "TkFR" */

/** @brief What the handler knew at the moment of the fault. */
typedef struct {
    uint32_t magic;     /**< TIKU_RA8P1_FAULT_MAGIC when the rest is valid */
    uint32_t count;     /**< faults recorded since the record was last clear */
    uint32_t kind;      /**< tiku_ra8p1_fault_kind_t of the last one        */
    uint32_t cfsr;      /**< configurable fault status                      */
    uint32_t hfsr;      /**< hard fault status                              */
    uint32_t addr;      /**< MMFAR or BFAR, 0 when neither was valid        */
    uint32_t pc;        /**< stacked PC, 0 if the frame push itself failed  */
    uint32_t lr;        /**< stacked LR, 0 likewise                         */
    uint32_t psr;       /**< stacked xPSR                                   */
    uint32_t sp;        /**< the frame's own address                        */
    uint32_t exc;       /**< EXC_RETURN, naming frame type and stack        */
    uint32_t msp;       /**< MSP when the record was written                */
    uint32_t psp;       /**< PSP when the record was written                */
    uint32_t raw[12];   /**< 12 words from the frame address, 0 if invalid  */
} tiku_ra8p1_fault_record_t;

/** @brief Which handler ran. */
typedef enum {
    TIKU_RA8P1_FAULT_HARD  = 0,
    TIKU_RA8P1_FAULT_MEM   = 1,
    TIKU_RA8P1_FAULT_BUS   = 2,
    TIKU_RA8P1_FAULT_USAGE = 3,
    TIKU_RA8P1_FAULT_UNEXPECTED = 4,    /**< exception or IRQ with no handler */
} tiku_ra8p1_fault_kind_t;

/**
 * @brief Enable the MemManage, BusFault and UsageFault exceptions.
 *
 * Each such fault then runs its own handler and is recorded under its own
 * kind; disabled, it escalates to HardFault.
 */
void tiku_ra8p1_fault_init(void);

/**
 * @brief Record a fault, print it and request a system reset; never returns.
 *
 * The fault shims and the default handler branch here.
 *
 * @param frame       Stacked exception frame; not read when NULL or after a
 *                    stacking error
 * @param kind        Which handler ran, a tiku_ra8p1_fault_kind_t
 * @param exc_return  The handler's EXC_RETURN, naming frame type and stack
 */
__attribute__((noreturn))
void tiku_ra8p1_fault_body(const uint32_t *frame, uint32_t kind,
                           uint32_t exc_return);

/**
 * @brief The last recorded fault.
 *
 * @return The record; check magic before trusting any other field
 */
const tiku_ra8p1_fault_record_t *tiku_ra8p1_fault_last(void);

/** @brief Clear the stored record's magic and count. */
void tiku_ra8p1_fault_clear(void);

/**
 * @brief Name of a fault kind, for printing.
 *
 * @param kind  A tiku_ra8p1_fault_kind_t
 * @return Static name; "unknown" for any other value
 */
const char *tiku_ra8p1_fault_kind_name(uint32_t kind);

#endif /* TIKU_RA8P1_FAULT_ARCH_H_ */
