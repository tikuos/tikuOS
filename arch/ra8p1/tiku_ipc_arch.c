/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_arch.c - CPU1 carve mapping with host cache maintenance.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_ipc_hal.h>
#include <hal/tiku_cpu.h>
#include "cpu1/tiku_cpu1_ipc.h"
#include "tiku_cpu1_arch.h"

static void clean(const void *p, uint32_t n) { tiku_cpu_dcache_clean(p, n); }
static void invalidate(const void *p, uint32_t n) { tiku_cpu_dcache_invalidate(p, n); }
static void barrier(void) { __asm__ volatile ("dsb" ::: "memory"); }
static uint8_t ready(void) { return tiku_ra8p1_cpu1_running() != 0; }

static const tiku_ipc_shared_mapping_t mapping = {
    TIKU_CPU1_STREAM, (const uint8_t *)TIKU_CPU1_AREA_ADDR,
    TIKU_CPU1_AREA_SIZE, TIKU_MEM_REGION_SRAM,
    clean, invalidate, barrier, ready, 1
};

const tiku_ipc_shared_mapping_t *tiku_ipc_hal_shared(void) { return &mapping; }
