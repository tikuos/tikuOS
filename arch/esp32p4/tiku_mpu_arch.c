/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - P4 durable-span write window and locked PMP NULL guard.
 *
 * PMA entries 0 and 1 hold the durable span read-only outside a window; a
 * store there is a store access fault, which the trap entry latches in
 * tiku_p4_mpu_latch and skips.  PMP entry 0, locked, faults the first 4 KB.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_mpu_hal.h>
#include <hal/tiku_cpu.h>

/* The durable span the linker gives .persistent. */
extern char __uninit_start[];
extern char __uninit_end[];

/* PMA configuration (PMA 15, which the ROM sets over 0x40000000-0x50000000
 * read/write/execute, shows the layout): top-of-range, enabled, read,
 * write.  Entries 0 and 1 sit below it, so they take priority. */
#define PMA_TOR 0x40000000u
#define PMA_EN  (1u << 0)
#define PMA_W   (1u << 3)
#define PMA_R   (1u << 4)
/* The software segment mask's write bits (MSP430 SAM layout). */
#define SAM_WRITE_BITS 0x0222u

static uint16_t permissions = TIKU_MPU_DEFAULT_SAM;
static uint16_t control;

/** @brief Make the durable span writable exactly while the mask allows
 *         writes; the clobber keeps stores on their side of the change. */
static void apply(void)
{
    uint32_t cfg = PMA_TOR | PMA_EN | PMA_R;

    if (permissions & SAM_WRITE_BITS) {
        cfg |= PMA_W;
    }
    __asm__ volatile("csrw 0xBC1, %0" ::"r"(cfg) : "memory");
}

uint16_t tiku_mpu_arch_get_sam(void)
{
    return permissions;
}

void tiku_mpu_arch_set_sam(uint16_t value)
{
    /* control repeats MSP430's MPUCTL0 writes, which the portable tests read
     * back: the password, then the password and the enable bit. */
    control = 0xA500u;
    permissions = value;
    apply();
    control = 0xA500u | 0x0001u;
}

uint16_t tiku_mpu_arch_get_ctl(void)
{
    return control;
}

void tiku_mpu_arch_disable_irq(void)
{
    tiku_cpu_irq_disable();
}

void tiku_mpu_arch_enable_irq(void)
{
    tiku_cpu_irq_enable();
}

void tiku_mpu_arch_init_segments(void)
{
    uint32_t config;

    /* PMP entry 0, locked with no permissions, faults any access below 4 KB;
     * the lock holds until reset. */
    __asm__ volatile("csrr %0, pmpcfg0" : "=r"(config));
    __asm__ volatile("csrw pmpaddr0, %0" ::"r"(511u) : "memory");
    __asm__ volatile("csrw pmpcfg0, %0" ::"r"((config & ~255u) | 0x98u)
                     : "memory");
    /* PMA entry 0 holds the span's start for entry 1's top-of-range; the
     * default mask then arms the span read-only.  tiku_mem_arch_init()
     * restored the span from its mirror before this. */
    tiku_p4_mpu_latch[TIKU_P4_MPU_LATCH_START] = (uint32_t)__uninit_start;
    tiku_p4_mpu_latch[TIKU_P4_MPU_LATCH_END] = (uint32_t)__uninit_end;
    __asm__ volatile("csrw 0xBC1, zero\n\tcsrw 0xBC0, zero" ::: "memory");
    __asm__ volatile("csrw 0xBD0, %0" ::"r"((uint32_t)__uninit_start >> 2));
    __asm__ volatile("csrw 0xBD1, %0" ::"r"((uint32_t)__uninit_end >> 2));
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

void tiku_mpu_arch_set_default_protection(void)
{
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}

void tiku_mpu_arch_module_window_exec(int enable)
{
    (void)enable;
}

void tiku_mpu_arch_set_seg_perm(uint8_t segment, uint8_t perm)
{
    unsigned shift;

    if (segment > 3) {
        return;
    }
    shift = segment * 4u;
    tiku_mpu_arch_set_sam((uint16_t)((permissions & ~(7u << shift)) |
                                     ((perm & 7u) << shift)));
}

uint16_t tiku_mpu_arch_unlock_nvm(void)
{
    uint16_t saved = permissions;

    tiku_mpu_arch_set_sam((uint16_t)(saved | SAM_WRITE_BITS));
    return saved;
}

void tiku_mpu_arch_lock_nvm(uint16_t saved)
{
    tiku_mpu_arch_set_sam(saved);
}

uint8_t tiku_mpu_arch_nvm_region_ro(void)
{
    uint32_t cfg;

    __asm__ volatile("csrr %0, 0xBC1" : "=r"(cfg));
    return (cfg & (PMA_EN | PMA_W)) == PMA_EN;
}

uint16_t tiku_mpu_arch_get_violation_flags(void)
{
    /* Bit 0: a store into the durable span outside a window, latched and
     * skipped by the trap entry. */
    return (uint16_t)tiku_p4_mpu_latch[TIKU_P4_MPU_LATCH_FLAGS];
}

void tiku_mpu_arch_clear_violation_flags(void)
{
    tiku_p4_mpu_latch[TIKU_P4_MPU_LATCH_FLAGS] = 0u;
}

void tiku_mpu_arch_enable_violation_nmi(void)
{
    /* The latch is always on. */
}

uint32_t tiku_mpu_arch_violation_count(void)
{
    return tiku_p4_mpu_latch[TIKU_P4_MPU_LATCH_COUNT];
}

uint32_t tiku_mpu_arch_last_fault_addr(void)
{
    return tiku_p4_mpu_latch[TIKU_P4_MPU_LATCH_LAST];
}
