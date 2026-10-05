/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_peek_poke.inl - PEEK and POKE byte memory access.
 *
 * On MSP430 they go straight to a volatile pointer cast from the address;
 * every other build reads and writes a 256-byte simulated map, which the host
 * harness tests.  Compiles to nothing when TIKU_BASIC_PEEK_POKE_ENABLE is 0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_PEEK_POKE_ENABLE

#ifdef PLATFORM_MSP430

/*---------------------------------------------------------------------------*/
/* TARGET (MSP430): DIRECT REGISTER ACCESS                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief PEEK: read the byte at @p addr through a volatile pointer.
 *
 * The address is not validated, so SFRs and any other location are reachable.
 */
static long
basic_peek(long addr)
{
    BASIC_RECLAIM_EXTERNAL();
    return (long)(*(volatile uint8_t *)(unsigned long)addr);
}

/** @brief POKE: write the low byte of @p val to @p addr, unvalidated. */
static void
basic_poke(long addr, long val)
{
    BASIC_RECLAIM_EXTERNAL();
    *(volatile uint8_t *)(unsigned long)addr = (uint8_t)val;
}

#else /* !PLATFORM_MSP430 */

/*---------------------------------------------------------------------------*/
/* OTHER BUILDS: 256-BYTE SIMULATED MAP                                      */
/*---------------------------------------------------------------------------*/

/* PEEK / POKE on a simulated map: an address wraps modulo 256 and touches no
 * real memory, so the host harness round-trips them without a wild
 * pointer. */
static uint8_t basic_peek_simbuf[256];

/** @brief PEEK on the simulated map: the byte at @p addr modulo 256. */
static long
basic_peek(long addr)
{
    return (long)basic_peek_simbuf[(unsigned long)addr & 0xFFu];
}

/** @brief POKE on the simulated map: store a byte at @p addr modulo 256. */
static void
basic_poke(long addr, long val)
{
    basic_peek_simbuf[(unsigned long)addr & 0xFFu] = (uint8_t)val;
}

#endif /* PLATFORM_MSP430 */

#endif /* TIKU_BASIC_PEEK_POKE_ENABLE */
