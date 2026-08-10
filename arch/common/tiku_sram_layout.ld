/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sram_layout.ld - the ONE SRAM-tier carve derivation (ARM).
 *
 * Every ARM platform hands the tier allocator whatever its chosen bank has
 * left after the statics; this fragment holds that arithmetic once.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Input/output contract (same INCLUDE pattern as tiku_nvm_layout.ld):
 *
 *   inputs (define BEFORE the INCLUDE):
 *     __tiku_tier_span_start      first byte past the bank's statics
 *     __tiku_tier_span_limit      hard upper edge of the span: the bank end,
 *                                 or the stack top less the MPU stack budget
 *     __tiku_tier_reserve_default headroom kept below the limit (0 where
 *                                 nothing lives above the span)
 *     __tiku_tier_floor_default   floor used when the build passes none
 *
 *   optional (--defsym from the build, not the device script):
 *     __tier_sram_floor           TIKU_TIER_SRAM_MIN from the make line --
 *                                 the floor's single author
 *     __tier_reserve_override     squeeze knob for the link-contract tests
 *
 *   outputs:
 *     __tier_sram_start / __tier_sram_end   the span tiku_tier.c adopts
 *
 * DEFINED() is required around each --defsym pickup: a bare --defsym symbol
 * is not in scope for a script expression and errors out as undefined.
 */

__tiku_tier_reserve = DEFINED(__tier_reserve_override)
                    ? __tier_reserve_override : __tiku_tier_reserve_default;
__tiku_tier_floor   = DEFINED(__tier_sram_floor)
                    ? __tier_sram_floor : __tiku_tier_floor_default;

__tier_sram_start = __tiku_tier_span_start;
__tier_sram_end   = __tiku_tier_span_limit - __tiku_tier_reserve;

/* Ordering first: linker arithmetic is unsigned, so a span whose statics
 * overran the limit would wrap to an enormous size and sail past the floor
 * check below.  Compare the edges before deriving anything from them. */
ASSERT(__tier_sram_start <= __tier_sram_end,
       "SRAM tier: statics overran the span (start past end)")

ASSERT((__tier_sram_end - __tier_sram_start) >= __tiku_tier_floor,
       "SRAM tier fell below its floor -- a static buffer grew into the span; shrink it or lower TIKU_TIER_SRAM_MIN")
