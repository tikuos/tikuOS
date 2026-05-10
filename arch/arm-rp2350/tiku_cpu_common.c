/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.c - RP2350 common helpers (delays, unique-id, reset cause)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_common.h"
#include "tiku_rp2350_regs.h"
#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Delays via TIMER0's 1 us tick                                             */
/*---------------------------------------------------------------------------*/

/* Read the lower 32 bits of TIMER0 atomically. The full 64-bit read
 * needs paired TIMELR/TIMEHR access; for delays the 32-bit lower
 * counter is more than enough — it wraps every ~71 minutes. */
static inline uint32_t rp2350_us_now(void) {
    return _RP2350_REG(RP2350_TIMER0_TIMERAWL);
}

void tiku_cpu_rp2350_delay_us(unsigned int us) {
    if (us == 0U) {
        return;
    }
    uint32_t start = rp2350_us_now();
    while ((rp2350_us_now() - start) < (uint32_t)us) {
        /* spin */
    }
}

void tiku_cpu_rp2350_delay_ms(unsigned int ms) {
    /* Decompose to keep within the 32-bit microsecond window per
     * call (max ~71 minutes; 1000 * 65535 = ~65 s comfortably fits). */
    while (ms > 0U) {
        unsigned int chunk = (ms > 1000U) ? 1000U : ms;
        tiku_cpu_rp2350_delay_us(chunk * 1000U);
        ms -= chunk;
    }
}

/*---------------------------------------------------------------------------*/
/* Unique ID                                                                 */
/*---------------------------------------------------------------------------*/

/*
 * Reading the actual flash chip's unique ID requires temporarily
 * disabling XIP and issuing a 0x4B command to the QSPI controller —
 * out of scope for the first port. We synthesise a stable 8-byte ID
 * from the addresses of three linker symbols: this is unique per
 * build (the linker layout is deterministic across reboots of the
 * same image). Programs that need a true silicon ID should add a
 * proper flash-readback driver later.
 */
extern char __sram_start;
extern char __flash_start;
extern char __vectors_start;

uint8_t tiku_cpu_rp2350_unique_id(uint8_t *buf, uint8_t len) {
    if (buf == NULL || len == 0U) {
        return 0U;
    }
    static const uint8_t magic[8] = {
        'r', 'p', '2', '3', '5', '0', 'O', 'S'
    };
    uint8_t n = (len > 8U) ? 8U : len;
    uint8_t i;
    /* XOR magic with low bits of the linker-symbol addresses so two
     * different builds produce different IDs. */
    uintptr_t a = (uintptr_t)&__sram_start;
    uintptr_t b = (uintptr_t)&__flash_start;
    uintptr_t c = (uintptr_t)&__vectors_start;
    for (i = 0; i < n; i++) {
        uint8_t mix = (uint8_t)((a >> (i * 4)) ^ (b >> i) ^ (c >> (i * 2)));
        buf[i] = magic[i] ^ mix;
    }
    return n;
}

/*---------------------------------------------------------------------------*/
/* Reset reason                                                              */
/*---------------------------------------------------------------------------*/

uint16_t tiku_cpu_rp2350_reset_reason(void) {
    /* WD_REASON: bit 0 = TIMEOUT, bit 1 = FORCE. Higher bits report
     * other reset sources on a future revision. We map the low byte
     * directly so callers see a 16-bit value compatible with MSP430
     * SYSRSTIV. 0 means cold boot. */
    return (uint16_t)(_RP2350_REG(RP2350_WD_REASON) & 0xFFU);
}
