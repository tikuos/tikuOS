/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_settings.c - portable next-boot clock preference.
 *
 * The chosen rate sits in a persist cell beside its complement; on a port
 * that mirrors durable memory, a save is checked against the durable image.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <string.h>
#include <hal/tiku_cpu.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/memory/tiku_nvm_mirror.h>
#include "tiku_cpu_settings.h"

#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ)
extern tiku_nvm_restore_t tiku_mem_arch_nvm_restore_status(void);
#endif

#if defined(PLATFORM_STM32N6)
#include <arch/stm32n6/tiku_xspi_arch.h>
#elif defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_flash_arch.h>
#elif defined(PLATFORM_ESP32C5)
#include <arch/esp32c5/tiku_flash_arch.h>
#endif
#if defined(PLATFORM_RA8P1)
extern uint32_t tiku_mem_arch_nvm_program_count(void);
#endif

#if !defined(PLATFORM_NORDIC)
/* A save commits hz and its complement through the cell, which stores the
 * gate last.  tiku_cpu_settings_target() takes the value only with a valid
 * gate and a matching complement, so a save torn between the words fails. */
static TIKU_DURABLE struct {
    uint32_t hz;
    uint32_t inverse;
} saved_clock;
TIKU_PERSIST_CELL(saved_clock_cell, saved_clock, 0x434C4B32UL, NULL, 0);
static unsigned long boot_default;
static int ready, restored;

/**
 * @brief Return 1 when the durable image holds saved_clock and its gate as
 *        SRAM does; ports that write NVM in place return 1.
 */
static int committed(void)
{
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
    defined(PLATFORM_STM32N6) || defined(PLATFORM_ESP32C61) || \
    defined(PLATFORM_ESP32C5)
    extern unsigned char __uninit_start;
    const uint32_t *header;
    const unsigned char *image;
    size_t capacity, len;
    size_t offset = (uintptr_t)&saved_clock - (uintptr_t)&__uninit_start;
    size_t gate = (uintptr_t)&saved_clock_cell_gate -
                  (uintptr_t)&__uninit_start;
#if defined(PLATFORM_RP2350)
    extern uint32_t __tiku_nvm_flash_start[], __tiku_nvm_flash_size;
    header = __tiku_nvm_flash_start;
    capacity = (uintptr_t)&__tiku_nvm_flash_size;
#elif defined(PLATFORM_AMBIQ)
    extern uint32_t __tiku_nvm_mram_start[], __tiku_nvm_mram_size;
    header = __tiku_nvm_mram_start;
    capacity = (uintptr_t)&__tiku_nvm_mram_size;
#elif defined(PLATFORM_ESP32C61) || defined(PLATFORM_ESP32C5)
    /* Two slots take turns; the image is the newer valid one. */
    image = tiku_mem_arch_durable(&len);
    if (image == NULL) return 0;
    header = (const uint32_t *)(const void *)
             (image - TIKU_NVM_MIRROR_HDR_BYTES);
    capacity = TIKU_FLASH_MIRROR_BYTES;
#else
    if (tiku_xspi_mmap_enable() != TIKU_XSPI_OK) return 0;
    header = (const uint32_t *)(TIKU_XSPI_MMAP_BASE + TIKU_XSPI_MIRROR_ADDR);
    capacity = TIKU_XSPI_MIRROR_BYTES;
#endif
    len = header[TIKU_NVM_MIRROR_W_LEN];
    image = (const unsigned char *)header + TIKU_NVM_MIRROR_HDR_BYTES;
    return header[TIKU_NVM_MIRROR_W_MAGIC] == TIKU_NVM_MIRROR_MAGIC_V2 &&
        capacity >= TIKU_NVM_MIRROR_HDR_BYTES &&
        len <= capacity - TIKU_NVM_MIRROR_HDR_BYTES &&
        offset <= len && sizeof saved_clock <= len - offset &&
        gate <= len && sizeof saved_clock_cell_gate <= len - gate &&
        tiku_nvm_crc32(image, len) == header[TIKU_NVM_MIRROR_W_CRC] &&
        memcmp(image + offset, &saved_clock, sizeof saved_clock) == 0 &&
        memcmp(image + gate, &saved_clock_cell_gate,
               sizeof saved_clock_cell_gate) == 0;
#else
    return 1; /* NVM written in place: the cell commit's status is the check. */
#endif
}

/**
 * @brief Return 1 when @p hz is a rate tiku_cpu_freq_available() lists and
 *        the port lists at least two.
 */
static int supported(unsigned long hz)
{
    unsigned int i;
    if (!hz || !tiku_cpu_freq_available(1)) return 0;
    for (i = 0; i < 32; i++) {
        unsigned long candidate = tiku_cpu_freq_available(i);
        if (!candidate) break;
        if (hz == candidate) return 1;
    }
    return 0;
}

/** @brief The advertised rate within 1 % of @p hz, or @p hz if none is. */
static unsigned long as_choice(unsigned long hz)
{
    unsigned int i;
    for (i = 0; i < 32; i++) {
        unsigned long candidate = tiku_cpu_freq_available(i);
        if (!candidate) break;
        if ((hz > candidate ? hz - candidate : candidate - hz) <=
            candidate / 100UL) return candidate;
    }
    return hz;
}

unsigned long tiku_cpu_settings_target(void)
{
    if (ready && restored && tiku_persist_cell_valid(&saved_clock_cell) &&
        saved_clock.inverse == ~saved_clock.hz && supported(saved_clock.hz))
        return saved_clock.hz;
    return boot_default ? boot_default : tiku_cpu_mclk_hz();
}

int tiku_cpu_settings_save(unsigned long hz)
{
    uint32_t value[2];
    if (!ready || !supported(hz)) return -1;
    value[0] = (uint32_t)hz;
    value[1] = ~value[0];
    if (tiku_persist_cell_commit_status(&saved_clock_cell, value, sizeof value)
            != TIKU_MEM_OK || !committed()) {
        /* Whether the value reached durable memory is unknown, and nothing
         * is rewritten: target() reports the boot default until a save
         * succeeds. */
        restored = 0;
        return -1;
    }
    restored = 1;
    return tiku_persist_cell_valid(&saved_clock_cell) &&
           saved_clock.hz == value[0] && saved_clock.inverse == value[1]
               ? 0 : -1;
}

void tiku_cpu_settings_boot(void)
{
    unsigned long target;
    if (ready) return;
    /* A port that measures its clock reads slightly off an advertised
     * choice; the default is the choice within 1 % of the reading. */
    boot_default = as_choice(tiku_cpu_mclk_hz());
    restored = 1;
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
    defined(PLATFORM_STM32N6) || defined(PLATFORM_ESP32C61) || \
    defined(PLATFORM_ESP32C5)
    /* A bad mirror may leave old NOLOAD SRAM intact after a warm reset, so
     * the setting counts as restored only when the mirror restore succeeded
     * and the durable image matches the working copy. */
    restored = tiku_mem_arch_nvm_restore_status() == TIKU_NVM_RESTORE_V2_OK &&
               committed();
#endif
    ready = 1;
    target = tiku_cpu_settings_target();
    if (target != boot_default && supported(target))
        tiku_cpu_freq_boot_set(target);
}
#endif
