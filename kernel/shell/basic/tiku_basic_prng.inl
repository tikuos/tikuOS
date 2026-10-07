/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_prng.inl - linear-congruential RND() generator.
 *
 * Seeded at the first call from the kernel tick and the boot count.  Each
 * call steps the LCG and scales its 32-bit state to the requested range.
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
    /* The seed mixes the kernel tick with the boot count, so boots that
     * reach their first RND at the same tick differ. */
    if (!basic_prng_seeded) {
        uint32_t boots = 0u;
#if TIKU_BASIC_VFS_ENABLE
        char cnt[12];
        int  len = tiku_vfs_read("/sys/boot/count", cnt, sizeof cnt - 1u);
        if (len > 0) {
            cnt[len] = '\0';
            boots = (uint32_t)strtoul(cnt, NULL, 10);
        }
#endif
        basic_prng_state = (uint32_t)tiku_clock_time() * 2654435761UL +
                           (boots + 1u) * 0x9E3779B9UL;
        basic_prng_seeded = 1;
    }
    /* LCG step with the Numerical Recipes constants. */
    basic_prng_state = basic_prng_state * 1664525UL + 1013904223UL;
    if (n <= 0) {
        return 0;
    }
    return (long)(((uint64_t)basic_prng_state * (uint32_t)n) >> 32);
}
