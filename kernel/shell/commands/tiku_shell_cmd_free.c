/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_free.c - "free" command implementation
 *
 * Displays memory usage: SRAM and non-volatile totals, the tier pools, and a
 * per-process breakdown (declared footprint plus live allocations).
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
/* Unconditional: this header carries the TIKU_DEVICE_NVM_LABEL and
 * TIKU_DEVICE_RAM_USABLE fallbacks that every memory report needs.  Gating it
 * on TIKU_INIT_ENABLE would make the fallbacks reachable in some builds and
 * not others -- the include-order trap. */
#include <kernel/memory/tiku_nvm_map.h>

#if TIKU_INIT_ENABLE
#include <kernel/init/tiku_init.h>
#endif

/*---------------------------------------------------------------------------*/
/* LINKER SYMBOLS                                                            */
/*---------------------------------------------------------------------------*/

/*
 * The linker scripts emit these symbols at section boundaries; their
 * addresses (not values) give the region sizes.  The layout shown is MSP430's.
 *
 * SRAM layout (low → high):
 *   __datastart     .data start (initialised globals)
 *   _edata          .data end / .bss start
 *   _end            .bss end (last static allocation)
 *   ...             heap (unused on TikuOS)
 *   ...             ← stack grows down from __stack
 *   __stack         top of SRAM
 *
 * NVM (FRAM) layout:
 *   .text           code
 *   .rodata         read-only data
 *   .persistent     FRAM-resident variables (init table, etc.)
 */
/* --- SRAM boundaries --- */
extern char __datastart;    /* first byte of .data (SRAM base) */
extern char _end;           /* past last byte of .bss          */
#if defined(TIKU_TIER_SRAM_DERIVED)
/* The SRAM tier is carved by the linker, not declared in .bss, so _end does
 * not account for it and the leftover below would report it as free. */
extern char __tier_sram_start;
extern char __tier_sram_end;
#endif
extern char __stack;        /* top of SRAM (stack origin)      */
extern char __tiku_stack_bottom __attribute__((weak));
extern char __tiku_stack_guard_start __attribute__((weak));

/* --- NVM boundaries --- */
extern char _etext;         /* past last byte of .text         */

/*
 * __hifram_end is provided by arch/msp430/devices/msp430fr5994_8k_ram.ld
 * (and any future per-device LD overrides). It marks the byte right
 * after the last HIFRAM-resident section (.upper.rodata, .upper.bss,
 * .upper.text). Defined as `weak` so this file still links on parts
 * whose LD script doesn't provide the symbol; an absent symbol
 * (address == 0) means "no usage data available" and the row is skipped.
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
 * each device's .ld file, and it is subtracted here so "unallocd" reports
 * the truly empty lower-FRAM space, not "empty space + 128 B IVT".
 */
#if defined(PLATFORM_MSP430)
#define TIKU_FREE_IVT_BYTES 128U
#else
/* Elsewhere the vector table lies below _etext and counts with the image. */
#define TIKU_FREE_IVT_BYTES 0U
#endif

/*---------------------------------------------------------------------------*/
/* CURRENT STACK USE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Estimate current stack usage by reading the stack pointer.
 *
 * Stack grows downward from __stack.  SP points to the last pushed
 * value.  The difference gives an approximate usage at this instant.
 *
 * @return Bytes in use, at most 0xFFFF
 */
static uint16_t
stack_used(void)
{
#if defined(PLATFORM_MSP430)
    uint16_t sp;
    uint16_t top = (uint16_t)(uintptr_t)&__stack;
    __asm__ volatile ("mov r1, %0" : "=r"(sp));
    if (sp < top) {
        return top - sp;
    }
    return 0;
#elif defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC)
    /* Cortex-M: 32-bit SP, clamped to 0xFFFF for the uint16_t result. */
    uintptr_t sp;
    uintptr_t top = (uintptr_t)&__stack;
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    if (sp < top) {
        uintptr_t used = top - sp;
        return (used > 0xFFFFU) ? 0xFFFFU : (uint16_t)used;
    }
    return 0;
#elif defined(PLATFORM_ESP32C61)
    uintptr_t sp;
    uintptr_t top = (uintptr_t)&__stack;
    __asm__ volatile ("mv %0, sp" : "=r"(sp));
    if (sp < top) {
        uintptr_t used = top - sp;
        return (used > 0xFFFFU) ? 0xFFFFU : (uint16_t)used;
    }
    return 0;
#else
    return 0;   /* host fallback */
#endif
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_free(uint8_t argc, const char *argv[])
{
    uint8_t i;
    unsigned long sram_total;
    unsigned long sram_static;
    unsigned long fram_total;
    unsigned long fram_used;
    unsigned long stack_budget;
    uintptr_t stack_bottom = (uintptr_t)&__tiku_stack_bottom;
    uintptr_t guard_start = (uintptr_t)&__tiku_stack_guard_start;
    int bounded_stack = guard_start >= (uintptr_t)&_end &&
        stack_bottom >= guard_start && stack_bottom <= (uintptr_t)&__stack;
    uint8_t proc_count = 0;

    (void)argc;
    (void)argv;

    sram_total = (unsigned long)TIKU_DEVICE_RAM_USABLE;

    /*
     * fram_total is the NVM window FRAM_START..FRAM_END, which on MSP430
     * parts with HIFRAM (FR5994, FR6989) is the lower 16-bit window, not
     * the whole FRAM: the in-use rows below measure against the same
     * window, so TIKU_DEVICE_FRAM_SIZE would stop them reconciling.  The
     * upper bank (TIKU_HIFRAM*, MEMORY_MODEL=large) gets its own line.
     */
    fram_total = (unsigned long)(TIKU_DEVICE_FRAM_END + 1UL
                            - TIKU_DEVICE_FRAM_START);
    sram_static = (unsigned long)((uintptr_t)&_end - (uintptr_t)&__datastart);

    /*
     * NVM in-use byte count = _etext - FRAM_START, one number rather than a
     * code/data split.  On MSP430 the small-model layout is
     *   [ rodata . persistent . data-init . text ] _etext . slack . IVT
     * and in the large model (-mcode-region=either) .lower.text lands below
     * _start, so a split at _start would count that code as const/data.
     * _etext ends every section the linker fills upward from FRAM_START in
     * either model.
     */
    {
        unsigned long text_end = (unsigned long)(uintptr_t)&_etext;
        /* Both bounds: for an image linked into SRAM above the NVM window, a
         * lower-bound test alone would count the gap between the two
         * memories as in use.  With _etext outside it, nothing is counted. */
        fram_used = (text_end > TIKU_DEVICE_FRAM_START &&
                     text_end <= TIKU_DEVICE_FRAM_END)
                    ? (unsigned long)(text_end + TIKU_FREE_IVT_BYTES
                                 - TIKU_DEVICE_FRAM_START)
                    : TIKU_FREE_IVT_BYTES;
    }

    /* ---- Compile-time (fixed at link) ---- */
    SHELL_PRINTF(SH_YELLOW "--- Compile-time ---" SH_RST "\n");
    SHELL_PRINTF(SH_BOLD "SRAM" SH_RST "  %5lu total\n",
                 (unsigned long)sram_total);
    SHELL_PRINTF("  .data+.bss  %5lu\n", (unsigned long)sram_static);
#if defined(TIKU_TIER_SRAM_DERIVED)
    {
        uintptr_t bank_lo = (uintptr_t)&__datastart;
        const uint8_t *base;
        tiku_mem_stats_t st;
        uint8_t si;
        (void)tiku_tier_init();
        for (si = 0; tiku_tier_span_stats(TIKU_MEM_SRAM, si, &base, &st)
                     == TIKU_MEM_OK; si++) {
            uintptr_t start = (uintptr_t)base;
            int primary = start >= bank_lo && start <= bank_lo + sram_total &&
                          st.total_bytes <= bank_lo + sram_total - start;
            SHELL_PRINTF("  tier bank%u  %5lu\n", primary ? 1u : 2u,
                         (unsigned long)st.total_bytes);
            if (primary) { sram_static += st.total_bytes; }
        }
    }
#endif
    stack_budget = sram_total > sram_static ? sram_total - sram_static : 0UL;
    if (bounded_stack) {
        unsigned long reserved = (uintptr_t)&__stack - guard_start;
        SHELL_PRINTF("  unassigned  %5lu\n",
                     stack_budget > reserved ? stack_budget - reserved : 0UL);
        stack_budget = (uintptr_t)&__stack - stack_bottom;
        SHELL_PRINTF("  stack       %5lu\n", stack_budget);
        SHELL_PRINTF("  guard       %5lu\n", (unsigned long)(stack_bottom - guard_start));
    } else {
        SHELL_PRINTF("  stack+free  %5lu\n", stack_budget);
    }

    SHELL_PRINTF(SH_BOLD "%s" SH_RST "  %5lu total"
#if defined(TIKU_DEVICE_HAS_HIFRAM) && TIKU_DEVICE_HAS_HIFRAM
                 " (lower window)"
#endif
                 "\n", TIKU_DEVICE_NVM_LABEL,
                 (unsigned long)fram_total);
    /* in-use = code+rodata+persistent+data-init+(.lower.text under
     * large mode), reported as one number because the breakdown
     * differs between memory models. See _etext rationale above. */
    SHELL_PRINTF("  in use      %5lu\n",
                 (unsigned long)(fram_used > TIKU_FREE_IVT_BYTES
                 ? (fram_used - TIKU_FREE_IVT_BYTES) : 0));
#if defined(PLATFORM_MSP430)
    /* The interrupt-vector table sits inside the NVM window on MSP430 only;
     * elsewhere TIKU_FREE_IVT_BYTES is 0 and the row is just noise. */
    SHELL_PRINTF("  ivt         %5u\n", (unsigned)TIKU_FREE_IVT_BYTES);
#endif
    SHELL_PRINTF("  unallocd    %5lu\n",
                 (unsigned long)(fram_total > fram_used ? fram_total - fram_used : 0));
#if defined(TIKU_DEVICE_HAS_HIFRAM) && TIKU_DEVICE_HAS_HIFRAM
    /*
     * Parts with a separate upper FRAM bank (FR5994, FR6989). Sizes
     * are > 64 KB so they must be printed via %lu. The kernel can
     * only place data here under MEMORY_MODEL=large, the default on
     * these parts; a small-model build leaves the region unused.
     *
     * If the per-device LD provides __hifram_end (the override
     * msp430fr5994_8k_ram.ld does), the split is used + free.
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
    SHELL_PRINTF("  stack free  " SH_BOLD "%5lu" SH_RST "\n",
                 stack_budget > stack_used() ? stack_budget - stack_used() : 0UL);

    /* NVM: unallocated + tier allocator */
    SHELL_PRINTF(SH_BOLD "%s" SH_RST "\n", TIKU_DEVICE_NVM_LABEL);
    {
        tiku_mem_stats_t nvm_tier;
        if (tiku_tier_stats(TIKU_MEM_NVM, &nvm_tier) == TIKU_MEM_OK) {
            SHELL_PRINTF("  tier pool   %5u / %u  (peak %u)\n",
                         nvm_tier.used_bytes, nvm_tier.total_bytes,
                         nvm_tier.peak_bytes);
        }
    }
    SHELL_PRINTF("  free now    " SH_BOLD "%5lu" SH_RST "\n",
                 (unsigned long)(fram_total > fram_used ? fram_total - fram_used : 0));

#if (TIKU_DRV_PSRAM_ENABLE + 0)
    /*
     * PSRAM is a late-attach tier -- absent at boot, present after `power
     * psram up`, gone after `down` -- so it is reported only while attached,
     * and a failing tiku_tier_stats means detached, not an error.
     */
    {
        tiku_mem_stats_t ps_tier;
        if (tiku_tier_stats(TIKU_MEM_PSRAM, &ps_tier) == TIKU_MEM_OK &&
            ps_tier.total_bytes != 0u) {
            SHELL_PRINTF(SH_BOLD "PSRAM" SH_RST "\n");
            SHELL_PRINTF("  tier pool   %5lu / %lu  (peak %lu)\n",
                         (unsigned long)ps_tier.used_bytes,
                         (unsigned long)ps_tier.total_bytes,
                         (unsigned long)ps_tier.peak_bytes);
            SHELL_PRINTF("  free now    " SH_BOLD "%5lu" SH_RST "\n",
                         (unsigned long)(ps_tier.total_bytes -
                                         ps_tier.used_bytes));
        }
    }
#endif

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
    /* Generic column names: the section header above names the NVM
     * technology (TIKU_DEVICE_NVM_LABEL). */
    SHELL_PRINTF(" pid  %-10s    sram     nvm  state\n", "name");
    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        struct tiku_process *p = tiku_process_get((int8_t)i);
        if (p == NULL) {
            continue;
        }
        /* Declared footprint plus measured live allocations (e.g. the BASIC
         * arena), via the helpers ps and /proc use.  The values are uint32_t
         * (a BASIC arena can be hundreds of KB), hence %6lu. */
        SHELL_PRINTF(" %3d  %-10s  %6lu  %6lu  %s\n",
                     p->pid,
                     p->name ? p->name : "?",
                     (unsigned long)tiku_process_sram_used(p),
                     (unsigned long)tiku_process_fram_used(p),
                     tiku_process_state_str(p->state));
    }
}
