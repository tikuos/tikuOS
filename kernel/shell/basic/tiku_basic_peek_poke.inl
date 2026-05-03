/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_peek_poke.inl - PEEK / POKE byte memory access.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * On MSP430 these go straight to a volatile uint8_t pointer cast
 * from the integer address (the canonical SFR poke).  On the host
 * harness they bounce off a 256-byte simulated buffer so tests can
 * round-trip without crashing on a wild pointer.  The whole file
 * compiles to nothing when TIKU_BASIC_PEEK_POKE_ENABLE is 0.
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
