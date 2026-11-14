/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.c - Korvo octal PSRAM at 20 MHz from the 40 MHz crystal.
 *
 * Register sequencing follows ESP-IDF 4d59230 AP octal, MSPI, PMA and cache.
 *
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tiku.h>
#include "tiku_psram_arch.h"
#include "tiku_runtime_arch.h"

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define SYS 0x20502000u
#define PIO 0x20503000u
#define PADS 0x20584000u
#define CLOCK 0x20587068u
#define LDO 0x20704218u
#define POWER 0x207041e8u
#define CLOSE (1u << 31)
#define SYNC 0x2c00009cu
#define POLLS 65536u
#define PMA_ADDRESS 0x141fffffu
#define PMA_CONFIG 0xc0000019u

/** @brief ROM SPI command layout; buffers reside in internal SRAM. */
typedef struct {
    uint16_t command, command_bits;
    uint32_t *address;
    uint32_t address_bits;
    uint32_t *tx;
    uint32_t tx_bits;
    uint32_t *rx;
    uint32_t rx_bits, dummy_bits;
} ps_command_t;

static uint32_t capacity;
static unsigned stage, attempted;

/** @brief Update a register field without changing unrelated bits. */
static void field(uint32_t address, uint32_t mask, uint32_t value)
{
    REG(address) = (REG(address) & ~mask) | (value & mask);
}

/** @brief Bound a register wait by poll count and ten milliseconds. */
static int wait_bits(uint32_t address, uint32_t mask, uint32_t expected)
{
    uint64_t begin = tiku_s31_time();
    for (unsigned n = 0; n < POLLS; n++) {
        if ((REG(address) & mask) == expected) return 0;
        if ((n & 63u) == 0 &&
            ((tiku_s31_time() - begin) & ((1ULL << 52) - 1)) >= 160000u)
            break;
    }
    return -1;
}

/** @brief Transfer one 2..64-byte octal command while CPU mapping is closed. */
static int transfer(uint16_t command, uint32_t address, unsigned dummy,
                    uint32_t *data, unsigned bytes, int write)
{
    ps_command_t config = {command, 16, &address, 32,
        write ? data : 0, write ? bytes * 8 : 0,
        write ? 0 : data, write ? 0 : bytes * 8, dummy};
    if (wait_bits(PIO, 0xfffe0000u, 0)) return -1;
    ((void (*)(int, int))0x2f800124u)(3, 7);
    ((void (*)(int, ps_command_t *))0x2f80011cu)(3, &config);
    field(PIO + 0x34, 3, 1);
    REG(PIO) = 1u << 18;
    if (wait_bits(PIO, 0xfffe0000u, 0)) return -1;
    if (!write) for (unsigned i = 0; i < (bytes + 3) / 4; i++)
        data[i] = REG(PIO + 0x58 + i * 4);
    return 0;
}

/** @brief Configure the 1.8 V supply, dedicated pads and both 20 MHz engines. */
static void controller(void)
{
    /* Dref 6 and multiplier 5 select 1.8 V; bypass remains disabled. */
    field(LDO, 0x3ffu, (6u << 6) | (5u << 3) | 6u);
    field(POWER, 1u << 31, 1u << 31);
    field(LDO, 4u, 0);
    tiku_clock_arch_delay(10000);
    field(CLOCK, 0x1ffffu, 0x187u);
    field(CLOCK, 6u, 0);
    field(SYS + 0x3c, CLOSE | 1, CLOSE);
    for (unsigned offset = 0x1c; offset <= 0x44; offset += 4) {
        unsigned shift = offset == 0x3c ? 15 : 12;
        field(PADS + offset, 7u << shift, 2u << shift);
        if (offset != 0x3c) field(PADS + offset, 0xf0u, 0);
    }
    field(PADS + 0x3c, 0x003c07ffu, 1);
    field(SYS + 0x1a0, 0xfe01ffffu, 0x8400018fu);
    field(SYS + 0x174, 0x001c0000u, 3u << 18);
    REG(SYS + 0x50) = 0x10001u;
    REG(PIO + 0x14) = 0x10001u;
    field(SYS + 0x190, 1u << 5, 1u << 5);
    field(SYS + 0x180, 1u << 5, 1u << 5);
}

/** @brief Set fixed latency and octal bursts, then verify AP identity and capacity. */
static int device(void)
{
    uint32_t value, mr01, mr23, mr48, mr8;
    value = 0x28;
    if (transfer(0xc0c0, 0, 0, &value, 2, 1)) return -1;
    value = 0x40;
    if (transfer(0xc0c0, 4, 0, &value, 2, 1)) return -1;
    value = 0x0b;
    if (transfer(0xc0c0, 8, 0, &value, 2, 1)) return -1;
    if (transfer(0x4040, 0, 8, &mr01, 2, 0) ||
        transfer(0x4040, 2, 8, &mr23, 2, 0) ||
        transfer(0x4040, 4, 8, &mr48, 2, 0) ||
        transfer(0x4040, 8, 8, &mr8, 2, 0)) return -1;
    return ((mr01 & 0xffu) == 0x28u && ((mr01 >> 8) & 31u) == 13u &&
            (mr23 & 0xe7u) == 0xc5u && (mr48 & 0xffu) == 0x40u &&
            (mr8 & 0xffu) == 0x0bu) ? 0 : -1;
}

/** @brief Configure mapped octal transactions and the 256 physical 64-KiB pages. */
static int mapping(void)
{
    REG(SYS + 0x48) = 0xf0000000u;
    REG(SYS + 0x4c) = 0xf0008080u;
    field(SYS + 0x40, 0x0fffffffu,
          1u | 0x38u | (17u << 6) | (31u << 14) | (3u << 20) | (7u << 22));
    field(SYS + 0x44, 0x0ffffc00u, (0xfu << 18) | (1u << 23));
    field(SYS + 0xd8, 15, 3);
    field(PIO + 0xd4, 2, 2);
    field(SYS + 0x0c, 3u << 25, 3u << 25);
    field(SYS + 0x384, 3u << 3, 0);
    for (unsigned page = 0; page < 1024; page++) {
        uint32_t value = page < 256 ? 0xc00u | page : 0;
        REG(SYS + 0x380) = page;
        REG(SYS + 0x37c) = value;
        if (REG(SYS + 0x37c) != value) return -1;
    }
    return 0;
}

int tiku_s31_psram_init(void)
{
    uint32_t irq, hart, old_cfg, old_addr, got;
    uint32_t pattern[16], received[16];
    __asm__ volatile ("csrr %0,mstatus\ncsrr %1,mhartid" : "=r"(irq), "=r"(hart));
    if ((irq & 8u) || hart || tiku_s31_in_trap) return -1;
    if (capacity) return 0;
    if (attempted) return -1;
    attempted = 1;
    __asm__ volatile ("csrr %0,0xbc0\ncsrr %1,0xbd0" : "=r"(old_cfg), "=r"(old_addr));
    if (old_cfg & (1u << 29)) return -1;
    stage = 1;
    controller();
    stage = 2;
    if (device()) return -1;
    for (unsigned i = 0; i < 16; i++) pattern[i] = 0x5a6b7c8du ^ (i * 0x1234567u);
    if (transfer(0x8080, 0, 8, pattern, 64, 1) ||
        transfer(0, 0, 18, received, 64, 0)) return -1;
    for (unsigned i = 0; i < 16; i++) if (pattern[i] != received[i]) return -1;
    stage = 3;
    ((uint32_t (*)(void))0x2f8006b4u)();
    if (mapping() || ((int (*)(unsigned, uint32_t, uint32_t))0x2f8005e8u)
        (16, TIKU_S31_PSRAM_BASE, TIKU_S31_PSRAM_BYTES)) goto failed;
    __asm__ volatile ("csrw 0xbc0,zero\ncsrw 0xbd0,%0\ncsrw 0xbc0,%1"
                      :: "r"(PMA_ADDRESS), "r"(PMA_CONFIG) : "memory");
    __asm__ volatile ("csrr %0,0xbc0" : "=r"(got));
    if (got != PMA_CONFIG) goto failed;
    __asm__ volatile ("csrr %0,0xbd0" : "=r"(got));
    if (got != PMA_ADDRESS) goto failed;
    field(SYS + 0x3c, CLOSE | 1, 1);
    field(0x2c000004u, 1, 0);
    ((void (*)(unsigned))0x2f8006b0u)(0);
    __asm__ volatile ("fence iorw,iorw" ::: "memory");
    stage = 4;
    for (unsigned i = 0; i < 16; i++)
        if (((volatile uint32_t *)TIKU_S31_PSRAM_BASE)[i] != pattern[i]) goto failed;
    if (((uint32_t (*)(void))0x2f8005d4u)() != TIKU_S31_PSRAM_LINE) goto failed;
    capacity = TIKU_S31_PSRAM_BYTES;
    stage = 5;
    return 0;
failed:
    field(SYS + 0x3c, CLOSE | 1, CLOSE);
    for (unsigned page = 0; page < 1024; page++) {
        REG(SYS + 0x380) = page;
        REG(SYS + 0x37c) = 0;
    }
    __asm__ volatile ("csrw 0xbc0,zero\ncsrw 0xbd0,%0\ncsrw 0xbc0,%1"
                      :: "r"(old_addr), "r"(old_cfg) : "memory");
    ((void (*)(unsigned))0x2f8006b0u)(0);
    return -1;
}

uint32_t tiku_s31_psram_size(void) { return capacity; }
unsigned tiku_s31_psram_stage(void) { return stage; }

/** @brief Apply a cache command to an owned span, with a bounded completion wait. */
static int cache_command(void *address, uint32_t length, unsigned command)
{
    uintptr_t start = (uintptr_t)address;
    uint32_t irq;
    int result = 0;
    if (!capacity || tiku_s31_in_trap ||
        !length || (start & 63u) || (length & 63u) ||
        start < TIKU_S31_PSRAM_BASE || start >= TIKU_S31_PSRAM_BASE + capacity ||
        length > TIKU_S31_PSRAM_BASE + capacity - start) return -1;
    __asm__ volatile ("csrrci %0,mstatus,8\nfence iorw,iorw" : "=r"(irq) :: "memory");
    REG(SYNC + 4) = 16;
    REG(SYNC + 8) = (uint32_t)start;
    REG(SYNC + 12) = length;
    /* S31 writeback requires two commands; invalidation requires one. */
    for (unsigned pass = 0; pass < (command == 1 ? 1u : 2u); pass++) {
        REG(SYNC) = command;
        if (wait_bits(SYNC, 16u, 16u)) { capacity = 0; result = -1; break; }
    }
    __asm__ volatile ("fence iorw,iorw" ::: "memory");
    if (irq & 8u) __asm__ volatile ("csrsi mstatus,8" ::: "memory");
    return result;
}

int tiku_s31_psram_sync(void *address, uint32_t length, uint8_t invalidate)
{
    return invalidate > 1 ? -1 : cache_command(address, length, invalidate ? 8u : 4u);
}

int tiku_s31_psram_invalidate(void *address, uint32_t length)
{
    return cache_command(address, length, 1);
}

#ifdef TIKU_S31_PSRAM_TEST
int tiku_s31_psram_read_physical(uint32_t offset, uint32_t data[16])
{
    uint32_t irq;
    int result;
    uintptr_t address = (uintptr_t)data;
    if (!capacity || tiku_s31_in_trap || (offset & 63u) || offset > capacity - 64 ||
        address < 0x2f000000u || address > 0x2f00ffc0u || (address & 3u)) return -1;
    __asm__ volatile ("csrrci %0,mstatus,8\nfence iorw,iorw" : "=r"(irq) :: "memory");
    field(SYS + 0x3c, CLOSE, CLOSE);
    result = wait_bits(SYS, 255u, 0);
    if (!result) result = transfer(0, offset, 18, data, 64, 0);
    field(SYS + 0x3c, CLOSE, 0);
    if (result) capacity = 0;
    if (irq & 8u) __asm__ volatile ("csrsi mstatus,8" ::: "memory");
    return result;
}
#endif
