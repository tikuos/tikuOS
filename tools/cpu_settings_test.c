/* Portable clock-policy tests with no physical device writes.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdio.h>
#include "../kernel/cpu/tiku_cpu_settings.c"

static unsigned long current = 150000000UL;
static const unsigned long choices[] = {12000000UL, 150000000UL, 1000000000UL};
static int changes, writes, fail_write, fixed;

unsigned long tiku_cpu_mclk_hz(void) { return current; }
unsigned long tiku_cpu_freq_available(unsigned int i)
{
    if (fixed) return i == 0 ? current : 0;
    return i < 3 ? choices[i] : 0;
}
void tiku_cpu_freq_boot_set(unsigned long hz) { changes++; current = hz; }
uint8_t tiku_persist_cell_valid(const tiku_persist_cell_t *c)
{
    return *c->gate == c->key;
}
void tiku_persist_cell_commit(const tiku_persist_cell_t *c,
                              const void *src, uint16_t len)
{
    writes++;
    if (!fail_write) {
        memcpy(c->data, src, len);
        *c->gate = c->key;
    }
}

int main(void)
{
    assert(tiku_cpu_settings_save(12000000UL) == -1); /* not booted */
    tiku_cpu_settings_boot();
    assert(changes == 0 && writes == 0);
    assert(tiku_cpu_settings_target() == 150000000UL);
    assert(tiku_cpu_settings_save(12000000UL) == 0);
    assert(current == 150000000UL && changes == 0);
    assert(tiku_cpu_settings_target() == 12000000UL);
    tiku_cpu_settings_boot(); /* must not become a runtime clock setter */
    assert(changes == 0);
    ready = 0; /* simulated reboot: persistent bytes remain */
    tiku_cpu_settings_boot();
    assert(current == 12000000UL && changes == 1);
    assert(tiku_cpu_settings_save(1000000000UL) == 0);
    assert(current == 12000000UL);
    fixed = 1;
    assert(tiku_cpu_settings_save(12000000UL) == -1);
    fixed = 0;
    assert(tiku_cpu_settings_save(999999999UL) == -1);
    fail_write = 1;
    assert(tiku_cpu_settings_save(150000000UL) == -1);
    fail_write = 0;
    /* Both a corrupt gate and a torn value must leave the boot clock alone. */
    saved_clock_cell_gate = 0;
    ready = 0;
    current = 150000000UL;
    tiku_cpu_settings_boot();
    assert(changes == 1 && current == 150000000UL);
    assert(tiku_cpu_settings_save(12000000UL) == 0);
    saved_clock.inverse ^= 1;
    ready = 0;
    tiku_cpu_settings_boot();
    assert(changes == 1 && current == 150000000UL);
    puts("PASS: boot-only apply, discrete rates, torn state, rejected saves");
    return 0;
}
