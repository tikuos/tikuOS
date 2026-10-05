/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sram_layout.ld - SRAM-tier span derivation shared by the ARM ports.
 *
 * The tier allocator gets what the chosen SRAM bank has left after the
 * statics, less a reserve; the link fails when that is below the floor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Input/output contract (same INCLUDE pattern as tiku_nvm_layout.ld):
 *
 *   inputs (defined before the INCLUDE):
 *     __tiku_tier_span_start      first byte past the bank's statics
 *     __tiku_tier_span_limit      hard upper edge of the span: the bank end,
 *                                 or the stack top less the MPU stack budget
 *     __tiku_tier_reserve_default headroom kept below the limit (0 where
 *                                 nothing lives above the span)
 *     __tiku_tier_floor_default   floor used when the build passes none
 *
 *   optional (--defsym from the build, not the device script):
 *     __tier_sram_floor           TIKU_TIER_SRAM_MIN from the make line
 *     __tier_reserve_override     replaces the reserve; used by the
 *                                 link-contract tests
 *
 *   outputs:
 *     __tier_sram_start / __tier_sram_end   the span tiku_tier.c adopts
 *
 * Each optional input is read through DEFINED(), so a build that does not
 * pass it links with the default; a bare reference to an absent symbol fails
 * the link as undefined.  DEFINED() sees a --defsym only when ld receives it
 * before -T, which is the order the gcc driver passes them in.
 */

__tiku_tier_reserve = DEFINED(__tier_reserve_override)
                    ? __tier_reserve_override : __tiku_tier_reserve_default;
__tiku_tier_floor   = DEFINED(__tier_sram_floor)
                    ? __tier_sram_floor : __tiku_tier_floor_default;

__tier_sram_start = __tiku_tier_span_start;
__tier_sram_end   = __tiku_tier_span_limit - __tiku_tier_reserve;

/* Linker arithmetic is unsigned: when the statics overrun the limit, the
 * span size wraps to a huge value and passes the floor check below.  This
 * comparison of the edges cannot wrap and fails the link in that case. */
ASSERT(__tier_sram_start <= __tier_sram_end,
       "SRAM tier: statics overran the span (start past end)")

ASSERT((__tier_sram_end - __tier_sram_start) >= __tiku_tier_floor,
       "SRAM tier fell below its floor -- a static buffer grew into the span; shrink it or lower TIKU_TIER_SRAM_MIN")
