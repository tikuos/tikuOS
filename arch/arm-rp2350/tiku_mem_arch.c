/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - RP2350 memory operations and flash-backed NVM.
 *
 * Durable state lives in SRAM .uninit and is mirrored, behind a 16-byte CRC
 * header, to the flash backup sector; boot restores it when the image checks
 * out.  Writes land in SRAM and the flash commit runs at the MPU relock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_mem_arch.h"
#include "tiku_mpu_arch.h"  /* arch NVM window around the mirror restore */
#include "tiku_rp2350_regs.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* LINKER SYMBOLS                                                            */
/*---------------------------------------------------------------------------*/

extern uint8_t  __uninit_start;
extern uint8_t  __uninit_end;
extern uint32_t __tiku_nvm_flash_start[]; /* incomplete array: no size */
extern uint32_t __tiku_nvm_flash_offset;
extern uint32_t __tiku_nvm_flash_size;

/**
 * @brief Flash geometry of the mirror: the erase sector and the program page.
 *
 * The mirror's magic words and header layout come from tiku_nvm_mirror.h.
 */
#define RP2350_NVM_SECTOR_SIZE   0x1000U   /* 4 KB QSPI erase granule */
#define RP2350_NVM_PAGE_SIZE     0x100U    /* 256-byte program page */
#include "kernel/memory/tiku_nvm_mirror.h"

/*---------------------------------------------------------------------------*/
/* BOOT-ROM FUNCTION LOOKUPS                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief ROM_TABLE_CODE keys for the boot-ROM flash operation functions.
 *
 * Encoded as c1 | (c2 << 8), as pico-sdk's ROM_TABLE_CODE(c1, c2).  The ROM's
 * table-lookup function, whose 16-bit address sits at ROM address 0x16, finds
 * each under mask 0x0004 (ARM_SEC) or 0x0010 (ARM_NONSEC).
 */
#define ROM_FUNC_CONNECT_INTERNAL_FLASH   0x4649U  /* 'I' | ('F'<<8) */
#define ROM_FUNC_FLASH_EXIT_XIP           0x5845U  /* 'E' | ('X'<<8) */
#define ROM_FUNC_FLASH_RANGE_ERASE        0x4552U  /* 'R' | ('E'<<8) */
#define ROM_FUNC_FLASH_RANGE_PROGRAM      0x5052U  /* 'R' | ('P'<<8) */
#define ROM_FUNC_FLASH_FLUSH_CACHE        0x4346U  /* 'F' | ('C'<<8) */
#define ROM_FUNC_FLASH_ENTER_CMD_XIP      0x5843U  /* 'C' | ('X'<<8) */

/** @brief ROM function pointer types for boot-ROM flash operations. */
typedef void *(*rom_lookup_fn_t)(uint32_t code, uint32_t mask);
typedef void (*rom_void_fn_t)(void);
typedef void (*rom_flash_erase_fn_t)(uint32_t flash_offset, size_t count,
                                     uint32_t block_size,
                                     uint8_t  block_cmd);
typedef void (*rom_flash_program_fn_t)(uint32_t flash_offset,
                                       const uint8_t *data, size_t count);

/**
 * @brief Resolved boot-ROM flash function pointers and resolution flag.
 *
 * rom_resolve_once() fills them; a function the ROM table does not list
 * stays NULL.  g_rom_resolved is 1 once the lookups have run.
 */
static rom_void_fn_t          g_rom_connect_flash;
static rom_void_fn_t          g_rom_flash_exit_xip;
static rom_flash_erase_fn_t   g_rom_flash_range_erase;
static rom_flash_program_fn_t g_rom_flash_range_program;
static rom_void_fn_t          g_rom_flash_flush_cache;
static rom_void_fn_t          g_rom_flash_enter_xip;
static uint8_t                g_rom_resolved;

/**
 * @brief Look up a boot-ROM function, trying ARM_SEC then ARM_NONSEC mask.
 *
 * @param lookup  Boot-ROM table-lookup function, from ROM address 0x16.
 * @param code    ROM_TABLE_CODE value identifying the desired function.
 * @return Pointer to the ROM function, or NULL if not found under either mask.
 */
static void *rom_lookup_any(rom_lookup_fn_t lookup, uint32_t code) {
    /* ARM_SEC first, then ARM_NONSEC for a function listed only there. */
    void *p = lookup(code, 0x0004U);
    if (p == NULL) {
        p = lookup(code, 0x0010U);
    }
    return p;
}

/**
 * @brief Resolve all boot-ROM flash function pointers exactly once.
 *
 * Reads the 16-bit lookup-function address at boot ROM address 0x16 and calls
 * rom_lookup_any() per flash operation; a repeat call returns at once.  With no
 * lookup function every pointer stays NULL, and flash commits fail.
 */
static void rom_resolve_once(void) {
    uint16_t lookup_addr;
    rom_lookup_fn_t lookup;

    if (g_rom_resolved) {
        return;
    }

    lookup_addr = *(volatile uint16_t *)(uintptr_t)0x16U;
    lookup = (rom_lookup_fn_t)(uintptr_t)lookup_addr;
    if (lookup == NULL) {
        return;     /* pointers stay NULL: flash commits return -1 */
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

/**
 * @brief Return non-zero if all boot-ROM flash function pointers are resolved.
 *
 * Calls rom_resolve_once() to ensure resolution has been attempted, then
 * checks that every required function pointer is non-NULL.
 *
 * @return 1 if all flash operations are available, 0 otherwise.
 */
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
/* FLUSH BUFFER                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief One-sector SRAM staging buffer used as flash program source.
 *
 * Holds one sector's slice of the mirror image (16-byte header, .uninit
 * contents, 0xFF padding).  The flush stages and commits the mirror through
 * it one sector at a time, so a mirror of several sectors needs no more SRAM.
 */
static uint8_t g_flush_buf[RP2350_NVM_SECTOR_SIZE]
    __attribute__((aligned(4)));

/** @brief Flushes that erased and programmed the mirror since boot. */
static uint32_t g_nvm_flush_programs;

/** @brief Return the number of flushes that wrote flash since boot. */
uint32_t tiku_mem_arch_nvm_program_count(void)
{
    return g_nvm_flush_programs;
}

/** @brief Boot-time mirror-restore outcome (see tiku_nvm_restore_t). */
static tiku_nvm_restore_t g_nvm_restore;

/** @brief Return how tiku_mem_arch_init() treated the mirror at boot. */
tiku_nvm_restore_t tiku_mem_arch_nvm_restore_status(void)
{
    return g_nvm_restore;
}

/** @brief Base of the XIP-mapped flash mirror, for tests/diagnostics. */
const uint8_t *tiku_mem_arch_nvm_mirror(void)
{
    return (const uint8_t *)__tiku_nvm_flash_start;
}

/** @brief The mirror's image when it checks out (see tiku_mem_hal.h). */
const uint8_t *tiku_mem_arch_durable(size_t *len)
{
    return tiku_nvm_mirror_image(__tiku_nvm_flash_start,
                                 (size_t)(uintptr_t)&__tiku_nvm_flash_size,
                                 len);
}

/** @brief The .uninit window durable variables live in (see tiku_mem_hal.h). */
uint8_t *tiku_mem_arch_durable_live(size_t *len)
{
    *len = (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);
    return (uint8_t *)&__uninit_start;
}

/**
 * @brief Erase and program one flash sector via boot-ROM helpers.
 *
 * Masks all interrupts around the operation, because XIP is suspended during
 * erase and program and an ISR fetching code from flash would bus-fault.
 * Without the boot-ROM helpers it returns -1 and leaves flash untouched.
 *
 * @param flash_offset  Byte offset from the start of flash for the sector.
 * @param src           SRAM buffer to program (must be at least len bytes).
 * @param len           Number of bytes to program (typically one sector).
 * @return 0 when the sector reads back equal to @p src, -1 otherwise.
 */
static int flash_commit_sector(uint32_t flash_offset,
                                const uint8_t *src,
                                size_t        len) {
    uint32_t primask;

    if (!rom_flash_ready()) {
        return -1;
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
    /* ROM calls have no result; verify through XIP after cache flush/remap. */
    return memcmp((const void *)(uintptr_t)(0x10000000UL + flash_offset),
                   src, len) == 0 ? 0 : -1;
}

/**
 * @brief Public: erase + program one flash sector via the boot-ROM helpers.
 *
 * flash_commit_sector() with its result discarded; the BASIC module installer
 * (tiku_basic_module.c) uses it.  tiku_rp2350_flash_commit_sector_status()
 * returns the result.
 *
 * @param flash_offset  Sector-aligned byte offset from the start of flash.
 * @param src           Replacement sector contents (SRAM).
 * @param len           Bytes to program (typically one whole sector).
 */
void tiku_rp2350_flash_commit_sector(uint32_t flash_offset,
                                     const uint8_t *src, size_t len) {
    (void)flash_commit_sector(flash_offset, src, len); /* result unused */
}

/**
 * @brief Public: erase + program one flash sector and report the result.
 *
 * The carved NVM region backend (tiku_nvm_region_rp2350.c) uses it.
 *
 * @param flash_offset  Sector-aligned byte offset from the start of flash.
 * @param src           Replacement sector contents (SRAM).
 * @param len           Bytes to program (typically one whole sector).
 * @return 0 when the sector reads back equal to @p src, -1 otherwise.
 */
int tiku_rp2350_flash_commit_sector_status(uint32_t flash_offset,
                                           const uint8_t *src, size_t len)
{
    return flash_commit_sector(flash_offset, src, len);
}

/**
 * @brief Public: program flash without erasing it, via the boot-ROM helpers.
 *
 * Masks interrupts and suspends XIP as the sector commit does, but skips the
 * erase.  The module installer uses it to program the header page it left
 * erased while it committed the rest of the slot.
 *
 * @note @p flash_offset and @p len must satisfy the boot-ROM's 256-byte program
 *       alignment.  Programming cells that are not erased does not set bits;
 *       flash can only clear them.
 */
void tiku_rp2350_flash_program(uint32_t flash_offset,
                               const uint8_t *src, size_t len) {
    uint32_t primask;

    if (!rom_flash_ready()) {
        return;
    }
    __asm__ volatile ("mrs %0, primask" : "=r"(primask));
    __asm__ volatile ("cpsid i" ::: "memory");

    g_rom_connect_flash();
    g_rom_flash_exit_xip();
    g_rom_flash_range_program(flash_offset, src, len);
    g_rom_flash_flush_cache();
    g_rom_flash_enter_xip();

    __asm__ volatile ("msr primask, %0" : : "r"(primask) : "memory");
}

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the RP2350 memory architecture and restore durable state.
 *
 * Resolves boot-ROM flash function pointers, then copies the mirror back into
 * the SRAM .uninit region when its image checks out, so durable state
 * survives a power cycle.  With no such image .uninit is zeroed.
 */
void tiku_mem_arch_init(void) {
    const uint32_t *flash = (const uint32_t *)__tiku_nvm_flash_start;
    size_t uninit_size =
        (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);

    /* Resolve the ROM functions here, so the first flush does not pay for
     * the lookup.  rom_resolve_once() runs its lookups once. */
    rom_resolve_once();

    /* A V2 mirror is restored only when its CRC checks out.  A power cut
     * during a flush (erase, then program) leaves an image that fails the
     * check; .uninit is then zeroed and each subsystem's first-boot setup
     * runs.  A V1 mirror, which has no CRC, is restored, and the next flush
     * rewrites it as V2.
     *
     * tiku_mem_init() can run again after the MPU is armed, when region 0
     * is read-only, so the restore writes .uninit inside the arch unlock
     * window, which does not flush and nests. */
    {
        uint16_t mpu_saved = tiku_mpu_arch_unlock_nvm();
        size_t len;
        const uint8_t *img = tiku_mem_arch_durable(&len);

    if (img != NULL) {
        memcpy(&__uninit_start, img, (uninit_size < len) ? uninit_size : len);
        if (uninit_size > len) {  /* this image grew: new cells start blank */
            memset(&__uninit_start + len, 0, uninit_size - len);
        }
        g_nvm_restore = TIKU_NVM_RESTORE_V2_OK;
    } else if (flash[0] == TIKU_NVM_MIRROR_MAGIC_V1) {
        if (uninit_size > (RP2350_NVM_SECTOR_SIZE - 4U)) {
            uninit_size = (RP2350_NVM_SECTOR_SIZE - 4U);
        }
        memcpy(&__uninit_start, (const uint8_t *)&flash[1], uninit_size);
        g_nvm_restore = TIKU_NVM_RESTORE_V1;
    } else {
        /* A fresh part or a torn flush: zero .uninit.  SRAM survives a
         * warm reset, and its old contents are not a restored image. */
        memset(&__uninit_start, 0, uninit_size);
        g_nvm_restore =
            (flash[TIKU_NVM_MIRROR_W_MAGIC] == TIKU_NVM_MIRROR_MAGIC_V2)
                ? TIKU_NVM_RESTORE_CRC_FAIL : TIKU_NVM_RESTORE_VIRGIN;
    }

        tiku_mpu_arch_lock_nvm(mpu_saved);
    }
}

/**
 * @brief Securely zero a memory buffer through a volatile pointer.
 *
 * Uses a volatile store loop to prevent the compiler from optimising
 * away the zeroing, which is required when clearing key material.
 *
 * @param buf  Pointer to the buffer to wipe.
 * @param len  Number of bytes to zero.
 */
void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)buf;
    tiku_mem_arch_size_t i;
    for (i = 0; i < len; i++) {
        p[i] = 0U;
    }
}

/**
 * @brief Read bytes from the NVM-backed SRAM working copy.
 *
 * The .uninit SRAM region is the live working copy of durable state; flash
 * holds the copy from the last flush.  This function is a plain byte-copy
 * from the SRAM copy.
 *
 * @param dst  Destination buffer.
 * @param src  Source address within the .uninit region.
 * @param len  Number of bytes to copy.
 */
void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len) {
    tiku_mem_arch_size_t i;
    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

/**
 * @brief Stage bytes into the SRAM working copy for deferred flash commit.
 *
 * Copies into the .uninit SRAM region only; the flush at the matching MPU
 * relock commits the mirror, once per unlock window however many writes the
 * window held.
 *
 * @param dst  Destination address within the .uninit region.
 * @param src  Source buffer.
 * @param len  Number of bytes to copy.
 */
void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                              tiku_mem_arch_size_t len) {
    /* SRAM only.  The flash commit runs at the tiku_mpu_lock_nvm() relock,
     * so stores made directly to .persistent variables in the same unlock
     * window land in the same snapshot. */
    tiku_mem_arch_size_t i;
    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

/**
 * @brief Flush the SRAM .uninit region to the flash mirror sector.
 *
 * Stages the 16-byte header (magic, CRC, length) and the region in
 * g_flush_buf, then erases and programs the mirror sectors; an unchanged
 * image writes nothing.
 *
 * @return 0 when the mirror holds .uninit, -1 for a layout that does not fit,
 *         missing boot-ROM helpers or a sector that does not verify, which
 *         can leave a partial mirror.
 */
int tiku_mem_arch_nvm_flush_status(void) {
    /* Every store to .persistent / .uninit reaches flash through this
     * flush.  The mirror is __tiku_nvm_flash_size bytes, a whole number of
     * 4 KB erase sectors.  Its image is the 16-byte header, the .uninit
     * contents and 0xFF padding; each sector's slice is staged in
     * g_flush_buf and committed in ascending order.  A power cut mid-commit
     * leaves an image that fails its check at boot, and .uninit then
     * starts blank. */
    const uint32_t *mirror = (const uint32_t *)__tiku_nvm_flash_start;
    const uint8_t  *mirror8 = (const uint8_t *)__tiku_nvm_flash_start;
    uint32_t mirror_bytes = (uint32_t)(uintptr_t)&__tiku_nvm_flash_size;
    size_t   uninit_size =
        (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);
    const uint8_t *uninit = (const uint8_t *)&__uninit_start;
    uint32_t crc;
    uint32_t off;

    if (mirror_bytes < TIKU_NVM_MIRROR_HDR_BYTES ||
        mirror_bytes % RP2350_NVM_SECTOR_SIZE != 0 ||
        uninit_size > mirror_bytes - TIKU_NVM_MIRROR_HDR_BYTES) {
        return -1;
    }

    /* Dirty check: nothing is written when the XIP-mapped mirror's magic
     * and length words match and its image bytes equal .uninit.  The CRC
     * word follows from the bytes, and equal lengths give equal 0xFF
     * padding.  The XIP read is coherent: every commit ends with the boot
     * ROM's cache flush, and a cold boot starts with a cold cache.  A
     * skipped commit saves an interrupts-off erase and program and one
     * sector erase cycle, so a relock whose window left .uninit unchanged
     * costs no flash wear. */
    if (mirror[TIKU_NVM_MIRROR_W_MAGIC] == TIKU_NVM_MIRROR_MAGIC_V2 &&
        mirror[TIKU_NVM_MIRROR_W_LEN]   == (uint32_t)uninit_size &&
        memcmp(uninit, mirror8 + TIKU_NVM_MIRROR_HDR_BYTES,
               uninit_size) == 0) {
        return 0;
    }

    crc = tiku_nvm_crc32(uninit, (uint32_t)uninit_size);

    /* Stage + commit each sector's slice of the virtual image. */
    for (off = 0; off < mirror_bytes; off += RP2350_NVM_SECTOR_SIZE) {
        uint32_t i;
        for (i = 0; i < RP2350_NVM_SECTOR_SIZE; i++) {
            uint32_t v = off + i;               /* virtual image offset */
            if (v < TIKU_NVM_MIRROR_HDR_BYTES) {
                g_flush_buf[i] = 0;             /* header patched below */
            } else if (v - TIKU_NVM_MIRROR_HDR_BYTES < uninit_size) {
                g_flush_buf[i] = uninit[v - TIKU_NVM_MIRROR_HDR_BYTES];
            } else {
                g_flush_buf[i] = 0xFFu;         /* post-erase padding   */
            }
        }
        if (off == 0) {
            uint32_t *hdr = (uint32_t *)(void *)g_flush_buf;
            hdr[TIKU_NVM_MIRROR_W_MAGIC] = TIKU_NVM_MIRROR_MAGIC_V2;
            hdr[TIKU_NVM_MIRROR_W_CRC]   = crc;
            hdr[TIKU_NVM_MIRROR_W_LEN]   = (uint32_t)uninit_size;
            hdr[TIKU_NVM_MIRROR_W_RSVD]  = 0xFFFFFFFFu;
        }
        if (flash_commit_sector((uint32_t)(uintptr_t)&__tiku_nvm_flash_offset
                                + off,
                            g_flush_buf, RP2350_NVM_SECTOR_SIZE) != 0) {
            return -1;
        }
    }
    g_nvm_flush_programs++;
    return 0;
}

/** @brief tiku_mem_arch_nvm_flush_status() with the result discarded. */
void tiku_mem_arch_nvm_flush(void)
{
    (void)tiku_mem_arch_nvm_flush_status();
}
