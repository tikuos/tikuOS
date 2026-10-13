/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_mpu_arch.c - RAM profile without hardware memory protection.
 * Permission requests do not change access; control and SAM report zero.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_mpu_hal.h>
#include <hal/tiku_cpu.h>

uint16_t tiku_mpu_arch_get_sam(void) { return 0; }
void tiku_mpu_arch_set_sam(uint16_t sam) { (void)sam; }
uint16_t tiku_mpu_arch_get_ctl(void) { return 0; }
void tiku_mpu_arch_disable_irq(void) { tiku_cpu_irq_disable(); }
void tiku_mpu_arch_enable_irq(void) { tiku_cpu_irq_enable(); }
void tiku_mpu_arch_init_segments(void) { }
void tiku_mpu_arch_set_default_protection(void) { }
void tiku_mpu_arch_module_window_exec(int enable) { (void)enable; }
void tiku_mpu_arch_set_seg_perm(uint8_t seg, uint8_t perm)
{
    (void)seg; (void)perm;
}
uint16_t tiku_mpu_arch_unlock_nvm(void) { return 0; }
void tiku_mpu_arch_lock_nvm(uint16_t saved_state) { (void)saved_state; }
uint16_t tiku_mpu_arch_get_violation_flags(void) { return 0; }
void tiku_mpu_arch_clear_violation_flags(void) { }
void tiku_mpu_arch_enable_violation_nmi(void) { }
