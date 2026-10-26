/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cold_arch.c - Native peripheral initialization before the C6 kernel starts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <arch/esp32c6/tiku_native_arch.h>
extern uint32_t __real_native_main(uint32_t *report);
extern uint32_t __real_tiku_c6_qualify(uint32_t *report);
static uint32_t reset_reason;
static uint32_t dma_security,access_filters;

/** @brief Attach reset cause, cold-entry identity and inherited DMA policy to the report. */
uint32_t __wrap_tiku_c6_qualify(uint32_t *report)
{
    uint32_t result=__real_tiku_c6_qualify(report);
    report[58]=reset_reason;report[59]=0x434f4c44u;
    report[60]=access_filters;report[61]=dma_security;
    return result;
}
/** @brief Record startup progress inside the reserved native SRAM arena. */
static void stage(unsigned value)
{
    C6_REG(C6_COLD_LOG+4u)=value;
}

/** @brief Initialize timer/watchdog ownership before entering the native kernel. */
uint32_t __wrap_native_main(uint32_t *report)
{
    const uint32_t key=0x50d83aa1u;
    reset_reason=((uint32_t (*)(uint32_t))0x40000018u)(0);
    C6_REG(C6_COLD_LOG+32u)=reset_reason;
    __asm__ volatile("csrw 0x7e1,zero\n\tcsrw 0x7e2,zero\n\tcsrw 0x7e0,%0\n\tcsrw 0x7e1,%0"::"r"(1u):"memory");
    stage(2);
    dma_security=C6_REG(0x6009800cu);
    access_filters=C6_REG(0x600990c4u);
    /* The fixed native SDIO descriptors and stages belong to the trusted kernel. */
    C6_REG(0x6009800cu)=0;
    __asm__ volatile("fence iorw,iorw":::"memory");
    C6_REG(0x600c2000u)&=~0x300u;
    C6_REG(0x60096054u)|=1u;
    C6_REG(0x60096054u)&=~2u;
    C6_REG(0x60096058u)=(C6_REG(0x60096058u)&~(1u<<20))|(1u<<22);
    C6_REG(C6_SYSTIMER)|=(1u<<31)|(1u<<30)|1u;
    C6_REG(C6_PLIC)=0;
    C6_REG(C6_PLIC+0x90)=1;
    stage(3);
    C6_REG(0x6009603cu)=(C6_REG(0x6009603cu)|1u)&~2u;
    C6_REG(0x60096044u)=(C6_REG(0x60096044u)&~(3u<<20))|(1u<<22);
    stage(4);
    C6_REG(0x60008064u)=key;
    C6_REG(0x60008048u)=0;
    C6_REG(0x6000804cu)=20000u<<16;
    C6_REG(0x60008050u)=6000u;
    C6_REG(0x60008048u)=(1u<<31)|(3u<<29)|(1u<<22)|(1u<<21)|(7u<<18)|(7u<<15);
    C6_REG(0x60008060u)=1;
    C6_REG(0x60008064u)=0;
    stage(5);
    C6_REG(0x600b1c18u)=key;
    C6_REG(0x600b1c00u)&=~(1u<<31);
    C6_REG(0x600b1c18u)=0;
    stage(6);
    return __real_native_main(report);
}
