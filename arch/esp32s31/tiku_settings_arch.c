/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_settings_arch.c - CRC-checked flash records with a final commit word.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_settings_arch.h"
#include "tiku_settings_internal.h"
#include <kernel/memory/tiku_nvm_mirror.h>
#include <stdint.h>
#include <string.h>
#define SETTINGS_BASE 0x00ffe000u
#define SECTOR_BYTES 4096u
#define RECORD_MAGIC 0x31534354u
typedef struct {
    uint32_t magic, generation, length, crc;
    uint8_t data[TIKU_S31_SETTINGS_BYTES];
} settings_record_t;
static settings_record_t records[2], next;
static uint32_t scratch[SECTOR_BYTES / 4];
static unsigned busy;
/** @brief Exclude IRQs, workers and recursive operations. */
static int enter(void)
{
    if (!tiku_s31_settings_context() || busy) return -1;
    busy = 1;
    return 0;
}
/** @brief Require the reservation to lie outside every declared flash partition. */
static int reservation(void)
{
    if (tiku_s31_settings_io(0, 0x8000, scratch, sizeof scratch)) return -1;
    unsigned partitions = 0;
    for (unsigned i = 0; i < 0xc00 / 4; i += 8) {
        unsigned magic = scratch[i] & 0xffffu;
        if (magic == 0xffffu || magic == 0xebebu) return partitions ? 0 : -1;
        if (magic != 0x50aau) return -1;
        uint32_t start = scratch[i + 1], size = scratch[i + 2];
        if (!size || start > SETTINGS_BASE || size > SETTINGS_BASE - start) return -1;
        partitions++;
    }
    return -1;
}
/** @brief Validate record geometry and integrity before using its generation. */
static int valid(const settings_record_t *r)
{
    if (r->magic != RECORD_MAGIC || r->length > sizeof r->data) return 0;
    uint32_t crc = tiku_nvm_crc32_update(UINT32_MAX, &r->generation, 8);
    return (tiku_nvm_crc32_update(crc, r->data, sizeof r->data) ^ UINT32_MAX) == r->crc;
}
/** @brief Read both slots; return -2 for damaged storage, -1 for empty, or the newest slot. */
static int select_record(void)
{
    if (reservation()) return -2;
    int selected = -1;
    unsigned blank = 0;
    for (unsigned i = 0; i < 2; i++) {
        if (tiku_s31_settings_io(0, SETTINGS_BASE + i * SECTOR_BYTES, scratch, sizeof scratch)) return -2;
        memcpy(&records[i], scratch, sizeof records[i]);
        unsigned erased = 1;
        for (unsigned j = 0; j < SECTOR_BYTES / 4; j++) erased &= scratch[j] == UINT32_MAX;
        blank += erased;
        if (!valid(&records[i])) continue;
        if (selected >= 0) {
            uint32_t delta = records[i].generation - records[selected].generation;
            if (delta == 0x80000000u || (!delta && memcmp(&records[i], &records[selected], sizeof records[i])))
                return -2;
            if (delta && delta < 0x80000000u) selected = (int)i;
        } else selected = (int)i;
    }
    return selected < 0 && blank != 2 ? -2 : selected;
}
int tiku_s31_settings_read(void *data, size_t capacity, size_t *length)
{
    if (!length || (!data && capacity) || enter()) return -1;
    *length = 0;
    int selected = select_record(), rc = -1;
    if (selected == -1 || (selected >= 0 && !records[selected].length)) rc = 1;
    else if (selected >= 0 && records[selected].length <= capacity) {
        *length = records[selected].length;
        memcpy(data, records[selected].data, *length); rc = 0;
    }
    busy = 0;
    return rc;
}
int tiku_s31_settings_write(const void *data, size_t length)
{
    if ((!data && length) || length > TIKU_S31_SETTINGS_BYTES || enter()) return -1;
    int selected = select_record(), rc = -1;
    if (selected != -2) {
        unsigned slot = selected < 0 ? 0 : (unsigned)selected ^ 1u;
        uint32_t address = SETTINGS_BASE + slot * SECTOR_BYTES;
        memset(&next, 0, sizeof next);
        next.magic = RECORD_MAGIC;
        next.generation = selected < 0 ? 1 : records[selected].generation + 1;
        next.length = length;
        if (length) memcpy(next.data, data, length);
        uint32_t crc = tiku_nvm_crc32_update(UINT32_MAX, &next.generation, 8);
        next.crc = tiku_nvm_crc32_update(crc, next.data, sizeof next.data) ^ UINT32_MAX;
        rc = tiku_s31_settings_io(1, address, NULL, 0);
        if (!rc) rc = tiku_s31_settings_io(2, address + 4, (uint8_t *)&next + 4, sizeof next - 4);
        if (!rc) rc = tiku_s31_settings_io(0, address + 4, scratch, sizeof next - 4);
        if (!rc && memcmp(scratch, (uint8_t *)&next + 4, sizeof next - 4)) rc = -1;
        if (!rc) rc = tiku_s31_settings_io(2, address, &next.magic, 4);
        if (!rc) rc = tiku_s31_settings_io(0, address, scratch, sizeof next);
        if (!rc && memcmp(scratch, &next, sizeof next)) rc = -1;
        if (!rc && selected >= 0) rc = tiku_s31_settings_io(1, SETTINGS_BASE + (unsigned)selected * SECTOR_BYTES, NULL, 0);
    }
    busy = 0;
    return rc;
}
