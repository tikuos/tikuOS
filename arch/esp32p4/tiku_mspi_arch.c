/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_mspi_arch.c - SRAM-only flash transactions preserving PSRAM cache data.
 * Cache sequence: ESP-IDF 4d59230ddff16327812782151ef0afef202dc6d7.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_mspi_arch.h"
#include "tiku_flash_arch.h"
#include "tiku_cache_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32p4_regs.h"

#define MSPI_AXI 0x5008c03cu
#define MSPI_CMD 0x5008c000u
#define MSPI_CLOSE (1u << 31)
#define MSPI_SYNC_CTRL 0x3ff10098u
#define MSPI_SYNC_MAP 0x3ff1009cu
#define MSPI_SYNC_ADDR 0x3ff100a0u
#define MSPI_SYNC_SIZE 0x3ff100a4u
#define MSPI_PREDICT_MASK 0x1030u
#ifndef TIKU_P4_MSPI_PREDICT_OFF
/** @brief Save and disable the CPU0 return stack and branch prediction bits. */
static uint32_t predict_off(void)
{
    uint32_t old, mask = MSPI_PREDICT_MASK;
    __asm__ volatile("csrrc %0, 0x7c1, %1" : "=r"(old) : "r"(mask) : "memory");
    return old & mask;
}
/** @brief Restore just the saved prediction bits. */
static void predict_restore(uint32_t state)
{
    __asm__ volatile("csrs 0x7c1, %0" :: "r"(state) : "memory");
}
#define TIKU_P4_MSPI_PREDICT_OFF() predict_off()
#define TIKU_P4_MSPI_PREDICT_RESTORE(s) predict_restore(s)
#define TIKU_P4_MSPI_SUSPEND() ((uint32_t (*)(void))0x4fc00508u)()
#define TIKU_P4_MSPI_RESUME(s) ((void (*)(uint32_t))0x4fc0050cu)(s)
#define TIKU_P4_MSPI_FENCE() __asm__ volatile("fence iorw, iorw\nfence.i" ::: "memory")
#define TIKU_P4_MSPI_HALT() __builtin_trap()
#endif

static uint32_t saved_irq, saved_predict, saved_autoload, saved_axi;
static uint32_t changed_offset, changed_length;
static uint8_t active;

/** @brief Trap in internal SRAM without restoring unsafe external access. */
static void unsafe(void) __attribute__((noreturn));
static void unsafe(void)
{
    TIKU_P4_MSPI_HALT();
    for (;;) {}
}

/** @brief Run a bounded cache synchronization command on an aligned range. */
static void sync_range(uint32_t map, uint32_t address, uint32_t size,
                       uint32_t operation)
{
    unsigned tries;
    TIKU_P4_REG_WRITE(MSPI_SYNC_MAP, map);
    TIKU_P4_REG_WRITE(MSPI_SYNC_ADDR, address);
    TIKU_P4_REG_WRITE(MSPI_SYNC_SIZE, size);
    TIKU_P4_REG_WRITE(MSPI_SYNC_CTRL, operation);
    for (tries = 0; tries < 1000000u; tries++)
        if (TIKU_P4_REG_READ(MSPI_SYNC_CTRL) & (1u << 4)) return;
    unsafe();
}

int tiku_p4_mspi_flash_begin(uint32_t offset, uint32_t length)
{
    uint32_t state;
    unsigned tries;
    if (!length || offset >= TIKU_P4_FLASH_SIZE ||
        length > TIKU_P4_FLASH_SIZE - offset || tiku_p4_in_isr()) return -1;
    state = TIKU_P4_IRQ_SAVE();
    if (active || !tiku_p4_cache_layout_ok()) {
        TIKU_P4_IRQ_RESTORE(state);
        return -1;
    }
    active = 1;
    saved_irq = state;
    saved_predict = TIKU_P4_MSPI_PREDICT_OFF();
    TIKU_P4_MSPI_FENCE();
    changed_offset = offset & ~63u;
    changed_length = ((offset + length + 63u) & ~63u) - changed_offset;
    /* Drain L1 dirty lines before L2 suspension; ECO2 requires two writebacks. */
    sync_range(0x10u, 0, 0, 4);
    sync_range(0x10u, 0, 0, 4);
    saved_autoload = TIKU_P4_MSPI_SUSPEND();
    saved_axi = TIKU_P4_REG_READ(MSPI_AXI);
    TIKU_P4_REG_WRITE(MSPI_AXI, saved_axi | MSPI_CLOSE);
    if (!(TIKU_P4_REG_READ(MSPI_AXI) & MSPI_CLOSE)) unsafe();
    for (tries = 0; tries < 65536u; tries++)
        if (!(TIKU_P4_REG_READ(MSPI_CMD) & 255u)) return 0;
    unsafe();
}

void tiku_p4_mspi_flash_end(int readable)
{
    if (!active || !readable) unsafe();
    sync_range(0x31u, 0x40000000u + changed_offset, changed_length, 1);
    TIKU_P4_REG_WRITE(MSPI_AXI, saved_axi);
    TIKU_P4_MSPI_RESUME(saved_autoload);
    TIKU_P4_MSPI_FENCE();
    TIKU_P4_MSPI_PREDICT_RESTORE(saved_predict);
    active = 0;
    TIKU_P4_IRQ_RESTORE(saved_irq);
}
