/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_prng.inl - linear-congruential RND() generator.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Lazy-seeded from tiku_clock_time() on first call; uses the
 * Numerical Recipes LCG constants.  Output draws from the high 16
 * bits of the state, which have the best statistical properties of
 * an LCG output.  Cheap on MSP430 (one mul, one add, one shift) and
 * good enough for casual BASIC games / test data.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Return a pseudo-random integer in [0, @p n).
 *
 * @param n  Upper bound (exclusive); @p n <= 0 returns 0.
 */
static long
basic_rnd(long n)
{
    /* Lazy seeding -- avoids paying clock-read cost when RND is
     * never called.  Seed mixes the kernel tick with a small
     * constant so 0-tick boots don't all start with the same
     * sequence. */
    if (!basic_prng_seeded) {
        basic_prng_state = (uint32_t)tiku_clock_time() * 2654435761UL +
                            0x9E3779B9UL;
        basic_prng_seeded = 1;
    }
    /* LCG step (Numerical Recipes constants): cheap on MSP430. */
    basic_prng_state = basic_prng_state * 1664525UL + 1013904223UL;
    if (n <= 0) {
        return 0;
    }
    return (long)((basic_prng_state >> 16) % (uint32_t)n);
}
