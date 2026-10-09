/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_mpu_arch.c - C5 software write-window state and locked PMP NULL guard.
 * Durable SRAM remains writable; its window controls commit ordering, not access.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_mpu_hal.h>
#include <hal/tiku_cpu.h>

static uint16_t permissions = 0x0555;

uint16_t tiku_mpu_arch_get_sam(void) { return permissions; }
void tiku_mpu_arch_set_sam(uint16_t value) { permissions = value; }
uint16_t tiku_mpu_arch_get_ctl(void) { return 0; }
void tiku_mpu_arch_disable_irq(void) { tiku_cpu_irq_disable(); }
void tiku_mpu_arch_enable_irq(void) { tiku_cpu_irq_enable(); }
void tiku_mpu_arch_init_segments(void)
{
    uint32_t config;
    __asm__ volatile ("csrr %0, pmpcfg0" : "=r"(config));
    __asm__ volatile ("csrw pmpaddr0, %0" :: "r"(511u) : "memory");
    __asm__ volatile ("csrw pmpcfg0, %0" :: "r"((config & ~255u) | 0x98u) : "memory");
    permissions = 0x0555;
}
void tiku_mpu_arch_set_default_protection(void) { permissions = 0x0555; }
void tiku_mpu_arch_set_seg_perm(uint8_t segment, uint8_t perm)
{
    unsigned shift;
    if (segment > 3) { return; }
    shift = segment * 4u;
    permissions = (uint16_t)((permissions & ~(7u << shift)) | ((perm & 7u) << shift));
}
uint16_t tiku_mpu_arch_unlock_nvm(void)
{
    uint16_t saved = permissions;
    permissions |= 0x0222;
    return saved;
}
void tiku_mpu_arch_lock_nvm(uint16_t saved) { permissions = saved; }
uint16_t tiku_mpu_arch_get_violation_flags(void) { return 0; }
void tiku_mpu_arch_clear_violation_flags(void) { }
void tiku_mpu_arch_enable_violation_nmi(void) { }
