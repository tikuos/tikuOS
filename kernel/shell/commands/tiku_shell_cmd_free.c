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
extern char _start;         /* first byte of .text (code)      */

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
    uint16_t fram_data;
    uint16_t fram_code;
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

    {
        uint16_t text_start = (uint16_t)(uintptr_t)&_start;
        fram_data = text_start > TIKU_DEVICE_FRAM_START
                    ? (uint16_t)(text_start - TIKU_DEVICE_FRAM_START) : 0;
        fram_code = TIKU_DEVICE_FRAM_END >= text_start
                    ? (uint16_t)(TIKU_DEVICE_FRAM_END + 1U - text_start)
                    : 0;
    }

    fram_used = fram_data + fram_code;

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
    SHELL_PRINTF("  code        %5u\n", fram_code);
    SHELL_PRINTF("  const/data  %5u\n", fram_data);
    SHELL_PRINTF("  unallocd    %5u\n",
                 fram_total > (fram_data + fram_code)
                 ? fram_total - fram_data - fram_code : 0);
#if defined(TIKU_DEVICE_HAS_HIFRAM) && TIKU_DEVICE_HAS_HIFRAM
    /*
     * Parts with a separate upper FRAM bank (FR5994, FR6989). The
     * size is > 64 KB so it must be printed via %lu. The kernel can
     * only place data here under MEMORY_MODEL=large; under the
     * default small model this region is reserved but unused.
     */
    SHELL_PRINTF("  hifram      %5lu (upper bank, large-model only)\n",
                 (unsigned long)(TIKU_DEVICE_HIFRAM_END
                                 - TIKU_DEVICE_HIFRAM_START + 1UL));
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
