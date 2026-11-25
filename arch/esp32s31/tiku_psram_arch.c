/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.c - Korvo octal PSRAM clocks, mapping and cache ownership.
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

#ifndef TIKU_S31_PSRAM_MHZ
#define TIKU_S31_PSRAM_MHZ 20
#endif
#if TIKU_S31_PSRAM_MHZ != 20 && TIKU_S31_PSRAM_MHZ != 40 && TIKU_S31_PSRAM_MHZ != 80 && \
    TIKU_S31_PSRAM_MHZ != 200
#error Unsupported S31 PSRAM clock
#endif

/* The device's latencies and the controller's matching dummy bits (ESP-IDF
 * esp_psram_impl_ap_oct.c): MR0 holds fixed latency (bit 5) and the read
 * latency code (bits 4:2), MR4 the write latency code (bits 7:5).  200 MHz
 * clocks the controller from the MSPI PLL at 400 MHz; the others from the
 * 320 MHz CPLL. */
#if TIKU_S31_PSRAM_MHZ == 200
#define MR0_VALUE 0x30u         /* read latency code 4 */
#define MR4_VALUE 0x20u         /* write latency code 1 */
#define RD_DUMMY 26u            /* 2 x (14 - 1) */
#define RD_REG_DUMMY 12u        /* 2 x (7 - 1) */
#define WR_DUMMY 12u            /* 2 x (7 - 1) */
#define CORE_SOURCE 1u          /* the MSPI PLL, undivided: 400 MHz */
#define CORE_DIVIDER 1u
#else
#define MR0_VALUE 0x28u         /* read latency code 2 */
#define MR4_VALUE 0x40u         /* write latency code 2 */
#define RD_DUMMY 18u            /* 2 x (10 - 1) */
#define RD_REG_DUMMY 8u         /* 2 x (5 - 1) */
#define WR_DUMMY 8u             /* 2 x (5 - 1) */
#define CORE_SOURCE 2u          /* the CPLL, 320 MHz */
#define CORE_DIVIDER (160u / TIKU_S31_PSRAM_MHZ)
#endif

#if TIKU_S31_PSRAM_MHZ == 200
/* The analog I2C master, the MSPI PLL's block on it, its divider and its
 * calibration bits (HP_SYS_CLKRST_ANA_PLL_CTRL0: end 8, stop 9). */
#define ANALOG 0x2010f800u
#define ANALOG_BUSY (1u << 25)
#define MPLL_BLOCK 0x0bu
#define MPLL_SELECT (1u << 12)
#define MSPI_DIV 0x20701054u    /* reference divider 2:0, feedback 7:3 */
#define PLL_CAL 0x20587174u

/** @brief Set bits @p msb..@p lsb of MSPI PLL analog register @p reg to
 *  @p value: read the byte over the analog I2C master, then write it. */
static int mpll_write(unsigned reg, unsigned msb, unsigned lsb, unsigned value)
{
    unsigned sel = (REG(ANALOG + 0x20u) & (MPLL_SELECT << 4)) ? 0u : 1u;
    uint32_t ctrl = ANALOG + 4u * sel, mask = ((1u << (msb - lsb + 1u)) - 1u) << lsb;
    REG(ANALOG + 0x1cu) = ~(MPLL_SELECT << 2) & 0xffffffu;
    if (wait_bits(ctrl, ANALOG_BUSY, 0)) return -1;
    REG(ctrl) = MPLL_BLOCK | (reg << 8);
    if (wait_bits(ctrl, ANALOG_BUSY, 0)) return -1;
    uint32_t data = ((REG(ctrl) >> 16) & 0xffu & ~mask) | ((value << lsb) & mask);
    REG(ctrl) = MPLL_BLOCK | (reg << 8) | (1u << 24) | (data << 16);
    return wait_bits(ctrl, ANALOG_BUSY, 0);
}

/** @brief Power the MSPI PLL and lock it at 400 MHz, the 40 MHz crystal
 *  times 20 over 2, as ESP-IDF's clk_ll_mpll_set_config() does. */
static int mspi_pll(void)
{
    REG(0x207040f4u) |= (1u << 22) | (1u << 26) | (1u << 30);
    REG(0x20589000u) |= 1u << 31;
    REG(0x2070401cu) |= (1u << 26) | (1u << 30);
    if (mpll_write(1, 1, 0, 3) || mpll_write(1, 2, 2, 1)) return -1;
    field(MSPI_DIV, 31u << 3, 9u << 3);
    if (mpll_write(3, 5, 4, 3) || mpll_write(1, 2, 2, 0)) return -1;
    REG(PLL_CAL) &= ~(1u << 9);
    field(MSPI_DIV, (31u << 3) | 7u, (19u << 3) | 1u);
    int result = wait_bits(PLL_CAL, 1u << 8, 1u << 8);
    REG(PLL_CAL) |= 1u << 9;
    return result;
}
#endif

/** @brief Configure the mapped clock and retain 20 MHz PIO commands. */
static int memory_clock(void)
{
#if TIKU_S31_PSRAM_MHZ != 20
    uint32_t modem = REG(0x205871e0u);
    field(0x205871e0u, 0x3fu, 0x25u);
    uint32_t analog = REG(0x2010f018u);
    REG(0x2010f018u) |= 4u;
    REG(0x207040f4u) |= (1u << 23) | (1u << 27) | (1u << 19);
    REG(0x20589000u) |= 1u << 29;
    REG(0x2070401cu) |= (1u << 23) | (1u << 27);
    field(0x2070104cu, 0xfffu, 0x81u);
    REG(0x20587174u) &= ~8u;
    int result = wait_bits(0x20587174u, 4u, 4u);
    tiku_clock_arch_delay(10);
    REG(0x20587174u) |= 8u;
#if TIKU_S31_PSRAM_MHZ == 200
    if (!result) result = mspi_pll();
#endif
    REG(0x2010f018u) = analog;
    REG(0x205871e0u) = modem;
    if (result) return -1;
    /* The core clock is twice the bus clock; PIO commands stay at 20 MHz. */
    unsigned pio_div = TIKU_S31_PSRAM_MHZ / 10;
    REG(PIO + 0x14) = ((pio_div - 1) << 16) |
                     ((pio_div / 2 - 1) << 8) | (pio_div - 1);
    field(CLOCK, (3u << 5) | (255u << 9), (CORE_SOURCE << 5) | ((CORE_DIVIDER - 1u) << 9));
#endif
    return 0;
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
    value = MR0_VALUE;
    if (transfer(0xc0c0, 0, 0, &value, 2, 1)) return -1;
    value = MR4_VALUE;
    if (transfer(0xc0c0, 4, 0, &value, 2, 1)) return -1;
    value = 0x0b;
    if (transfer(0xc0c0, 8, 0, &value, 2, 1)) return -1;
    if (transfer(0x4040, 0, RD_REG_DUMMY, &mr01, 2, 0) ||
        transfer(0x4040, 2, RD_REG_DUMMY, &mr23, 2, 0) ||
        transfer(0x4040, 4, RD_REG_DUMMY, &mr48, 2, 0) ||
        transfer(0x4040, 8, RD_REG_DUMMY, &mr8, 2, 0)) return -1;
    return ((mr01 & 0xffu) == MR0_VALUE && ((mr01 >> 8) & 31u) == 13u &&
            (mr23 & 0xe7u) == 0xc5u && (mr48 & 0xffu) == MR4_VALUE &&
            (mr8 & 0xffu) == 0x0bu) ? 0 : -1;
}

/** @brief Configure mapped octal transactions and the 256 physical 64-KiB pages. */
static int mapping(void)
{
    REG(SYS + 0x48) = 0xf0000000u;
    REG(SYS + 0x4c) = 0xf0008080u;
    field(SYS + 0x40, 0x0fffffffu,
          1u | 0x38u | ((RD_DUMMY - 1u) << 6) | (31u << 14) | (3u << 20) |
          ((WR_DUMMY - 1u) << 22));
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

#if TIKU_S31_PSRAM_MHZ == 200
/* ESP-IDF's tuning pattern (mspi_timing_by_dqs.c), 128 bytes. */
static const uint32_t tuning_data[32] = {
    0x7f786655, 0xa5ff005a, 0x3f3c33aa, 0xa5ff5a00, 0x1f1e9955, 0xa5005aff, 0x0f0fccaa,
    0xa55a00ff, 0x07876655, 0xffa55a00, 0x03c333aa, 0xff00a55a, 0x01e19955, 0xff005aa5,
    0x00f0ccaa, 0xff5a00a5, 0x80786655, 0x00a5ff5a, 0xc03c33aa, 0x00a55aff, 0xe01e9355,
    0x00ff5aa5, 0xf00fccaa, 0x005affa5, 0xf8876655, 0x5aa5ff00, 0xfcc333aa, 0x5affa500,
    0xfee19955, 0x5a00a5ff, 0x11f0ccaa, 0x5a00ffa5};
/* The tuning chosen at init: DQS phase in bits 17:16, the passing window's
 * first delay setting in 15:8 and its length in 7:0. */
static uint32_t tuned;

/** @brief Set the data, clock and select pads' delay lines to @p data and
 *  the DQS pad's two to @p dqs. */
static void delay_lines(unsigned data, unsigned dqs)
{
    for (unsigned offset = 0x1c; offset <= 0x44; offset += 4)
        if (offset != 0x3c) field(PADS + offset, 0xf0u, data << 4);
    field(PADS + 0x3c, (15u << 7) | (15u << 18), (dqs << 7) | (dqs << 18));
}

/** @brief Whether the tuning pattern written at physical 0x80 reads back
 *  whole eight times in a row. */
static int round_trip(void)
{
    uint32_t out[16], in[16];
    for (unsigned t = 0; t < 8u; t++)
        for (unsigned half = 0; half < 2u; half++) {
            for (unsigned i = 0; i < 16u; i++) out[i] = tuning_data[half * 16u + i] ^ (t * 0x01010101u);
            if (transfer(0x8080, 0x80u + 64u * half, WR_DUMMY, out, 64, 1) ||
                transfer(0, 0x80u + 64u * half, RD_DUMMY, in, 64, 0)) return 0;
            for (unsigned i = 0; i < 16u; i++) if (in[i] != out[i]) return 0;
        }
    return 1;
}

/** @brief Delay setting @p i of 31: the DQS delay from 15 down to 0 with the
 *  data at 0, then the data delay from 1 up to 15 with the DQS at 0. */
static void delay_setting(unsigned i)
{
    delay_lines(i < 16u ? 0u : i - 15u, i < 16u ? 15u - i : 0u);
}

/** @brief Tune reads at 200 MHz as ESP-IDF's DQS scheme does: the first DQS
 *  phase of the longest passing run of the four, then the middle of the
 *  longest passing run of the 31 delay settings, each tried with PIO
 *  transfers at the bus rate.  PIO returns to 20 MHz.  @return 0, or -1
 *  when no phase or setting passes. */
static int tune(void)
{
    unsigned best = 0, run = 0, start = 0, length = 0, phase = 0;
    REG(PIO + 0x14) = 0x10001u;
    delay_lines(0, 0);
    for (unsigned p = 0; p < 4u; p++) {
        field(PADS + 0x3c, 3u << 1, p << 1);
        run = round_trip() ? run + 1u : 0u;
        if (run > best) { best = run; phase = p + 1u - run; }
    }
    if (best) {
        field(PADS + 0x3c, 3u << 1, phase << 1);
        run = 0;
        for (unsigned i = 0; i < 31u; i++) {
            delay_setting(i);
            run = round_trip() ? run + 1u : 0u;
            if (run > length) { length = run; start = i + 1u - run; }
        }
    }
    if (length) delay_setting(start + length / 2u);
    REG(PIO + 0x14) = (19u << 16) | (9u << 8) | 19u;
    tuned = phase << 16 | start << 8 | length;
    return length ? 0 : -1;
}

uint32_t tiku_s31_psram_tuning(void) { return tuned; }
#else
uint32_t tiku_s31_psram_tuning(void) { return 0; }
#endif

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
    if (transfer(0x8080, 0, WR_DUMMY, pattern, 64, 1) ||
        transfer(0, 0, RD_DUMMY, received, 64, 0)) return -1;
    for (unsigned i = 0; i < 16; i++) if (pattern[i] != received[i]) return -1;
    stage = 3;
    if (memory_clock()) return -1;
#if TIKU_S31_PSRAM_MHZ == 200
    if (tune()) return -1;
#endif
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
    if (!result) result = transfer(0, offset, RD_DUMMY, data, 64, 0);
    field(SYS + 0x3c, CLOSE, 0);
    if (result) capacity = 0;
    if (irq & 8u) __asm__ volatile ("csrsi mstatus,8" ::: "memory");
    return result;
}
#endif
