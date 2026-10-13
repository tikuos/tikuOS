/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_fault_arch.c - Retained CPU0 faults with queue-independent USB output.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_fault_arch.h"
#include "tiku_esp32p4_regs.h"
#include "kernel/memory/tiku_mem.h"

#define FAULT_MAGIC 0x50344654u
#define FAULT_VERSION 1u
#ifndef TIKU_P4_FAULT_FENCE
#define TIKU_P4_FAULT_FENCE() __asm__ volatile("fence rw, rw" ::: "memory")
#endif
#ifndef TIKU_P4_FAULT_HALT
#define TIKU_P4_FAULT_HALT() do { for (;;) __asm__ volatile("wfi"); } while (0)
#endif

static volatile tiku_p4_fault_record_t retained TIKU_RETAINED;
_Static_assert(sizeof(retained) == 64, "P4 retained record size");

/** @brief Compute reflected CRC-32 over 14 little-endian payload words. */
static uint32_t checksum(const tiku_p4_fault_record_t *record)
{
    const unsigned char *bytes = (const unsigned char *)record;
    uint32_t crc = 0xffffffffu;
    unsigned i, bit;
    for (i = 4; i < 60; i++) {
        crc ^= bytes[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

int tiku_p4_fault_last(tiku_p4_fault_record_t *record)
{
    const volatile unsigned char *from = (const volatile unsigned char *)&retained;
    unsigned char *to = (unsigned char *)record;
    unsigned i;
    if (record == 0 || retained.magic != FAULT_MAGIC) return 0;
    for (i = 0; i < sizeof(*record); i++) to[i] = from[i];
    return record->magic == FAULT_MAGIC && record->version == FAULT_VERSION &&
           record->sequence != 0 && record->reserved[0] == 0 &&
           record->reserved[1] == 0 && record->reserved[2] == 0 &&
           record->crc == checksum(record);
}

void tiku_p4_fault_clear(void)
{
    retained.magic = 0;
    TIKU_P4_FAULT_FENCE();
}

/** @brief Commit payload and checksum before publishing its valid marker. */
static void save(const tiku_p4_fault_record_t *record)
{
    const unsigned char *from = (const unsigned char *)record;
    volatile unsigned char *to = (volatile unsigned char *)&retained;
    unsigned i;
    tiku_p4_fault_clear();
    for (i = 4; i < sizeof(*record); i++) to[i] = from[i];
    TIKU_P4_FAULT_FENCE();
    retained.magic = FAULT_MAGIC;
    TIKU_P4_FAULT_FENCE();
}

/** @brief Send a bounded string in USB packets without libc or console state. */
static void output(const char *text)
{
    unsigned budget = 10000000u, count;
    do {
        count = 0;
        while (!(TIKU_P4_REG_READ(TIKU_P4_USB_EP1_CONF) & 2u))
            if (--budget == 0) return;
        while (*text && count < 64) {
            if (!(TIKU_P4_REG_READ(TIKU_P4_USB_EP1_CONF) & 2u)) {
                if (--budget == 0) return;
                continue;
            }
            TIKU_P4_REG_WRITE(TIKU_P4_USB_EP1, (unsigned char)*text++);
            count++;
        }
        TIKU_P4_REG_WRITE(TIKU_P4_USB_EP1_CONF, 1u);
    } while (*text || count == 64);
}

/** @brief Append a constant string to the fixed-size fault line. */
static char *append(char *at, const char *text)
{
    while (*text) *at++ = *text++;
    return at;
}

/** @brief Format fixed-width fields; the caller supplies a validated local record. */
static void report(const char *prefix, const tiku_p4_fault_record_t *r)
{
    static const char *const names[] = {
        "version", "sequence", "cause", "pc", "value", "sp", "status",
        "hart", "reset", "ra", "flags", "crc"
    };
    const uint32_t values[] = { r->version, r->sequence, r->cause, r->pc,
        r->value, r->sp, r->status, r->hart, r->reset, r->ra, r->flags, r->crc };
    static const char digits[] = "0123456789abcdef";
    char text[256], *at = append(text, prefix);
    unsigned i, shift;
    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        at = append(at, " "); at = append(at, names[i]); at = append(at, "=0x");
        for (shift = 0; shift < 8; shift++)
            *at++ = digits[(values[i] >> (28 - 4 * shift)) & 15];
    }
    at = append(at, "\r\n"); *at = 0;
    output(text);
}

void tiku_p4_fault_report(void)
{
    tiku_p4_fault_record_t record;
    if (tiku_p4_fault_last(&record)) report("\r\nP4FAULT", &record);
    else output("\r\nP4FAULT NONE\r\n");
}

void tiku_p4_fault_capture(uint32_t cause, uint32_t pc, uint32_t value,
                           uint32_t sp, uint32_t status, uint32_t hart,
                           uint32_t ra, uint32_t flags)
{
    tiku_p4_fault_record_t record;
    uint32_t sequence = tiku_p4_fault_last(&record) ? record.sequence : 0;
    record.magic = FAULT_MAGIC;
    record.version = FAULT_VERSION;
    record.sequence = sequence == UINT32_MAX ? sequence : sequence + 1;
    record.cause = cause; record.pc = pc; record.value = value; record.sp = sp;
    record.status = status; record.hart = hart; record.ra = ra; record.flags = flags;
    record.reset = TIKU_P4_REG_READ(TIKU_P4_RESET_CAUSE);
    record.reserved[0] = record.reserved[1] = record.reserved[2] = 0;
    record.crc = checksum(&record);
    save(&record);
    report("\r\nTRAP", &record);
    TIKU_P4_FAULT_HALT();
}
