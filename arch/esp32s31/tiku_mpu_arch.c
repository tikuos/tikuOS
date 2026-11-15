/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mpu_arch.c - S31 durable-SRAM write window in PMA entries 1 and 2.
 *
 * PMA entries 1 and 2 cover the durable span read-only outside a window;
 * an unbracketed store there faults.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_mpu_hal.h>
#include <hal/tiku_cpu.h>

/* The durable span the linker gives .persistent and .uninit. */
extern char __uninit_start[];
extern char __uninit_end[];

/* PMA configuration (riscv/csr.h at the ESP-IDF pin): TOR range, enabled,
 * read, write.  Entry 0 maps PSRAM; entries 1 and 2 bound durable SRAM. */
#define PMA_TOR 0x40000000u
#define PMA_EN  (1u << 0)
#define PMA_W   (1u << 3)
#define PMA_R   (1u << 4)
#define PMA_NONCACHEABLE (1u << 27)

/* The software segment mask's write bits (MSP430 SAM layout). */
#define SAM_WRITE_BITS 0x0222u

/* The PMA and PMP CSR accesses; the PMA address writes take no memory
 * clobber.  A host model defines these over its own words. */
#ifndef TIKU_S31_MPU_CSR_READ
#define TIKU_S31_MPU_CSR_READ(csr, out) \
    __asm__ volatile("csrr %0, " #csr : "=r"(out))
#define TIKU_S31_MPU_CSR_WRITE(csr, value) \
    __asm__ volatile("csrw " #csr ", %0" ::"r"(value) : "memory")
#define TIKU_S31_MPU_CSR_WRITE_ADDR(csr, value) \
    __asm__ volatile("csrw " #csr ", %0" ::"r"(value))
#define TIKU_S31_MPU_CSR_ZERO(csr) \
    __asm__ volatile("csrw " #csr ", zero" ::: "memory")
#define TIKU_S31_MPU_CSR_ZERO2(first, second) \
    __asm__ volatile("csrw " #first ", zero\n\tcsrw " #second ", zero" \
                     ::: "memory")
#endif

static uint16_t permissions = TIKU_MPU_DEFAULT_SAM;
static uint16_t control;

/** @brief Make the durable span writable exactly while the mask allows
 *         writes; the clobber keeps stores on their side of the change. */
static void apply(void)
{
    uint32_t cfg = PMA_TOR | PMA_EN | PMA_R | PMA_NONCACHEABLE;

    if (permissions & SAM_WRITE_BITS) {
        cfg |= PMA_W;
    }
    TIKU_S31_MPU_CSR_WRITE(0xBC2, cfg);
}

int tiku_s31_durable_guard_off(void)
{
    uint32_t first,second;
    TIKU_S31_MPU_CSR_READ(0xBC1,first);TIKU_S31_MPU_CSR_READ(0xBC2,second);
    if((first|second)&(1u<<29))return -1;
    TIKU_S31_MPU_CSR_ZERO2(0xBC2, 0xBC1);
    TIKU_S31_MPU_CSR_READ(0xBC1,first);TIKU_S31_MPU_CSR_READ(0xBC2,second);
    return ((first|second) & ~PMA_NONCACHEABLE) ? -1 : 0;
}
int tiku_s31_mpu_ready(void)
{
    uint32_t first,second,begin,end;
    TIKU_S31_MPU_CSR_READ(0xBC1,first);TIKU_S31_MPU_CSR_READ(0xBC2,second);
    TIKU_S31_MPU_CSR_READ(0xBD1,begin);TIKU_S31_MPU_CSR_READ(0xBD2,end);
    return !(first & ~PMA_NONCACHEABLE)&&(second & ~PMA_NONCACHEABLE)==(PMA_TOR|PMA_EN|PMA_R)&&
        begin==((uint32_t)(uintptr_t)__uninit_start>>2)&&end==((uint32_t)(uintptr_t)__uninit_end>>2);
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
    /* The durable span is writable while a SAM_WRITE_BITS bit is set. */
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
    /* Entries 1 and 2 form a TOR range over durable SRAM. */
    TIKU_S31_MPU_CSR_ZERO(0xBC1);
    TIKU_S31_MPU_CSR_WRITE_ADDR(0xBD1, (uint32_t)(uintptr_t)__uninit_start >> 2);
    TIKU_S31_MPU_CSR_WRITE_ADDR(0xBD2, (uint32_t)(uintptr_t)__uninit_end >> 2);
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

uint16_t tiku_mpu_arch_get_violation_flags(void)
{
    /* A blocked store is a store access fault, which the trap handler
     * records (`diag fault`); no flags latch. */
    return 0;
}
void tiku_mpu_arch_clear_violation_flags(void)
{
}
void tiku_mpu_arch_enable_violation_nmi(void)
{
}

void tiku_mpu_arch_module_window_exec(int enable)
{
    /* This firmware exposes no loadable-module window. */
    (void)enable;
}
