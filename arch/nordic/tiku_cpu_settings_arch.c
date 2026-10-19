/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_settings_arch.c - nRF54L next-boot CPU clock preference.
 *
 * Keeps the 64/128 MHz rate the next boot applies in a durable persist cell;
 * the build default (TIKU_NORDIC_CPU_MHZ) applies when none is saved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <arch/nordic/tiku_cpu_freq_boot_arch.h>
#include <kernel/memory/tiku_mem.h>

/* Build-default core rate in MHz: 64 or 128. */
#ifndef TIKU_NORDIC_CPU_MHZ
#define TIKU_NORDIC_CPU_MHZ 128
#endif
#if (TIKU_NORDIC_CPU_MHZ != 64) && (TIKU_NORDIC_CPU_MHZ != 128)
#error "TIKU_NORDIC_CPU_MHZ must be 64 or 128"
#endif

#if defined(TIKU_MINIMAL)

/* A MINIMAL=1 image links no persist store: every boot applies the build
 * default, and a save is refused. */

unsigned long tiku_cpu_nordic_target_hz(void)
{
    return TIKU_NORDIC_CPU_MHZ * 1000000UL;
}

int tiku_cpu_nordic_target_set(unsigned long hz)
{
    (void)hz;
    return -1;
}

#else

/* RRAM is directly readable before clock/peripheral bring-up, but this cell
 * is never primed or written in early boot.  Missing or invalid state uses
 * the build default.  An interrupted aligned word store leaves one of the
 * two allowed values or a value the reader rejects. */
static TIKU_DURABLE uint32_t cpu_target_hz;
TIKU_PERSIST_CELL(cpu_target_cell, cpu_target_hz, 0x43505531UL, NULL, 0);

unsigned long tiku_cpu_nordic_target_hz(void)
{
    if (tiku_persist_cell_valid(&cpu_target_cell) &&
        (cpu_target_hz == 64000000UL || cpu_target_hz == 128000000UL)) {
        return cpu_target_hz;
    }
    return TIKU_NORDIC_CPU_MHZ * 1000000UL;
}

int tiku_cpu_nordic_target_set(unsigned long hz)
{
    uint32_t value = (uint32_t)hz;
    if (hz != 64000000UL && hz != 128000000UL) return -1;
    tiku_persist_cell_commit(&cpu_target_cell, &value, sizeof value);
    return tiku_persist_cell_valid(&cpu_target_cell) && cpu_target_hz == hz
               ? 0 : -1;
}

#endif /* TIKU_MINIMAL */
