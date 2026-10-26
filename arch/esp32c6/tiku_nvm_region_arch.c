/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_arch.c - C6 sector-preserving mapped NVM region writes.
 *
 * The NVM region backend is the REGION flash range, read through the
 * mapping at 0x42000000 and written sector by sector; a 4 KiB sector is
 * erased only when a bit must go from 0 to 1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel/memory/tiku_nvm_region.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/memory/tiku_nvm_mirror.h>
#include <string.h>
#include "tiku_flash_arch.h"
#include "tiku_native_arch.h"
#include <hal/tiku_mem_hal.h>
#define TIKU_C6_IRQ_SAVE() native_irq_save()
#define TIKU_C6_IRQ_RESTORE(s) native_irq_restore(s)
#define tiku_c6_in_isr() TIKU_MEM_ARCH_IN_EXCEPTION()
#define tiku_c6_mmap_ready() (tiku_c6_flash_jedec()!=0)

static uint8_t sector[TIKU_C6_FLASH_SECTOR_SIZE];
static int writing;
#define REGION_PENDING 0xc6524543u
typedef struct {
    uint32_t pending,offset,check,reserved;
    uint8_t data[4096];
} region_recovery_t;
#ifndef TIKU_C6_JOURNAL
static TIKU_DURABLE_ARCH region_recovery_t recovery;
#define TIKU_C6_JOURNAL recovery
#endif
static uint8_t journal_failed,provisioning;

/** @brief Commit an undo sector in the alternating durable mirror before target erase. */
static int journal_begin(uint32_t offset)
{
    uint16_t saved;
    if(journal_failed||TIKU_C6_JOURNAL.pending)return -1;
    saved=tiku_mpu_unlock_nvm();
    memcpy(TIKU_C6_JOURNAL.data,(const void *)(uintptr_t)(TIKU_C6_FLASH_VIRTUAL+offset),4096);
    TIKU_C6_JOURNAL.offset=offset;
    TIKU_C6_JOURNAL.check=tiku_nvm_crc32(TIKU_C6_JOURNAL.data,4096);
    TIKU_C6_JOURNAL.reserved=0;TIKU_C6_JOURNAL.pending=REGION_PENDING;
    if(tiku_mpu_lock_nvm_status(saved)!=TIKU_MEM_OK){journal_failed=1;return -1;}
    return 0;
}
/** @brief Durably clear a completed recovery record before acknowledging the write. */
static int journal_end(void)
{
    uint16_t saved=tiku_mpu_unlock_nvm();TIKU_C6_JOURNAL.pending=0;
    if(tiku_mpu_lock_nvm_status(saved)!=TIKU_MEM_OK){journal_failed=1;return -1;}
    return 0;
}
int tiku_c6_region_recover(void)
{
    uint32_t offset=TIKU_C6_JOURNAL.offset;
    if(TIKU_MEM_ARCH_IN_EXCEPTION()||!tiku_c6_flash_jedec())return -1;
    if(TIKU_C6_JOURNAL.pending){
        if(TIKU_C6_JOURNAL.pending!=REGION_PENDING||TIKU_C6_JOURNAL.reserved||
            (offset&4095u)||offset<TIKU_C6_FLASH_REGION_START||
            offset>TIKU_C6_FLASH_REGION_END-4096||
            tiku_nvm_crc32(TIKU_C6_JOURNAL.data,4096)!=TIKU_C6_JOURNAL.check)return -1;
        if(tiku_c6_flash_erase_sector(offset)||
            tiku_c6_flash_program(offset,TIKU_C6_JOURNAL.data,4096)||journal_end())return -1;
    }
    const uint8_t *base=(const uint8_t *)(uintptr_t)(TIKU_C6_FLASH_VIRTUAL+TIKU_C6_FLASH_REGION_START);
    provisioning=1;
    for(uint32_t n=0;n<TIKU_C6_FLASH_REGION_END-TIKU_C6_FLASH_REGION_START;n++)
        if(base[n]!=255){provisioning=0;break;}
    return 0;
}
void tiku_c6_region_seal(void){provisioning=0;}


/** @brief Rewrite affected sectors, preserving bytes outside the request.
 * @note Requires a stable readable source. Sources overlapping any affected
 * sector are refused before writing. Erases and aligned word updates keep
 * an undo sector in the durable mirror; boot replays an interrupted rewrite.
 * Multi-sector requests can persist a prefix. Initial blank provisioning
 * writes carry no existing data; its ownership service controls recovery.
 */
static int region_write(tiku_nvm_backend_t *be, size_t off, const void *data,
                         size_t len)
{
    const uint8_t *src = data;
    uintptr_t first, end, source = (uintptr_t)data;
    size_t at, part, i, within;
    uint32_t state;
    int result = -1, erase, changed;
    if (!be || !src ||
        (uintptr_t)be->base != TIKU_C6_FLASH_VIRTUAL + TIKU_C6_FLASH_REGION_START ||
        be->size != TIKU_C6_FLASH_REGION_END - TIKU_C6_FLASH_REGION_START ||
        off > be->size || len > be->size - off ||
        len > UINTPTR_MAX - source || tiku_c6_in_isr() ||
        !tiku_c6_flash_jedec() || !tiku_c6_mmap_ready() || journal_failed || TIKU_C6_JOURNAL.pending) return -1;
    if (!len) return 0;
    first = (uintptr_t)be->base + off / sizeof(sector) * sizeof(sector);
    end = (uintptr_t)be->base + ((off + len + sizeof(sector) - 1) /
                               sizeof(sector)) * sizeof(sector);
    if (source < end && source + len > first) return -1;
    state = TIKU_C6_IRQ_SAVE();
    if (writing) { TIKU_C6_IRQ_RESTORE(state); return -1; }
    writing = 1;
    TIKU_C6_IRQ_RESTORE(state);
    while (len) {
        at = off / sizeof(sector) * sizeof(sector);
        within = off - at;
        part = sizeof(sector) - within;
        if (part > len) part = len;
        if (tiku_c6_flash_read(TIKU_C6_FLASH_REGION_START + at,
                               sector, sizeof(sector))) goto done;
        erase = changed = 0;
        for (i = 0; i < part; i++) {
            uint8_t value = src[i];
            if (sector[within + i] != value) changed = 1;
            if ((sector[within + i] & value) != value) erase = 1;
            sector[within + i] = value;
        }
        if (changed) {
            int journal=!provisioning&&(erase||(part==4&&!(off&3u)));
            if(journal&&journal_begin(TIKU_C6_FLASH_REGION_START+(uint32_t)at))goto done;
            if (erase && tiku_c6_flash_erase_sector(TIKU_C6_FLASH_REGION_START + at)) goto done;
            if (tiku_c6_flash_program(TIKU_C6_FLASH_REGION_START + (erase ? at : off),
                                      sector + (erase ? 0 : within),
                                      erase ? sizeof(sector) : part)) goto done;
            if(journal&&journal_end())goto done;
        }
        off += part; src += part; len -= part;
    }
    result = 0;
done:
    state = TIKU_C6_IRQ_SAVE();
    writing = 0;
    TIKU_C6_IRQ_RESTORE(state);
    return result;
}

const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    static tiku_nvm_backend_t backend = {
        (uint8_t *)(uintptr_t)(TIKU_C6_FLASH_VIRTUAL + TIKU_C6_FLASH_REGION_START),
        TIKU_C6_FLASH_REGION_END - TIKU_C6_FLASH_REGION_START,
        region_write, NULL, NULL
    };
    return tiku_c6_flash_jedec() && tiku_c6_mmap_ready() ? &backend : NULL;
}
