/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fault_arch.h - RA8P1 fault record.
 *
 * The handlers dump what they know, keep a record that outlives the reset they
 * then force, and reset -- which only became a recovery rather than a death
 * once R6 put the image in MRAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_FAULT_ARCH_H_
#define TIKU_RA8P1_FAULT_ARCH_H_

#include <stdint.h>

/** @brief Marks a record as written rather than left over as SRAM noise. */
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
    uint32_t lr;        /**< stacked LR, same caveat                        */
    uint32_t psr;       /**< stacked xPSR                                   */
    uint32_t sp;        /**< the frame's own address                        */
    uint32_t exc;       /**< EXC_RETURN, naming frame type and stack        */
    uint32_t msp;       /**< both stack pointers at record time, because a  */
    uint32_t psp;       /**< shifted frame is visible only against them     */
    uint32_t raw[12];   /**< the frame area verbatim, for when the eight    */
                        /**< named fields are themselves the corruption    */
} tiku_ra8p1_fault_record_t;

/** @brief Which handler ran. */
typedef enum {
    TIKU_RA8P1_FAULT_HARD  = 0,
    TIKU_RA8P1_FAULT_MEM   = 1,
    TIKU_RA8P1_FAULT_BUS   = 2,
    TIKU_RA8P1_FAULT_USAGE = 3,
    TIKU_RA8P1_FAULT_UNEXPECTED = 4,
} tiku_ra8p1_fault_kind_t;

/**
 * @brief Enable the configurable faults so they do not escalate to HardFault.
 *
 * A MemManage that arrives as MemManage names its own address in MMFAR; the
 * same access escalated to HardFault does not.
 */
void tiku_ra8p1_fault_init(void);

/**
 * @brief Record a fault and reset; the shims and the default handler land here.
 *
 * @param frame       Stacked exception frame, or NULL if the push failed
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

/** @brief Forget the stored record. */
void tiku_ra8p1_fault_clear(void);

/** @brief Name for a kind, for printing. @param kind Kind @return Static name */
const char *tiku_ra8p1_fault_kind_name(uint32_t kind);

#endif /* TIKU_RA8P1_FAULT_ARCH_H_ */
