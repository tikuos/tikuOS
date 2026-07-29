/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_peek_poke.inl - PEEK and POKE byte memory access.
 *
 * On target these go straight to a volatile pointer cast from the address; on the
 * host harness they bounce off a small simulated buffer so tests round-trip
 * without a wild pointer.  Compiles to nothing when the feature is off.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_PEEK_POKE_ENABLE

#ifdef PLATFORM_MSP430

/*---------------------------------------------------------------------------*/
/* TARGET (MSP430): DIRECT REGISTER ACCESS                                   */
/*---------------------------------------------------------------------------*/

/* On the actual target, PEEK / POKE are byte access through a
 * volatile pointer cast from the integer address.  This is exactly
 * the operation users want for SFR pokes -- we don't try to validate
 * the address. */
static long
basic_peek(long addr)
{
    return (long)(*(volatile uint8_t *)(unsigned long)addr);
}

static void
basic_poke(long addr, long val)
{
    *(volatile uint8_t *)(unsigned long)addr = (uint8_t)val;
}

#else /* !PLATFORM_MSP430 */

/*---------------------------------------------------------------------------*/
/* HOST HARNESS: 256-BYTE SIMULATED MAP                                      */
/*---------------------------------------------------------------------------*/

/* On the host harness, route PEEK / POKE through a small simulated
 * memory map so tests can round-trip without crashing on a wild
 * pointer.  The map wraps every 256 bytes -- enough to verify the
 * keyword + parser + dispatch wiring. */
static uint8_t basic_peek_simbuf[256];

static long
basic_peek(long addr)
{
    return (long)basic_peek_simbuf[(unsigned long)addr & 0xFFu];
}

static void
basic_poke(long addr, long val)
{
    basic_peek_simbuf[(unsigned long)addr & 0xFFu] = (uint8_t)val;
}

#endif /* PLATFORM_MSP430 */

#endif /* TIKU_BASIC_PEEK_POKE_ENABLE */
