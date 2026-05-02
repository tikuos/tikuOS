/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_free.c - "free" command implementation
 *
 * Displays system memory usage: SRAM and FRAM totals with
 * per-process breakdown from the process registry's
 * sram_used / fram_used fields.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_free.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/process/tiku_process.h>
#include <kernel/memory/tiku_mem.h>
#include "tiku.h"
#include <stdint.h>

#if TIKU_INIT_ENABLE
#include <kernel/init/tiku_init.h>
#include <kernel/memory/tiku_nvm_map.h>
#endif

/*---------------------------------------------------------------------------*/
/* LINKER SYMBOLS                                                            */
/*---------------------------------------------------------------------------*/

/*
 * The MSP430 GCC linker emits these symbols at section boundaries.
 * We take their addresses (not values) to compute region sizes.
 *
 * SRAM layout (low → high):
 *   __datastart     .data start (initialised globals)
 *   _edata          .data end / .bss start
 *   _end            .bss end (last static allocation)
 *   ...             heap (unused on TikuOS)
 *   ...             ← stack grows down from __stack
 *   __stack         top of SRAM
 *
 * FRAM layout:
 *   .text           code
 *   .rodata         read-only data
 *   .persistent     FRAM-resident variables (init table, etc.)
 */
/* --- SRAM boundaries --- */
extern char __datastart;    /* first byte of .data (SRAM base) */
extern char _end;           /* past last byte of .bss          */
extern char __stack;        /* top of SRAM (stack origin)      */

/* --- FRAM boundaries --- */
extern char _etext;         /* past last byte of .text         */

/*
 * __hifram_end is provided by arch/msp430/devices/msp430fr5994_8k_ram.ld
 * (and any future per-device LD overrides). It marks the byte right
 * after the last HIFRAM-resident section (.upper.rodata, .upper.bss,
 * .upper.text). Defined as `weak` so this file still links on parts
 * whose LD script doesn't provide the symbol — we treat the absence
 * (address == 0) as "no usage data available" and skip the row.
 *
 * Gated on TIKU_MEMORY_MODEL_LARGE because in small-mode builds the
 * 16-bit relocation can't reach a HIFRAM address (>= 0x10000) — a
 * direct reference would link-fail with R_MSP430X_ABS16 truncation.
 * Small-mode builds report "hifram total" only and skip the
 * in-use/unallocd breakdown.
 */
#if defined(TIKU_DEVICE_HAS_HIFRAM) && TIKU_DEVICE_HAS_HIFRAM && \
    defined(TIKU_MEMORY_MODEL_LARGE) && TIKU_MEMORY_MODEL_LARGE
extern char __hifram_end __attribute__((weak));
#define TIKU_FREE_HAS_HIFRAM_END 1
#else
#define TIKU_FREE_HAS_HIFRAM_END 0
#endif

/*
 * The MSP430 IVT lives in the top 128 bytes of lower FRAM
 * (0xFF80..0xFFFF on every FR-series part TikuOS targets). The
 * linker reserves it through the __interrupt_vector_* sections in
 * each device's .ld file; we subtract it here so "unallocd" reports
 * the truly empty lower-FRAM space, not "empty space + 128 B IVT".
 */
#define TIKU_FREE_IVT_BYTES 128U

/*---------------------------------------------------------------------------*/
/* STACK HIGH-WATER MARK                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Estimate current stack usage by reading the stack pointer.
 *
 * Stack grows downward from __stack.  SP points to the last pushed
 * value.  The difference gives an approximate usage at this instant.
 */
static uint16_t
stack_used(void)
{
    uint16_t sp;
    uint16_t top = (uint16_t)(uintptr_t)&__stack;

#ifdef PLATFORM_MSP430
    __asm__ volatile ("mov r1, %0" : "=r"(sp));
#else
    sp = top;   /* fallback for host builds */
#endif

    if (sp < top) {
        return top - sp;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_free(uint8_t argc, const char *argv[])
{
    uint8_t i;
    uint16_t sram_total;
    uint16_t sram_static;
    uint16_t fram_total;
    uint16_t fram_used;
    uint8_t proc_count = 0;

    (void)argc;
    (void)argv;

    sram_total = (uint16_t)TIKU_DEVICE_RAM_SIZE;

    /*
     * fram_total is the size of the lower-FRAM 16-bit window
     * (FRAM_START..FRAM_END), not the chip's whole FRAM. The
     * fram_code / fram_data rows below measure against this same
     * window — using TIKU_DEVICE_FRAM_SIZE here would over-report
     * on parts with HIFRAM (FR5994, FR6989) and the rows would
     * stop reconciling. On those parts the upper bank is reachable
     * only via TIKU_HIFRAM* + MEMORY_MODEL=large and is reported
     * as a separate line below when present.
     *
     * The "+ 1U" wraps in uint16 (FRAM_END is 0xFFFF on most parts),
     * so the subtraction is computed modulo 2^16 — which gives the
     * correct lower-window size as long as FRAM_END == 0xFFFF.
     */
    fram_total = (uint16_t)(TIKU_DEVICE_FRAM_END + 1U
                            - TIKU_DEVICE_FRAM_START);
    sram_static = (uint16_t)((uintptr_t)&_end - (uintptr_t)&__datastart);

    /*
     * Lower-FRAM in-use byte count = _etext - FRAM_START.
     *
     * Why this single number rather than a code/data split:
     *
     *   In small mode the FRAM layout is
     *     [ rodata . persistent . data-init . text ] _etext . slack . IVT
     *   so _start - FRAM_START used to give "const/data" and
     *   _etext - _start used to give "code". That worked.
     *
     *   In large mode (-mcode-region=either) the layout becomes
     *     [ rodata . persistent . data-init . lower.text . text ]
     *     _etext . slack . IVT
     *   .lower.text lands BELOW _start, so the old split silently
     *   reclassified ~30 KB of code as "const/data" and reported
     *   "code = 396" on a build that actually had 36 KB of code in
     *   lower FRAM.
     *
     *   _etext is the end of all FRAM-resident sections that the
     *   linker fills upward from FRAM_START, regardless of which
     *   memory model is in use, so _etext - FRAM_START is the
     *   one number that's correct across both modes.
     */
    {
        uint16_t text_end = (uint16_t)(uintptr_t)&_etext;
        fram_used = text_end > TIKU_DEVICE_FRAM_START
                    ? (uint16_t)(text_end + TIKU_FREE_IVT_BYTES
                                 - TIKU_DEVICE_FRAM_START)
                    : TIKU_FREE_IVT_BYTES;
    }

    /* ---- Compile-time (fixed at link) ---- */
    SHELL_PRINTF(SH_YELLOW "--- Compile-time ---" SH_RST "\n");
    SHELL_PRINTF(SH_BOLD "SRAM" SH_RST "  %5u total\n", sram_total);
    SHELL_PRINTF("  .data+.bss  %5u\n", sram_static);
    /* What's left of SRAM after static data: hosts the stack and any
     * future heap. Not "reserved" in any protective sense — it's the
     * available pool. Stack-now / free-now under "Runtime" below
     * partition this number. */
    SHELL_PRINTF("  stack+free  %5u\n",
                 sram_total - sram_static);

    SHELL_PRINTF(SH_BOLD "FRAM" SH_RST "  %5u total (lower window)\n",
                 fram_total);
    /* in-use = code+rodata+persistent+data-init+(.lower.text under
     * large mode), reported as one number because the breakdown
     * differs between memory models. See _etext rationale above. */
    SHELL_PRINTF("  in use      %5u\n",
                 fram_used > TIKU_FREE_IVT_BYTES
                 ? (uint16_t)(fram_used - TIKU_FREE_IVT_BYTES) : 0);
    SHELL_PRINTF("  ivt         %5u\n", (unsigned)TIKU_FREE_IVT_BYTES);
    SHELL_PRINTF("  unallocd    %5u\n",
                 fram_total > fram_used ? fram_total - fram_used : 0);
#if defined(TIKU_DEVICE_HAS_HIFRAM) && TIKU_DEVICE_HAS_HIFRAM
    /*
     * Parts with a separate upper FRAM bank (FR5994, FR6989). Sizes
     * are > 64 KB so they must be printed via %lu. The kernel can
     * only place data here under MEMORY_MODEL=large; under the
     * default small model this region is reserved but unused.
     *
     * If the per-device LD provides __hifram_end (the override
     * msp430fr5994_8k_ram.ld does), we report used + free split.
     * Otherwise fall back to the single "total reachable" line.
     */
    {
        unsigned long hifram_total =
            (unsigned long)(TIKU_DEVICE_HIFRAM_END
                            - TIKU_DEVICE_HIFRAM_START + 1UL);
        SHELL_PRINTF("  hifram      %5lu total (upper bank)\n",
                     hifram_total);
#if TIKU_FREE_HAS_HIFRAM_END
        if (&__hifram_end != (char *)0) {
            uintptr_t hi_end = (uintptr_t)&__hifram_end;
            unsigned long hi_used =
                hi_end > TIKU_DEVICE_HIFRAM_START
                ? (unsigned long)(hi_end - TIKU_DEVICE_HIFRAM_START)
                : 0UL;
            SHELL_PRINTF("    in use    %5lu\n", hi_used);
            SHELL_PRINTF("    unallocd  %5lu\n",
                         hifram_total > hi_used
                         ? hifram_total - hi_used : 0UL);
        }
#endif
    }
#endif

    /* ---- Runtime (changes dynamically) ---- */
    SHELL_PRINTF(SH_GREEN "--- Runtime ---" SH_RST "\n");

    /* SRAM: stack + tier allocator */
    SHELL_PRINTF(SH_BOLD "SRAM" SH_RST "\n");
    SHELL_PRINTF("  stack now   %5u\n", stack_used());
    {
        tiku_mem_stats_t sram_tier;
        if (tiku_tier_stats(TIKU_MEM_SRAM, &sram_tier) == TIKU_MEM_OK) {
            SHELL_PRINTF("  tier pool   %5u / %u  (peak %u)\n",
                         sram_tier.used_bytes, sram_tier.total_bytes,
                         sram_tier.peak_bytes);
        }
    }
    SHELL_PRINTF("  free now    " SH_BOLD "%5u" SH_RST "\n",
                 sram_total - sram_static - stack_used());

    /* FRAM: unallocated + tier allocator */
    SHELL_PRINTF(SH_BOLD "FRAM" SH_RST "\n");
    {
        tiku_mem_stats_t nvm_tier;
        if (tiku_tier_stats(TIKU_MEM_NVM, &nvm_tier) == TIKU_MEM_OK) {
            SHELL_PRINTF("  tier pool   %5u / %u  (peak %u)\n",
                         nvm_tier.used_bytes, nvm_tier.total_bytes,
                         nvm_tier.peak_bytes);
        }
    }
    SHELL_PRINTF("  free now    " SH_BOLD "%5u" SH_RST "\n",
                 fram_total > fram_used ? fram_total - fram_used : 0);

#if TIKU_INIT_ENABLE
    {
        const tiku_nvm_region_t *r;
        uint8_t init_count = tiku_init_count();
        uint16_t entry_bytes = (uint16_t)init_count *
                               sizeof(tiku_init_entry_t);

        r = tiku_nvm_region_get(TIKU_NVM_REGION_CONFIG);
        if (r != (const tiku_nvm_region_t *)0) {
            SHELL_PRINTF("  config rgn  %5u allocated\n", r->size);
            SHELL_PRINTF("  init table  %5u (%u/%u entries)\n",
                         4 + entry_bytes,
                         init_count, TIKU_INIT_MAX_ENTRIES);
        }
    }
#endif

    /* ---- Processes ---- */
    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        if (tiku_process_get((int8_t)i) != NULL) {
            proc_count++;
        }
    }

    if (proc_count == 0) {
        return;
    }

    SHELL_PRINTF(SH_CYAN "--- Processes (%u/%u) ---" SH_RST "\n",
                 proc_count, TIKU_PROCESS_MAX);
    SHELL_PRINTF(" pid  %-10s  sram  fram  state\n", "name");
    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        struct tiku_process *p = tiku_process_get((int8_t)i);
        if (p == NULL) {
            continue;
        }
        SHELL_PRINTF(" %3d  %-10s  %4u  %4u  %s\n",
                     p->pid,
                     p->name ? p->name : "?",
                     p->sram_used,
                     p->fram_used,
                     tiku_process_state_str(p->state));
    }
}
