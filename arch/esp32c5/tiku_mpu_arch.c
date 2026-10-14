/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_mpu_arch.c - C5 durable-SRAM write window and locked PMP NULL guard.
 * PMA entries 10 and 11 cover the durable span read-only outside a window;
 * an unbracketed store there faults.  SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_mpu_hal.h>
#include <hal/tiku_cpu.h>

/* The durable span the linker gives .persistent and .uninit. */
extern char __uninit_start[];
extern char __uninit_end[];

/* PMA configuration (riscv/csr.h at the ESP-IDF pin): TOR range, enabled,
 * read, write.  Entries 10 and 11 sit below the ROM's entry 14, which maps
 * all of HP SRAM read/write/execute, so they take priority over it. */
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
    __asm__ volatile("csrw 0xBCB, %0" ::"r"(cfg) : "memory");
}

void tiku_c5_durable_guard_off(void)
{
    __asm__ volatile("csrw 0xBCB, zero\n\tcsrw 0xBCA, zero" ::: "memory");
}

uint16_t tiku_mpu_arch_get_sam(void)
{
    return permissions;
}

/**
 * @brief Set the segment mask and the durable span's write permission with
 *        it; the control shadow records the MSP430 password write, then
 *        password and enable, which the portable tests read back.
 */
void tiku_mpu_arch_set_sam(uint16_t value)
{
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

/**
 * @brief Lock the NULL guard and arm the durable span read-only.
 *
 * PMP entry 0, locked with no permissions, faults any access below 4 KB;
 * the lock holds until reset.  PMA entry 10 holds the span's start for
 * entry 11's top-of-range.  Called once the working copy has been restored.
 */
void tiku_mpu_arch_init_segments(void)
{
    uint32_t config;

    __asm__ volatile("csrr %0, pmpcfg0" : "=r"(config));
    __asm__ volatile("csrw pmpaddr0, %0" ::"r"(511u) : "memory");
    __asm__ volatile("csrw pmpcfg0, %0" ::"r"((config & ~255u) | 0x98u)
                     : "memory");
    __asm__ volatile("csrw 0xBCA, zero" ::: "memory");
    __asm__ volatile("csrw 0xBDA, %0" ::"r"((uint32_t)__uninit_start >> 2));
    __asm__ volatile("csrw 0xBDB, %0" ::"r"((uint32_t)__uninit_end >> 2));
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
}
void tiku_mpu_arch_set_default_protection(void)
{
    tiku_mpu_arch_set_sam(TIKU_MPU_DEFAULT_SAM);
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
/** @brief A violation is a store access fault, recorded by the trap handler
 *         (`diag fault`); no flags latch.  @return 0 */
uint16_t tiku_mpu_arch_get_violation_flags(void)
{
    return 0;
}
void tiku_mpu_arch_clear_violation_flags(void)
{
}
void tiku_mpu_arch_enable_violation_nmi(void)
{
}
