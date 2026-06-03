/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - RP2350 memory operations + flash-backed NVM
 *
 * Persistent state placed with __attribute__((section(".persistent"))) or
 * __attribute__((section(".uninit"))) lives in the SRAM .uninit section
 * (see linker script).  Reads of that state are plain memory accesses, but
 * writes through tiku_mem_arch_nvm_write() are additionally mirrored to a
 * 4 KB sector at the end of XIP flash (__tiku_nvm_flash_*).  On boot,
 * tiku_mem_arch_init() restores the SRAM region from that mirror so the
 * state survives full power cycles, not just warm resets.
 *
 * The mirror sector is a verbatim 1:1 image of the live .uninit region:
 *
 *   flash[__tiku_nvm_flash_start ..] = mirror image (4 KB, includes magic)
 *
 *   SRAM[__uninit_start ..] = live working copy
 *
 * The mirror starts with a 4-byte magic (RP2350_NVM_MAGIC).  On boot we
 * read flash[0..3]; if it matches the magic, the rest of the sector is a
 * valid snapshot and we copy it into .uninit.  If the magic is missing
 * (fresh chip, post-mass-erase) we leave .uninit at its NOLOAD garbage
 * and let the kernel's "no magic, initialise fresh" logic in each
 * subsystem (persist, init, lc_persist, ...) do its job.
 *
 * Each tiku_mem_arch_nvm_write() call commits to flash immediately by:
 *   1. Copying src bytes into SRAM .uninit (so reads see the new data).
 *   2. Snapshotting all of .uninit (plus the magic) into the SRAM
 *      flush buffer.
 *   3. Disabling IRQs (XIP is suspended during flash op; ISRs would
 *      bus-fault on instruction fetch).
 *   4. Calling boot-ROM flash_range_erase + flash_range_program for
 *      the mirror sector.
 *   5. Re-enabling IRQs.
 *
 * Cost: ~20 ms per write (sector erase + program time).  Acceptable for
 * one-shot config writes (boot counter, persist register/write, init
 * table); slow for hot paths (lc_persist).  Hot-path callers are opt-in
 * so the trade-off is theirs to manage.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mem_arch.h"
#include "tiku_rp2350_regs.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Linker symbols                                                            */
/*---------------------------------------------------------------------------*/

extern uint8_t  __uninit_start;
extern uint8_t  __uninit_end;
extern uint32_t __tiku_nvm_flash_start;
extern uint32_t __tiku_nvm_flash_offset;
extern uint32_t __tiku_nvm_flash_size;

#define RP2350_NVM_SECTOR_SIZE   0x1000U   /* 4 KB QSPI erase granule */
#define RP2350_NVM_PAGE_SIZE     0x100U    /* 256-byte program page */
#define RP2350_NVM_MAGIC         0x4E564D54U   /* 'NVMT' little-endian */

/*---------------------------------------------------------------------------*/
/* Boot-ROM function lookups                                                 */
/*                                                                            */
/* Same pattern as tiku_cpu_rp2350_reboot_to_bootsel(): the 16-bit pointer    */
/* at flash offset 0x16 is the address of the table-lookup function.  We     */
/* try lookup masks 0x0004 (ARM_SEC) then 0x0010 (ARM_NONSEC) until one      */
/* of them resolves each function.  Codes are pico-sdk ROM_TABLE_CODE(c1,c2) */
/* which encodes as c1 | (c2 << 8).                                          */
/*---------------------------------------------------------------------------*/

#define ROM_FUNC_CONNECT_INTERNAL_FLASH   0x4649U  /* 'I' | ('F'<<8) */
#define ROM_FUNC_FLASH_EXIT_XIP           0x5845U  /* 'E' | ('X'<<8) */
#define ROM_FUNC_FLASH_RANGE_ERASE        0x4552U  /* 'R' | ('E'<<8) */
#define ROM_FUNC_FLASH_RANGE_PROGRAM      0x5052U  /* 'R' | ('P'<<8) */
#define ROM_FUNC_FLASH_FLUSH_CACHE        0x4346U  /* 'F' | ('C'<<8) */
#define ROM_FUNC_FLASH_ENTER_CMD_XIP      0x5843U  /* 'C' | ('X'<<8) */

typedef void *(*rom_lookup_fn_t)(uint32_t code, uint32_t mask);
typedef void (*rom_void_fn_t)(void);
typedef void (*rom_flash_erase_fn_t)(uint32_t flash_offset, size_t count,
                                     uint32_t block_size,
                                     uint8_t  block_cmd);
typedef void (*rom_flash_program_fn_t)(uint32_t flash_offset,
                                       const uint8_t *data, size_t count);

static rom_void_fn_t          g_rom_connect_flash;
static rom_void_fn_t          g_rom_flash_exit_xip;
static rom_flash_erase_fn_t   g_rom_flash_range_erase;
static rom_flash_program_fn_t g_rom_flash_range_program;
static rom_void_fn_t          g_rom_flash_flush_cache;
static rom_void_fn_t          g_rom_flash_enter_xip;
static uint8_t                g_rom_resolved;

static void *rom_lookup_any(rom_lookup_fn_t lookup, uint32_t code) {
    /* Try ARM_SEC first, fall back to ARM_NONSEC if the boot ROM exposed
     * the function under a different mask. */
    void *p = lookup(code, 0x0004U);
    if (p == NULL) {
        p = lookup(code, 0x0010U);
    }
    return p;
}

static void rom_resolve_once(void) {
    uint16_t lookup_addr;
    rom_lookup_fn_t lookup;

    if (g_rom_resolved) {
        return;
    }

    lookup_addr = *(volatile uint16_t *)(uintptr_t)0x16U;
    lookup = (rom_lookup_fn_t)(uintptr_t)lookup_addr;
    if (lookup == NULL) {
        return;     /* leave fn pointers NULL; flush path becomes a no-op */
    }

    g_rom_connect_flash =
        (rom_void_fn_t)rom_lookup_any(lookup, ROM_FUNC_CONNECT_INTERNAL_FLASH);
    g_rom_flash_exit_xip =
        (rom_void_fn_t)rom_lookup_any(lookup, ROM_FUNC_FLASH_EXIT_XIP);
    g_rom_flash_range_erase =
        (rom_flash_erase_fn_t)rom_lookup_any(lookup, ROM_FUNC_FLASH_RANGE_ERASE);
    g_rom_flash_range_program =
        (rom_flash_program_fn_t)rom_lookup_any(lookup, ROM_FUNC_FLASH_RANGE_PROGRAM);
    g_rom_flash_flush_cache =
        (rom_void_fn_t)rom_lookup_any(lookup, ROM_FUNC_FLASH_FLUSH_CACHE);
    g_rom_flash_enter_xip =
        (rom_void_fn_t)rom_lookup_any(lookup, ROM_FUNC_FLASH_ENTER_CMD_XIP);

    g_rom_resolved = 1U;
}

static int rom_flash_ready(void) {
    rom_resolve_once();
    return  g_rom_connect_flash       != NULL &&
            g_rom_flash_exit_xip      != NULL &&
            g_rom_flash_range_erase   != NULL &&
            g_rom_flash_range_program != NULL &&
            g_rom_flash_flush_cache   != NULL &&
            g_rom_flash_enter_xip     != NULL;
}

/*---------------------------------------------------------------------------*/
/* Flush buffer                                                              */
/*                                                                            */
/* One sector's worth of SRAM, used as the source for flash programming.     */
/* Layout: 4-byte magic, then the .uninit region verbatim, then 0xFF tail.   */
/*---------------------------------------------------------------------------*/

static uint8_t g_flush_buf[RP2350_NVM_SECTOR_SIZE]
    __attribute__((aligned(4)));

/* Compose the snapshot in g_flush_buf from the live .uninit region. */
static void compose_snapshot(void) {
    size_t uninit_size =
        (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);

    /* 4-byte magic at the head. */
    g_flush_buf[0] = (uint8_t)(RP2350_NVM_MAGIC      );
    g_flush_buf[1] = (uint8_t)(RP2350_NVM_MAGIC >>  8);
    g_flush_buf[2] = (uint8_t)(RP2350_NVM_MAGIC >> 16);
    g_flush_buf[3] = (uint8_t)(RP2350_NVM_MAGIC >> 24);

    /* Live SRAM copy. */
    if (uninit_size > (RP2350_NVM_SECTOR_SIZE - 4U)) {
        uninit_size = (RP2350_NVM_SECTOR_SIZE - 4U);
    }
    memcpy(&g_flush_buf[4], &__uninit_start, uninit_size);

    /* Pad to sector size with 0xFF (post-erase state). */
    memset(&g_flush_buf[4U + uninit_size], 0xFF,
           RP2350_NVM_SECTOR_SIZE - 4U - uninit_size);
}

/* Disable IRQs, drive the boot-ROM flash sequence, re-enable IRQs. The
 * boot-ROM functions handle XIP suspend/resume internally; the IRQ mask
 * is so an ISR (which is in XIP) doesn't bus-fault on instruction fetch
 * while XIP is paused. */
static void flash_commit_sector(uint32_t flash_offset,
                                const uint8_t *src,
                                size_t        len) {
    uint32_t primask;

    if (!rom_flash_ready()) {
        return;     /* boot ROM didn't expose flash ops -- skip */
    }

    /* Save and mask PRIMASK; restore at the end. */
    __asm__ volatile ("mrs %0, primask" : "=r"(primask));
    __asm__ volatile ("cpsid i" ::: "memory");

    g_rom_connect_flash();
    g_rom_flash_exit_xip();
    g_rom_flash_range_erase(flash_offset, RP2350_NVM_SECTOR_SIZE,
                            RP2350_NVM_SECTOR_SIZE, 0x20U /* 4KB cmd */);
    g_rom_flash_range_program(flash_offset, src, len);
    g_rom_flash_flush_cache();
    g_rom_flash_enter_xip();

    /* Restore PRIMASK. */
    __asm__ volatile ("msr primask, %0" : : "r"(primask) : "memory");
}

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

void tiku_mem_arch_init(void) {
    const uint32_t *flash = (const uint32_t *)&__tiku_nvm_flash_start;
    size_t uninit_size =
        (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);

    /* Resolve ROM functions early so a later nvm_write doesn't pay the
     * lookup cost.  rom_resolve_once() is idempotent. */
    rom_resolve_once();

    /* If the mirror sector starts with our magic, the rest is a valid
     * snapshot of .uninit -- copy it back into SRAM.  Otherwise leave
     * .uninit at whatever value it has (NOLOAD garbage on cold boot,
     * survival data on warm boot). */
    if (flash[0] == RP2350_NVM_MAGIC) {
        if (uninit_size > (RP2350_NVM_SECTOR_SIZE - 4U)) {
            uninit_size = (RP2350_NVM_SECTOR_SIZE - 4U);
        }
        memcpy(&__uninit_start,
               (const uint8_t *)&flash[1],
               uninit_size);
    }
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)buf;
    tiku_mem_arch_size_t i;
    for (i = 0; i < len; i++) {
        p[i] = 0U;
    }
}

void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len) {
    /* Reads come from the SRAM working copy -- which is exactly the
     * mirror image after tiku_mem_arch_init() restores it.  Plain
     * memcpy semantics. */
    tiku_mem_arch_size_t i;
    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len) {
    /* SRAM-only.  The flash commit happens at the matching
     * tiku_mpu_lock_nvm() relock point (via tiku_mem_arch_nvm_flush)
     * so direct stores into .persistent variables inside the unlock
     * window are captured in the same snapshot.  Doing the flash op
     * here would (a) double-write when callers combine direct stores
     * with arch writes, and (b) miss the direct stores entirely.
     *
     * Cost of a single relock: one ~20 ms erase+program cycle,
     * regardless of how many byte-level writes happened inside the
     * window.  Long transactions therefore amortise. */
    tiku_mem_arch_size_t i;
    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

void tiku_mem_arch_nvm_flush(void) {
    /* Snapshot the live .uninit region into g_flush_buf (prepended
     * with the magic word), then erase + program the 4 KB flash
     * mirror sector.  This is the explicit durability checkpoint:
     * every kernel write to .persistent / .uninit eventually flows
     * through this on its way to surviving a power cycle. */
    compose_snapshot();
    flash_commit_sector((uint32_t)(uintptr_t)&__tiku_nvm_flash_offset,
                        g_flush_buf, RP2350_NVM_SECTOR_SIZE);
}
