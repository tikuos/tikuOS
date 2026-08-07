/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu1_cache.h - the CPU1-side cache and MPU registers.
 *
 * Private to the payload, which is a standalone link and must not pull in
 * the M85's register header.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CPU1_CACHE_H_
#define TIKU_CPU1_CACHE_H_

/*
 * The Cortex-M33 has no cache of its own (UM 2.16).  What Table 1.15 counts
 * as CPU1 cache is two 16 KB Renesas blocks outside the core: C-Cache serves
 * 0x0000_0000-0x1FFF_FFFF over the C-AHB, S-Cache 0x2000_0000-0xDFFF_FFFF
 * over the S-AHB.  This payload links into SRAM at 0x2200_0000, so BOTH its
 * fetches and its data go through the S-Cache and the C-Cache would cache
 * nothing.  Line size is 32 bytes, the same as the M85's.
 */
#define CPU1_CACHE_BASE     0x4001C000UL        /* secure view */
#define CPU1_SCACTL         (CPU1_CACHE_BASE + 0x040UL)
#define CPU1_SCAFCT         (CPU1_CACHE_BASE + 0x044UL)
#define CPU1_SCAWTA         (CPU1_CACHE_BASE + 0x04CUL)
#define CPU1_SCACTL_ENS     (1UL << 0)
#define CPU1_SCAFCT_FS      (1UL << 0)          /* flush, self-clearing */
#define CPU1_SCAWTA_WT      (1UL << 0)          /* write-through, reset 1 */

/* CPU1's own PMSAv8 MPU, at the architectural addresses in its private PPB.
 * The M85 cannot reach it and it cannot reach the M85's. */
#define CPU1_MPU_CTRL       0xE000ED94UL
#define CPU1_MPU_RNR        0xE000ED98UL
#define CPU1_MPU_RBAR       0xE000ED9CUL
#define CPU1_MPU_RLAR       0xE000EDA0UL
#define CPU1_MPU_MAIR0      0xE000EDC0UL
#define CPU1_MPU_CTRL_ENABLE    (1UL << 0)
#define CPU1_MPU_CTRL_HFNMIENA  (1UL << 1)
#define CPU1_MPU_CTRL_PRIVDEF   (1UL << 2)
#define CPU1_MPU_RBAR_XN        (1UL << 0)
#define CPU1_MPU_RBAR_AP_RW     (1UL << 1)
#define CPU1_MPU_RLAR_EN        (1UL << 0)

/** @brief MAIR attr0: Normal memory, inner and outer non-cacheable. */
#define CPU1_MAIR_NORMAL_NC 0x44UL

#endif /* TIKU_CPU1_CACHE_H_ */
