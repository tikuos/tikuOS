/* TikuOS -- portable next-boot clock preference.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
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
#endif
#if defined(PLATFORM_RA8P1)
extern uint32_t tiku_mem_arch_nvm_program_count(void);
#endif

#if !defined(PLATFORM_NORDIC)
/* A complementary word rejects torn multiword saves even on FRAM. The cell
 * gate additionally protects persist's commit protocol. */
static TIKU_DURABLE struct {
    uint32_t hz;
    uint32_t inverse;
} saved_clock;
TIKU_PERSIST_CELL(saved_clock_cell, saved_clock, 0x434C4B32UL, NULL, 0);
static unsigned long boot_default;
static int ready, restored;

/** @brief Verify the durable image, not merely its SRAM working copy. */
static int committed(void)
{
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
    defined(PLATFORM_STM32N6)
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
    return 1; /* Direct NVM: RA8P1 additionally checks its flush counter. */
#endif
}

/** @brief Accept only discrete choices advertised by this platform. */
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
    uint32_t previous[2];
#if defined(PLATFORM_RA8P1)
    uint32_t programs = tiku_mem_arch_nvm_program_count();
#endif
    if (!ready || !supported(hz)) return -1;
    value[0] = (uint32_t)hz;
    value[1] = ~value[0];
    memcpy(previous, &saved_clock, sizeof previous);
    tiku_persist_cell_commit(&saved_clock_cell, value, sizeof value);
    if (!committed()
#if defined(PLATFORM_RA8P1)
        || programs == tiku_mem_arch_nvm_program_count()
#endif
        ) {
        /* Do not leave a failed request queued in SRAM for a later,
         * unrelated persistence flush to silently commit. */
        tiku_persist_cell_commit(&saved_clock_cell, previous, sizeof previous);
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
    boot_default = tiku_cpu_mclk_hz();
    restored = 1;
#if defined(PLATFORM_RP2350) || defined(PLATFORM_AMBIQ) || \
    defined(PLATFORM_STM32N6)
    /* A bad mirror may leave old NOLOAD SRAM intact after a warm reset.
     * Never mistake that working copy for a successfully restored setting. */
    restored = tiku_mem_arch_nvm_restore_status() == TIKU_NVM_RESTORE_V2_OK &&
               committed();
#endif
    ready = 1;
    target = tiku_cpu_settings_target();
    if (target != boot_default && supported(target))
        tiku_cpu_freq_boot_set(target);
}
#endif
