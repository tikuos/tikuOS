/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_prng.inl - linear-congruential RND() generator.
 *
 * Lazily seeded from the clock on first call.  Output draws from the high 16 bits
 * of the state, which are the best-behaved of an LCG, and costs one multiply, one
 * add and one shift.
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
