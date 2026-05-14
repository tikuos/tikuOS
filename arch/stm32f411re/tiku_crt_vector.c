/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_crt_vector.c — Cortex-M4 / STM32F411xC/E vector table
 *
 * Source: RM0383 Rev 4, Table 37 "Vector table for STM32F411xC/E"
 *         §10.1 Nested vectored interrupt controller (NVIC), p.202–205
 *
 * The table must be at ORIGIN(FLASH) = 0x08000000 so the STM32 boot
 * ROM reads the initial SP from offset 0x000 and the reset handler
 * address from offset 0x004 before any code runs.
 *
 * The linker script places section .vectors at ORIGIN(FLASH); KEEP()
 * prevents --gc-sections from discarding it; -Wl,-u,tiku_stm32f411_vectors
 * in LDFLAGS ensures the symbol is pulled in before gc-sections runs.
 *
 * The file comproses the following elements:
 *   1. Static default handler  — internal linkage, cannot be globally
 *                                overridden by accident.
 *   2. Weak aliases            — one per named IRQ; a peripheral driver
 *                                overrides its slot with a non-weak
 *                                definition of the same name in its own
 *                                translation unit.
 *   3. Vector table array      — named aliases for overridable slots,
 *                                raw static symbol for reserved slots.
 *
 * Reserved positions per Table 37 (positions 19–22, 39, 43–46, 48–55,
 * 52–55, 61–66, 74–80, 82–83, 86–90) are filled with the static default
 * handler directly — they have no meaningful name to expose.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Linker-script symbols
 * ---------------------------------------------------------------------- */

extern uint32_t __stack;   /* top of SRAM — initial SP value             */

/* -------------------------------------------------------------------------
 * Forward declaration of reset handler (defined in tiku_crt_early.c)
 * ---------------------------------------------------------------------- */

void tiku_stm32f411_reset_handler(void);

/* -------------------------------------------------------------------------
 * Default handler
 *
 * Static (internal linkage) so it cannot be accidentally overridden from
 * outside this translation unit.  Uses WFE so a JTAG halt lands on a
 * recognisable instruction rather than a tight spin consuming power.
 * If the watchdog is running it will fire and reset the device cleanly.
 * ---------------------------------------------------------------------- */

static void stm32f411_default_handler(void) {
    while (1) {
        __asm__ volatile ("wfe");
    }
}

/* -------------------------------------------------------------------------
 * Weak aliases — ARM system exceptions
 *
 * Each is independently overridable. A kernel fault handler, MPU driver,
 * or RTOS port can provide a non-weak definition of the matching name in
 * its own .c file and the linker's strong-over-weak rule does the rest.
 * ---------------------------------------------------------------------- */

/* RM0383 Table 37, fixed-priority system exceptions */
void tiku_stm32f411_nmi_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
void tiku_stm32f411_hard_fault_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
void tiku_stm32f411_mem_manage_handler(void)      /* MemManage / MPU fault          */
    __attribute__((weak, alias("stm32f411_default_handler")));
void tiku_stm32f411_bus_fault_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
void tiku_stm32f411_usage_fault_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
void tiku_stm32f411_svcall_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
void tiku_stm32f411_debug_mon_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
void tiku_stm32f411_pendsv_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
void tiku_stm32f411_systick_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));

/* -------------------------------------------------------------------------
 * Weak aliases — device-specific IRQs
 *
 * Named exactly after their RM0383 Table 37 acronyms, lowercased and
 * prefixed with tiku_stm32f411_, with _irq_handler suffix.  The position column
 * from Table 37 is reproduced in the comments for cross-reference.
 *
 * Positions 19–22 are reserved in Table 37 (no acronym assigned).
 * Position 39 (between USART2 and EXTI15_10) is reserved.
 * Positions 43–46 are reserved (between OTG_FS_WKUP and DMA1_Stream7).
 * Position 48 is reserved (between DMA1_Stream7 and SDIO).
 * Positions 52–55 are reserved (between SPI3 and DMA2_Stream0).
 * Positions 61–66 are reserved (between DMA2_Stream4 and OTG_FS).
 * Positions 74–80 are reserved (between I2C3_ER and FPU).
 * Positions 82–83 are reserved (between FPU and SPI4).
 * Positions 86–90 are reserved after SPI5 — beyond the table.
 * ---------------------------------------------------------------------- */

/* Position  0 — 0x0000 0040 */
void tiku_stm32f411_wwdg_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position  1 — 0x0000 0044 */
void tiku_stm32f411_exti16_pvd_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position  2 — 0x0000 0048 */
void tiku_stm32f411_exti21_tamp_stamp_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position  3 — 0x0000 004C */
void tiku_stm32f411_exti22_rtc_wkup_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position  4 — 0x0000 0050 */
void tiku_stm32f411_flash_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position  5 — 0x0000 0054 */
void tiku_stm32f411_rcc_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position  6 — 0x0000 0058 */
void tiku_stm32f411_exti0_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position  7 — 0x0000 005C */
void tiku_stm32f411_exti1_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position  8 — 0x0000 0060 */
void tiku_stm32f411_exti2_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position  9 — 0x0000 0064 */
void tiku_stm32f411_exti3_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 10 — 0x0000 0068 */
void tiku_stm32f411_exti4_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 11 — 0x0000 006C */
void tiku_stm32f411_dma1_stream0_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 12 — 0x0000 0070 */
void tiku_stm32f411_dma1_stream1_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 13 — 0x0000 0074 */
void tiku_stm32f411_dma1_stream2_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 14 — 0x0000 0078 */
void tiku_stm32f411_dma1_stream3_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 15 — 0x0000 007C */
void tiku_stm32f411_dma1_stream4_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 16 — 0x0000 0080 */
void tiku_stm32f411_dma1_stream5_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 17 — 0x0000 0084 */
void tiku_stm32f411_dma1_stream6_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 18 — 0x0000 0088 */
void tiku_stm32f411_adc_irq_handler(void)         /* ADC1 global                    */
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Positions 19–22: reserved — no alias, raw default in table            */
/* Position 23 — 0x0000 009C */
void tiku_stm32f411_exti9_5_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 24 — 0x0000 00A0 */
void tiku_stm32f411_tim1_brk_tim9_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 25 — 0x0000 00A4 */
void tiku_stm32f411_tim1_up_tim10_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 26 — 0x0000 00A8 */
void tiku_stm32f411_tim1_trg_com_tim11_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 27 — 0x0000 00AC */
void tiku_stm32f411_tim1_cc_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 28 — 0x0000 00B0 */
void tiku_stm32f411_tim2_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 29 — 0x0000 00B4 */
void tiku_stm32f411_tim3_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 30 — 0x0000 00B8 */
void tiku_stm32f411_tim4_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 31 — 0x0000 00BC */
void tiku_stm32f411_i2c1_ev_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 32 — 0x0000 00C0 */
void tiku_stm32f411_i2c1_er_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 33 — 0x0000 00C4 */
void tiku_stm32f411_i2c2_ev_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 34 — 0x0000 00C8 */
void tiku_stm32f411_i2c2_er_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 35 — 0x0000 00CC */
void tiku_stm32f411_spi1_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 36 — 0x0000 00D0 */
void tiku_stm32f411_spi2_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 37 — 0x0000 00D4 */
void tiku_stm32f411_usart1_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 38 — 0x0000 00D8 */
void tiku_stm32f411_usart2_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 39: reserved — no acronym in Table 37                        */
/* Position 40 — 0x0000 00E0 */
void tiku_stm32f411_exti15_10_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 41 — 0x0000 00E4 */
void tiku_stm32f411_exti17_rtc_alarm_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 42 — 0x0000 00E8 */
void tiku_stm32f411_exti18_otg_fs_wkup_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Positions 43–46: reserved                                             */
/* Position 47 — 0x0000 00FC */
void tiku_stm32f411_dma1_stream7_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 48: reserved                                                 */
/* Position 49 — 0x0000 0104 */
void tiku_stm32f411_sdio_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 50 — 0x0000 0108 */
void tiku_stm32f411_tim5_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 51 — 0x0000 010C */
void tiku_stm32f411_spi3_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Positions 52–55: reserved                                             */
/* Position 56 — 0x0000 0120 */
void tiku_stm32f411_dma2_stream0_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 57 — 0x0000 0124 */
void tiku_stm32f411_dma2_stream1_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 58 — 0x0000 0128 */
void tiku_stm32f411_dma2_stream2_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 59 — 0x0000 012C */
void tiku_stm32f411_dma2_stream3_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 60 — 0x0000 0130 */
void tiku_stm32f411_dma2_stream4_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Positions 61–66: reserved                                             */
/* Position 67 — 0x0000 014C */
void tiku_stm32f411_otg_fs_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 68 — 0x0000 0150 */
void tiku_stm32f411_dma2_stream5_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 69 — 0x0000 0154 */
void tiku_stm32f411_dma2_stream6_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 70 — 0x0000 0158 */
void tiku_stm32f411_dma2_stream7_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 71 — 0x0000 015C */
void tiku_stm32f411_usart6_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 72 — 0x0000 0160 */
void tiku_stm32f411_i2c3_ev_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 73 — 0x0000 0164 */
void tiku_stm32f411_i2c3_er_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Positions 74–80: reserved                                             */
/* Position 81 — 0x0000 0184 */
void tiku_stm32f411_fpu_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Positions 82–83: reserved                                             */
/* Position 84 — 0x0000 0190 */
void tiku_stm32f411_spi4_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));
/* Position 85 — 0x0000 0194 */
void tiku_stm32f411_spi5_irq_handler(void)
    __attribute__((weak, alias("stm32f411_default_handler")));

/* -------------------------------------------------------------------------
 * Vector table
 *
 * Placed in .vectors which the linker script puts at ORIGIN(FLASH) =
 * 0x08000000.  `used` prevents the compiler discarding the array;
 * KEEP(.vectors) in the linker script prevents the section being
 * gc'd; -Wl,-u,tiku_stm32f411_vectors ensures the symbol is pulled
 * in before gc-sections evaluates reachability.
 *
 * Layout follows RM0383 Table 37 exactly.  Reserved positions use the
 * static default handler directly — they carry no exported weak alias
 * because no driver should ever claim them.
 *
 * Addresses in the right-hand comments are the aliased XIP addresses
 * (0x0000_xxxx) from Table 37, not the physical flash addresses
 * (0x0800_xxxx), since Table 37 lists the alias-space view.
 * ---------------------------------------------------------------------- */

__attribute__((section(".vectors"), used))
void (*const tiku_stm32f411_vectors[])(void) = {

    /* ---- ARM system exceptions (RM0383 Table 37, grey rows) ---------- */

    (void (*)(void))&__stack,           /* 0x0000 0000  Initial SP        */
    tiku_stm32f411_reset_handler,       /* 0x0000 0004  Reset             */
    tiku_stm32f411_nmi_handler,                   /* 0x0000 0008  NMI               */
    tiku_stm32f411_hard_fault_handler,            /* 0x0000 000C  HardFault         */
    tiku_stm32f411_mem_manage_handler,            /* 0x0000 0010  MemManage         */
    tiku_stm32f411_bus_fault_handler,             /* 0x0000 0014  BusFault          */
    tiku_stm32f411_usage_fault_handler,           /* 0x0000 0018  UsageFault        */
    stm32f411_default_handler,          /* 0x0000 001C  Reserved          */
    stm32f411_default_handler,          /* 0x0000 0020  Reserved          */
    stm32f411_default_handler,          /* 0x0000 0024  Reserved          */
    stm32f411_default_handler,          /* 0x0000 0028  Reserved          */
    tiku_stm32f411_svcall_handler,                /* 0x0000 002C  SVCall            */
    tiku_stm32f411_debug_mon_handler,             /* 0x0000 0030  Debug Monitor     */
    stm32f411_default_handler,          /* 0x0000 0034  Reserved          */
    tiku_stm32f411_pendsv_handler,                /* 0x0000 0038  PendSV            */
    tiku_stm32f411_systick_handler,               /* 0x0000 003C  SysTick           */

    /* ---- Device IRQs (RM0383 Table 37, position column) -------------- */

    tiku_stm32f411_wwdg_irq_handler,              /* pos  0  0x0000 0040  WWDG                    */
    tiku_stm32f411_exti16_pvd_irq_handler,        /* pos  1  0x0000 0044  EXTI16/PVD              */
    tiku_stm32f411_exti21_tamp_stamp_irq_handler, /* pos  2  0x0000 0048  EXTI21/TAMP_STAMP       */
    tiku_stm32f411_exti22_rtc_wkup_irq_handler,   /* pos  3  0x0000 004C  EXTI22/RTC_WKUP         */
    tiku_stm32f411_flash_irq_handler,             /* pos  4  0x0000 0050  FLASH                   */
    tiku_stm32f411_rcc_irq_handler,               /* pos  5  0x0000 0054  RCC                     */
    tiku_stm32f411_exti0_irq_handler,             /* pos  6  0x0000 0058  EXTI0                   */
    tiku_stm32f411_exti1_irq_handler,             /* pos  7  0x0000 005C  EXTI1                   */
    tiku_stm32f411_exti2_irq_handler,             /* pos  8  0x0000 0060  EXTI2                   */
    tiku_stm32f411_exti3_irq_handler,             /* pos  9  0x0000 0064  EXTI3                   */
    tiku_stm32f411_exti4_irq_handler,             /* pos 10  0x0000 0068  EXTI4                   */
    tiku_stm32f411_dma1_stream0_irq_handler,      /* pos 11  0x0000 006C  DMA1_Stream0            */
    tiku_stm32f411_dma1_stream1_irq_handler,      /* pos 12  0x0000 0070  DMA1_Stream1            */
    tiku_stm32f411_dma1_stream2_irq_handler,      /* pos 13  0x0000 0074  DMA1_Stream2            */
    tiku_stm32f411_dma1_stream3_irq_handler,      /* pos 14  0x0000 0078  DMA1_Stream3            */
    tiku_stm32f411_dma1_stream4_irq_handler,      /* pos 15  0x0000 007C  DMA1_Stream4            */
    tiku_stm32f411_dma1_stream5_irq_handler,      /* pos 16  0x0000 0080  DMA1_Stream5            */
    tiku_stm32f411_dma1_stream6_irq_handler,      /* pos 17  0x0000 0084  DMA1_Stream6            */
    tiku_stm32f411_adc_irq_handler,               /* pos 18  0x0000 0088  ADC (ADC1 global)       */
    stm32f411_default_handler,          /* pos 19  0x0000 008C  Reserved                */
    stm32f411_default_handler,          /* pos 20  0x0000 0090  Reserved                */
    stm32f411_default_handler,          /* pos 21  0x0000 0094  Reserved                */
    stm32f411_default_handler,          /* pos 22  0x0000 0098  Reserved                */
    tiku_stm32f411_exti9_5_irq_handler,           /* pos 23  0x0000 009C  EXTI9_5                 */
    tiku_stm32f411_tim1_brk_tim9_irq_handler,     /* pos 24  0x0000 00A0  TIM1_BRK / TIM9        */
    tiku_stm32f411_tim1_up_tim10_irq_handler,     /* pos 25  0x0000 00A4  TIM1_UP / TIM10        */
    tiku_stm32f411_tim1_trg_com_tim11_irq_handler,/* pos 26  0x0000 00A8  TIM1_TRG_COM / TIM11   */
    tiku_stm32f411_tim1_cc_irq_handler,           /* pos 27  0x0000 00AC  TIM1_CC                 */
    tiku_stm32f411_tim2_irq_handler,              /* pos 28  0x0000 00B0  TIM2                    */
    tiku_stm32f411_tim3_irq_handler,              /* pos 29  0x0000 00B4  TIM3                    */
    tiku_stm32f411_tim4_irq_handler,              /* pos 30  0x0000 00B8  TIM4                    */
    tiku_stm32f411_i2c1_ev_irq_handler,           /* pos 31  0x0000 00BC  I2C1_EV                 */
    tiku_stm32f411_i2c1_er_irq_handler,           /* pos 32  0x0000 00C0  I2C1_ER                 */
    tiku_stm32f411_i2c2_ev_irq_handler,           /* pos 33  0x0000 00C4  I2C2_EV                 */
    tiku_stm32f411_i2c2_er_irq_handler,           /* pos 34  0x0000 00C8  I2C2_ER                 */
    tiku_stm32f411_spi1_irq_handler,              /* pos 35  0x0000 00CC  SPI1                    */
    tiku_stm32f411_spi2_irq_handler,              /* pos 36  0x0000 00D0  SPI2                    */
    tiku_stm32f411_usart1_irq_handler,            /* pos 37  0x0000 00D4  USART1                  */
    tiku_stm32f411_usart2_irq_handler,            /* pos 38  0x0000 00D8  USART2                  */
    stm32f411_default_handler,          /* pos 39  0x0000 00DC  Reserved                */
    tiku_stm32f411_exti15_10_irq_handler,         /* pos 40  0x0000 00E0  EXTI15_10               */
    tiku_stm32f411_exti17_rtc_alarm_irq_handler,  /* pos 41  0x0000 00E4  EXTI17/RTC_Alarm        */
    tiku_stm32f411_exti18_otg_fs_wkup_irq_handler,/* pos 42  0x0000 00E8  EXTI18/OTG_FS_WKUP     */
    stm32f411_default_handler,          /* pos 43  0x0000 00EC  Reserved                */
    stm32f411_default_handler,          /* pos 44  0x0000 00F0  Reserved                */
    stm32f411_default_handler,          /* pos 45  0x0000 00F4  Reserved                */
    stm32f411_default_handler,          /* pos 46  0x0000 00F8  Reserved                */
    tiku_stm32f411_dma1_stream7_irq_handler,      /* pos 47  0x0000 00FC  DMA1_Stream7            */
    stm32f411_default_handler,          /* pos 48  0x0000 0100  Reserved                */
    tiku_stm32f411_sdio_irq_handler,              /* pos 49  0x0000 0104  SDIO                    */
    tiku_stm32f411_tim5_irq_handler,              /* pos 50  0x0000 0108  TIM5                    */
    tiku_stm32f411_spi3_irq_handler,              /* pos 51  0x0000 010C  SPI3                    */
    stm32f411_default_handler,          /* pos 52  0x0000 0110  Reserved                */
    stm32f411_default_handler,          /* pos 53  0x0000 0114  Reserved                */
    stm32f411_default_handler,          /* pos 54  0x0000 0118  Reserved                */
    stm32f411_default_handler,          /* pos 55  0x0000 011C  Reserved                */
    tiku_stm32f411_dma2_stream0_irq_handler,      /* pos 56  0x0000 0120  DMA2_Stream0            */
    tiku_stm32f411_dma2_stream1_irq_handler,      /* pos 57  0x0000 0124  DMA2_Stream1            */
    tiku_stm32f411_dma2_stream2_irq_handler,      /* pos 58  0x0000 0128  DMA2_Stream2            */
    tiku_stm32f411_dma2_stream3_irq_handler,      /* pos 59  0x0000 012C  DMA2_Stream3            */
    tiku_stm32f411_dma2_stream4_irq_handler,      /* pos 60  0x0000 0130  DMA2_Stream4            */
    stm32f411_default_handler,          /* pos 61  0x0000 0134  Reserved                */
    stm32f411_default_handler,          /* pos 62  0x0000 0138  Reserved                */
    stm32f411_default_handler,          /* pos 63  0x0000 013C  Reserved                */
    stm32f411_default_handler,          /* pos 64  0x0000 0140  Reserved                */
    stm32f411_default_handler,          /* pos 65  0x0000 0144  Reserved                */
    stm32f411_default_handler,          /* pos 66  0x0000 0148  Reserved                */
    tiku_stm32f411_otg_fs_irq_handler,            /* pos 67  0x0000 014C  OTG_FS                  */
    tiku_stm32f411_dma2_stream5_irq_handler,      /* pos 68  0x0000 0150  DMA2_Stream5            */
    tiku_stm32f411_dma2_stream6_irq_handler,      /* pos 69  0x0000 0154  DMA2_Stream6            */
    tiku_stm32f411_dma2_stream7_irq_handler,      /* pos 70  0x0000 0158  DMA2_Stream7            */
    tiku_stm32f411_usart6_irq_handler,            /* pos 71  0x0000 015C  USART6                  */
    tiku_stm32f411_i2c3_ev_irq_handler,           /* pos 72  0x0000 0160  I2C3_EV                 */
    tiku_stm32f411_i2c3_er_irq_handler,           /* pos 73  0x0000 0164  I2C3_ER                 */
    stm32f411_default_handler,          /* pos 74  0x0000 0168  Reserved                */
    stm32f411_default_handler,          /* pos 75  0x0000 016C  Reserved                */
    stm32f411_default_handler,          /* pos 76  0x0000 0170  Reserved                */
    stm32f411_default_handler,          /* pos 77  0x0000 0174  Reserved                */
    stm32f411_default_handler,          /* pos 78  0x0000 0178  Reserved                */
    stm32f411_default_handler,          /* pos 79  0x0000 017C  Reserved                */
    stm32f411_default_handler,          /* pos 80  0x0000 0180  Reserved                */
    tiku_stm32f411_fpu_irq_handler,               /* pos 81  0x0000 0184  FPU                     */
    stm32f411_default_handler,          /* pos 82  0x0000 0188  Reserved                */
    stm32f411_default_handler,          /* pos 83  0x0000 018C  Reserved                */
    tiku_stm32f411_spi4_irq_handler,              /* pos 84  0x0000 0190  SPI4                    */
    tiku_stm32f411_spi5_irq_handler,              /* pos 85  0x0000 0194  SPI5                    */
};
