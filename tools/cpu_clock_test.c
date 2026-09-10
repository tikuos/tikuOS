/* Host checks for the production Nordic early-boot clock preference.
 * No MMIO, physical storage writes, or clock changes.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../arch/nordic/tiku_cpu_settings_arch.c"

static int commits, fail_write;

uint8_t tiku_persist_cell_valid(const tiku_persist_cell_t *c)
{
    return *c->gate == c->key;
}

void tiku_persist_cell_commit(const tiku_persist_cell_t *c,
                              const void *src, uint16_t len)
{
    commits++;
    if (fail_write) return;
    memcpy(c->data, src, len);
    *c->gate = c->key;
}

int main(void)
{
    unsigned long fallback = TIKU_NORDIC_CPU_MHZ * 1000000UL;
    assert(tiku_cpu_nordic_target_hz() == fallback);
    assert(commits == 0); /* early boot cannot initialize/write storage */
    assert(tiku_cpu_nordic_target_set(64000000UL) == 0);
    assert(tiku_cpu_nordic_target_hz() == 64000000UL);
    assert(tiku_cpu_nordic_target_set(128000000UL) == 0);
    assert(tiku_cpu_nordic_target_hz() == 128000000UL);
    assert(tiku_cpu_nordic_target_set(96000000UL) == -1);
    assert(tiku_cpu_nordic_target_set(0) == -1);
    assert(commits == 2);
    /* Simulate a bad value or incomplete commit observed after reset. */
    cpu_target_hz = 1;
    assert(tiku_cpu_nordic_target_hz() == fallback);
    cpu_target_hz = 64000000UL;
    cpu_target_cell_gate = 0;
    assert(tiku_cpu_nordic_target_hz() == fallback);
    assert(commits == 2);
    /* Readback must not mistake fallback for a successful save. */
    fail_write = 1;
    assert(tiku_cpu_nordic_target_set(fallback) == -1);
    fail_write = 0;
    assert(tiku_cpu_nordic_target_set(64000000UL) == 0);
    assert(tiku_cpu_nordic_target_hz() == 64000000UL);
    puts("PASS: clock targets, defaults, invalid state and failed saves");
    return 0;
}
