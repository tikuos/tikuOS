/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - S31 LP noise source with hardware health-test checks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trng_arch.h"
#include "tiku_runtime_arch.h"
#include <kernel/threads/tiku_thread.h>
#include <hal/tiku_clock_hal.h>
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
static unsigned ready, failed, have_last;
static uint32_t last;
/** @brief Verify clock, sampling configuration, health tests and error status. */
static int healthy(void)
{
    return (REG(0x2071003cu) & 0xc0000000u) == 0x40000000u &&
        (REG(0x208140fcu) & (1u<<28)) && REG(0x20814000u) == 0xe0907e10u &&
        !(REG(0x20814004u) & 0x5e000000u) && !(REG(0x2081402cu) & 1u);
}
/** @brief Latch a source fault and stop sampling. */
static void fault(void)
{ failed = 1; REG(0x20814000u) &= ~(1u<<31); }
int tiku_s31_trng_init(void)
{
    if (tiku_s31_in_trap || !tiku_thread_in_kernel() || failed) return -1;
    if (ready) return healthy() ? 0 : (fault(), -1);
    REG(0x2071003cu) |= 1u<<30;
    REG(0x2071003cu) |= 1u<<31; REG(0x2071003cu) &= ~(1u<<31);
    REG(0x208140fcu) |= 1u<<28;
    REG(0x20814000u) = 0xe0907e10u;
    REG(0x20814004u) = 2048; REG(0x20814030u) = 1; REG(0x20814004u) = 2049;
    tiku_clock_arch_delay(10000);
    if (!healthy()) { fault(); return -1; }
    unsigned changes = 0;
    last = REG(0x20814048u);
    for (unsigned i = 0; i < 32; i++) {
        tiku_clock_arch_delay(2);
        uint32_t word = REG(0x20814048u);
        changes += word != last; last = word;
    }
    if (changes != 32 || !healthy()) { fault(); return -1; }
    ready = have_last = 1;
    return 0;
}
int tiku_trng_arch_read_bytes(uint8_t *out, size_t length)
{
    if ((!out && length) || length > 4096 || tiku_s31_in_trap || !ready || failed) return -1;
    for (size_t i = 0; i < length; i += 4) {
        uint32_t state;
        __asm__ volatile("csrrci %0,mstatus,8" : "=r"(state) :: "memory");
        tiku_clock_arch_delay(2);
        uint32_t word = REG(0x20814048u);
        if (!healthy() || (have_last && word == last)) fault();
        last = word; have_last = 1;
        if (state & 8u) __asm__ volatile("csrsi mstatus,8" ::: "memory");
        if (failed) {
            volatile uint8_t *wipe = out;
            for (size_t j = 0; j < length; j++) wipe[j] = 0;
            return -1;
        }
        for (unsigned j = 0; j < 4 && i+j < length; j++) out[i+j] = word >> (8*j);
    }
    return 0;
}
int tiku_trng_arch_read_u32(uint32_t *out)
{ return tiku_trng_arch_read_bytes((uint8_t *)out, sizeof *out); }
