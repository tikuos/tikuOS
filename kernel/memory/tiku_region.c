/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region.c - memory region registry.
 *
 * A boot-time registry of the platform's physical memory map, so a subsystem can
 * check its buffers sit in the right memory type and claim tracking can reject
 * two subsystems that overlap -- both caught at init rather than at run time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_mem.h"
#include <stdint.h>
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* INTERNAL STATE                                                            */
/*---------------------------------------------------------------------------*/

/** Pointer to platform-provided region table (lives in flash) */
static const tiku_mem_region_t *region_table;

/** Number of entries in the region table */
static tiku_mem_arch_size_t region_count;

/** Claimed region tracking array (lives in SRAM) */
static tiku_mem_claimed_t claimed[TIKU_REGION_MAX_CLAIMS];

/** Number of active claims */
static tiku_mem_arch_size_t claimed_count;

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Check if two address ranges overlap.
 *
 * They overlap exactly when a < b + b_size and b < a + a_size.  All arithmetic
 * is in uintptr_t, which avoids undefined pointer comparison and handles the
 * full address space on 16-bit targets.
 *
 * @param a_base  Start of first range
 * @param a_size  Size of first range
 * @param b_base  Start of second range
 * @param b_size  Size of second range
 * @return 1 if the ranges overlap, 0 otherwise
 */
static int ranges_overlap(const uint8_t *a_base,
                           tiku_mem_arch_size_t a_size,
                           const uint8_t *b_base,
                           tiku_mem_arch_size_t b_size)
{
    uintptr_t a_start = (uintptr_t)a_base;
    uintptr_t b_start = (uintptr_t)b_base;

    /*
     * Overflow-safe overlap check: avoid computing start + size which
     * can wrap on 16-bit targets when a region reaches address 0xFFFF.
     * Two ranges [a, a+as) and [b, b+bs) overlap iff the gap between
     * their starts is smaller than the other range's size.
     */
    if (a_start >= b_start) {
        return (a_start - b_start) < (uintptr_t)b_size;
    } else {
        return (b_start - a_start) < (uintptr_t)a_size;
    }
}

/**
 * @brief Check if a range falls within any declared region (type-agnostic)
 *
 * Used by tiku_region_claim to verify that a range falls within known
 * memory without caring about the region type. A range is "known" if
 * it is entirely contained within at least one declared region.
 *
 * @param ptr   Start of the range
 * @param size  Size of the range in bytes
 * @return 1 if contained in some declared region, 0 otherwise
 */
static int region_is_known(const uint8_t *ptr, tiku_mem_arch_size_t size)
{
    tiku_mem_arch_size_t i;
    uintptr_t range_start;
    uintptr_t reg_start;

    range_start = (uintptr_t)ptr;

    /* Overflow check: if ptr + size wraps around, the range is invalid */
    if (range_start + (uintptr_t)size < range_start) {
        return 0;
    }

    for (i = 0; i < region_count; i++) {
        reg_start = (uintptr_t)region_table[i].base;

        /*
         * Overflow-safe containment: compute the buffer's offset from
         * the region base and check that offset + size fits within the
         * region size, without ever computing reg_start + reg_size.
         */
        if (range_start >= reg_start) {
            uintptr_t offset = range_start - reg_start;

            if (offset < (uintptr_t)region_table[i].size &&
                (uintptr_t)size <=
                    (uintptr_t)region_table[i].size - offset) {
                return 1;
            }
        }
    }

    return 0;
}

/*---------------------------------------------------------------------------*/
/* REGION FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the region registry with the platform's memory map.
 *
 * Keeps a pointer to the platform table rather than copying it, and rejects a
 * table whose regions overlap -- overlap would make a containment check
 * ambiguous, letting one buffer appear to be in two region types at once.
 *
 * @param table  Platform's const region descriptor array
 * @param count  Number of entries in the table
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID on bad args or
 *         overlapping regions
 */
tiku_mem_err_t tiku_region_init(const tiku_mem_region_t *table,
                                 tiku_mem_arch_size_t count)
{
    tiku_mem_arch_size_t i;
    tiku_mem_arch_size_t j;

    if (table == NULL || count == 0 || count > TIKU_REGION_MAX_REGIONS) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Check for pairwise overlaps in the platform's region table */
    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            if (ranges_overlap(table[i].base, table[i].size,
                               table[j].base, table[j].size)) {
                return TIKU_MEM_ERR_INVALID;
            }
        }
    }

    region_table = table;
    region_count = count;

    /* Zero the claimed regions array */
    for (i = 0; i < TIKU_REGION_MAX_CLAIMS; i++) {
        claimed[i].base     = NULL;
        claimed[i].size     = 0;
        claimed[i].owner_id = 0;
    }
    claimed_count = 0;

    return TIKU_MEM_OK;
}

/**
 * @brief Check if a memory range is within a region of the expected type.
 *
 * A linear scan; the whole range must sit in one matching region.  Arithmetic
 * is in uintptr_t, since comparing pointers into different objects is
 * undefined, and a wrapped ptr + size is rejected rather than passing.
 *
 * @param ptr            Start of the range
 * @param size           Size of the range in bytes
 * @param expected_type  Required region type
 * @return 1 if contained, 0 otherwise
 */
tiku_mem_err_t tiku_region_contains(const uint8_t *ptr,
                                     tiku_mem_arch_size_t size,
                                     tiku_mem_region_type_t expected_type)
{
    tiku_mem_arch_size_t i;
    uintptr_t range_start;
    uintptr_t reg_start;

    if (ptr == NULL || size == 0) {
        return 0;
    }

    range_start = (uintptr_t)ptr;

    /* Overflow check: ptr + size must not wrap around */
    if (range_start + (uintptr_t)size < range_start) {
        return 0;
    }

    for (i = 0; i < region_count; i++) {
        if (region_table[i].type != expected_type) {
            continue;
        }

        reg_start = (uintptr_t)region_table[i].base;

        /*
         * Overflow-safe containment: instead of computing
         * reg_end = reg_start + reg_size (which wraps past 0xFFFF on
         * 16-bit targets when the region reaches the top of the address
         * space), compute the buffer's offset from the region base and
         * verify that offset + size fits within the region size.
         */
        if (range_start >= reg_start) {
            uintptr_t offset = range_start - reg_start;

            if (offset < (uintptr_t)region_table[i].size &&
                (uintptr_t)size <=
                    (uintptr_t)region_table[i].size - offset) {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * @brief Claim a memory range for a subsystem.
 *
 * Records ownership so an overlapping claim is caught.  This is the question
 * _contains() does not answer: a buffer can be in the right memory type and
 * still collide with another subsystem's.
 *
 * @param ptr       Start of the range to claim
 * @param size      Size of the range in bytes
 * @param owner_id  Identifier of the claiming subsystem
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_INVALID if the range is
 *         outside every region, TIKU_MEM_ERR_FULL if the claim table is
 *         full, or TIKU_MEM_ERR_BUSY on an overlapping claim
 */
tiku_mem_err_t tiku_region_claim(const uint8_t *ptr,
                                  tiku_mem_arch_size_t size,
                                  uint8_t owner_id)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    tiku_mem_arch_size_t i;

    if (ptr == NULL || size == 0) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Verify the range falls within a declared region (any type) */
    if (!region_is_known(ptr, size)) {
        return TIKU_MEM_ERR_INVALID;
    }

    /* Check for overlap with existing claims */
    for (i = 0; i < TIKU_REGION_MAX_CLAIMS; i++) {
        if (claimed[i].size == 0) {
            continue;
        }

        if (ranges_overlap(ptr, size, claimed[i].base, claimed[i].size)) {
            return TIKU_MEM_ERR_INVALID;
        }
    }

    /* Find first empty slot */
    for (i = 0; i < TIKU_REGION_MAX_CLAIMS; i++) {
        if (claimed[i].size == 0) {
            claimed[i].base     = ptr;
            claimed[i].size     = size;
            claimed[i].owner_id = owner_id;
            claimed_count++;
            return TIKU_MEM_OK;
        }
    }

    return TIKU_MEM_ERR_FULL;
}

/**
 * @brief Release a previously claimed memory range
 *
 * Finds the claim by matching its base pointer (compared as uintptr_t
 * to avoid pointer comparison UB) and clears the slot.
 *
 * @param ptr  Base pointer of the claim to release
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_NOT_FOUND if not found
 */
tiku_mem_err_t tiku_region_unclaim(const uint8_t *ptr)
{
    TIKU_MEM_KERNEL_ONLY(TIKU_MEM_ERR_INVALID);
    tiku_mem_arch_size_t i;

    if (ptr == NULL) {
        return TIKU_MEM_ERR_NOT_FOUND;
    }

    for (i = 0; i < TIKU_REGION_MAX_CLAIMS; i++) {
        if (claimed[i].size != 0 &&
            (uintptr_t)claimed[i].base == (uintptr_t)ptr) {
            claimed[i].base     = NULL;
            claimed[i].size     = 0;
            claimed[i].owner_id = 0;
            claimed_count--;
            return TIKU_MEM_OK;
        }
    }

    return TIKU_MEM_ERR_NOT_FOUND;
}

/**
 * @brief Look up the region type for a single address
 *
 * Scans the region table to find which region contains the address
 * and returns its type via the output parameter. Useful for debug
 * and diagnostic printing.
 *
 * @param ptr       Address to look up
 * @param out_type  Output: type of the containing region
 * @return TIKU_MEM_OK on success, TIKU_MEM_ERR_NOT_FOUND if the
 *         address is not in any declared region
 */
tiku_mem_err_t tiku_region_get_type(const uint8_t *ptr,
                                     tiku_mem_region_type_t *out_type)
{
    tiku_mem_arch_size_t i;
    uintptr_t addr;
    uintptr_t reg_start;

    if (ptr == NULL || out_type == NULL) {
        return TIKU_MEM_ERR_NOT_FOUND;
    }

    addr = (uintptr_t)ptr;

    for (i = 0; i < region_count; i++) {
        reg_start = (uintptr_t)region_table[i].base;

        if (addr >= reg_start &&
            (addr - reg_start) < (uintptr_t)region_table[i].size) {
            *out_type = region_table[i].type;
            return TIKU_MEM_OK;
        }
    }

    return TIKU_MEM_ERR_NOT_FOUND;
}
