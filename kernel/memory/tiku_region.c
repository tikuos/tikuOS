/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region.c - Memory region registry implementation
 *
 * Implements a boot-time registry for the platform's physical memory
 * map. Subsystems use it to verify that their buffers reside in the
 * correct memory type (SRAM for arenas, NVM for persistent storage)
 * and to detect conflicting buffer assignments via claim tracking.
 *
 * Why a region registry:
 *   On microcontrollers, accidentally placing an arena buffer in NVM
 *   (or a persistent store in SRAM) is a silent, hard-to-diagnose bug.
 *   The region registry catches these mistakes at init time with a
 *   simple address-range check, before the system runs. The cost is
 *   a small static table and a few linear scans during boot — nothing
 *   on the hot path.
 *
 * Why overlap detection:
 *   Two subsystems using overlapping buffers corrupt each other silently.
 *   The claim table tracks which ranges are owned, and rejects overlaps
 *   at registration time. On a small MCU this is far better than chasing
 *   memory corruption at runtime.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
 * @brief Check if two address ranges overlap
 *
 * Two ranges [a, a+a_size) and [b, b+b_size) overlap if and only if
 * a < b + b_size AND b < a + a_size. All arithmetic is done in
 * uintptr_t to avoid pointer comparison undefined behavior and to
 * handle the full address space correctly on 16-bit targets.
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
 * @brief Initialize the region registry with the platform's memory map
 *
 * Stores a pointer to the platform-provided region table (expected to
 * live in flash — no copy is made). Zeroes the claimed regions array.
 * Validates that no two regions in the table overlap, returning an
 * error if they do.
 *
 * Why validate overlaps at init:
 *   Overlapping region definitions would make containment checks
 *   ambiguous — a buffer could appear to be in two different region
 *   types simultaneously. Catching this at boot is cheap and prevents
 *   subtle misclassification bugs.
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
 * @brief Check if a memory range is within a region of the expected type
 *
 * Linear scan through the region table. Returns 1 if the entire range
 * [ptr, ptr+size) is contained within a single region whose type
 * matches expected_type. Returns 0 otherwise.
 *
 * Why we check for pointer arithmetic overflow:
 *   On a 16-bit MCU, ptr + size can wrap around to zero if the range
 *   extends past the end of the address space. A wrapped range would
 *   pass the containment check erroneously. We detect this by checking
 *   that ptr + size >= ptr in uintptr_t arithmetic.
 *
 * Why uintptr_t for comparisons:
 *   Comparing pointers that do not point into the same array is
 *   undefined behavior in C. Casting to uintptr_t converts to an
 *   integer type where comparison is always well-defined. This is
 *   essential for a memory registry that compares addresses across
 *   different memory regions.
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
 * @brief Claim a memory range for a subsystem
 *
 * Registers ownership of a range so overlapping claims can be detected.
 * The range must fall within a declared region (any type — we just need
 * to know it's valid memory). Then we check the claimed table for
 * overlaps with existing claims. If none, the claim is stored in the
 * first empty slot.
 *
 * Why claim exists separately from region_contains:
 *   region_contains answers "is this the right type of memory?"
 *   Claim answers "is anyone else already using this memory?" Both
 *   checks are needed: a buffer in the right memory type can still
 *   collide with another subsystem's buffer.
 *
 * @param ptr       Start of the range to claim
 * @param size      Size of the range in bytes
 * @param owner_id  Identifier of the claiming subsystem
 * @return TIKU_MEM_OK, TIKU_MEM_ERR_INVALID, or TIKU_MEM_ERR_FULL
 */
tiku_mem_err_t tiku_region_claim(const uint8_t *ptr,
                                  tiku_mem_arch_size_t size,
                                  uint8_t owner_id)
{
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
