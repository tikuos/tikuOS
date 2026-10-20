/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_esp32p4_regs.h - ESP32-P4 v1.3 native register addresses.
 *
 * ROM identity, L2 cache, USB Serial/JTAG, watchdog, clock, PMU, eFuse,
 * interrupt matrix, CLIC and SYSTIMER registers used by this port.
 * Addresses: ESP-IDF 4d59230ddff16327812782151ef0afef202dc6d7 hw_ver1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_REGS_H_
#define TIKU_ESP32P4_REGS_H_
#include <stdint.h>

#define TIKU_P4_ROM_CHIP_ID 0x4fc00010UL
#define TIKU_P4_ROM_ECO     0x4fc00014UL
#define TIKU_P4_L2_SIZE    0x3ff10278UL
#define TIKU_P4_L2_LINE    0x3ff1027cUL
#define TIKU_P4_USB_EP1      0x500d2000UL
#define TIKU_P4_USB_EP1_CONF 0x500d2004UL
#define TIKU_P4_USB_INT_ENA  0x500d2010UL
#define TIKU_P4_USB_FRAM_NUM 0x500d2024UL
#define TIKU_P4_USB_WR_DONE  (1u << 0)
#define TIKU_P4_USB_TX_FREE  (1u << 1)
#define TIKU_P4_USB_RX_AVAIL (1u << 2)
#define TIKU_P4_USB_PACKET_SIZE 64u

#define TIKU_P4_TG0_WDT_CONFIG  0x500c2048UL
#define TIKU_P4_TG1_WDT_CONFIG  0x500c3048UL
#define TIKU_P4_TG0_WDT_PROTECT 0x500c2064UL
#define TIKU_P4_TG0_WDT_PRESCALE 0x500c204cUL
#define TIKU_P4_TG0_WDT_HOLD 0x500c2050UL
#define TIKU_P4_TG0_WDT_FEED 0x500c2060UL
#define TIKU_P4_TG0_CLOCK 0x500e6094UL
#define TIKU_P4_LP_SYSTEM_CTRL 0x50110008UL
#define TIKU_P4_RESET_CAUSE 0x50111010UL
#define TIKU_P4_TG1_WDT_PROTECT 0x500c3064UL
#define TIKU_P4_LP_WDT_CONFIG  0x50116000UL
#define TIKU_P4_LP_WDT_PROTECT 0x50116018UL
#define TIKU_P4_SWD_CONFIG     0x5011601cUL
#define TIKU_P4_SWD_PROTECT    0x50116020UL
#define TIKU_P4_WDT_KEY       0x50d83aa1UL
#define TIKU_P4_WDT_ENABLE   (1u << 31)
#define TIKU_P4_TG_WDT_FLASHBOOT (1u << 14)
#define TIKU_P4_LP_WDT_FLASHBOOT (1u << 12)
#define TIKU_P4_SWD_AUTO_FEED    (1u << 18)
#define TIKU_P4_SOC_CLK0   0x500e6014UL
#define TIKU_P4_SOC_CLK2   0x500e601cUL
#define TIKU_P4_HP_RESET0  0x500e60c0UL
#define TIKU_P4_HP_RESET1  0x500e60c4UL
#define TIKU_P4_LP_CLK     0x50120000UL
#define TIKU_P4_LP_RESET   0x50120008UL
/* LP core control (lpperi_reg.h, lp_system_reg.h, pmu_reg.h at the pin).
 * TIKU_P4_LP_CLK and TIKU_P4_LP_RESET bit 31 are its clock and reset. */
#define TIKU_P4_LP_CORE_BIT      (1u << 31)
#define TIKU_P4_LPPERI_CPU       0x5012000cUL  /* bit 31: debug module off */
#define TIKU_P4_LP_SYS_BOOT_ADDR 0x50110018UL
#define TIKU_P4_LP_SYS_DBG_PC    0x50110190UL
#define TIKU_P4_LP_SYS_EXC_PC    0x50110194UL
#define TIKU_P4_PMU_HP_INT_RAW   0x50115164UL  /* bit 27: LP core exception */
#define TIKU_P4_PMU_HP_INT_CLR   0x50115170UL
#define TIKU_P4_PMU_LP_CPU_PWR0  0x50115184UL  /* 29 stall, 30 reset at sleep */
#define TIKU_P4_PMU_LP_CPU_PWR2  0x5011518cUL  /* wake sources, 22 = HP core */
#define TIKU_P4_PMU_HP_LP_COMM   0x5011519cUL  /* 30 LP wakes HP, 31 HP wakes LP */
#define TIKU_P4_LP_CPU_EXC_INT   (1u << 27)
#define TIKU_P4_LP_SLP_STALL_EN  (1u << 29)
#define TIKU_P4_LP_SLP_RESET_EN  (1u << 30)
#define TIKU_P4_LP_WAKE_BY_HP    (1u << 22)
#define TIKU_P4_HP_TRIGGER_LP    (1u << 31)
#define TIKU_P4_LP_GPIO_IN       0x5012A030UL  /* pad levels of LP IO 0..15 */
#define TIKU_P4_SYSTIMER_CLK 0x500e6098UL
#define TIKU_P4_ROOT_SELECT 0x50111040UL
#define TIKU_P4_ROOT_DIV0 0x500e6004UL
#define TIKU_P4_ROOT_DIV1 0x500e6008UL
#define TIKU_P4_ROOT_DIV2 0x500e600cUL
#define TIKU_P4_ROOT_DIV3 0x500e6010UL
#define TIKU_P4_PLL_CAL 0x500e60bcUL
#define TIKU_P4_CPLL_CAL_END (1u << 2)
#define TIKU_P4_CPLL_CAL_STOP (1u << 3)
#define TIKU_P4_PMU_HP_BIAS 0x50115018UL
#define TIKU_P4_PMU_HP_REGULATOR 0x50115028UL
#define TIKU_P4_PMU_LP_REGULATOR 0x5011509cUL
#define TIKU_P4_PMU_CLOCK_POWER 0x501150ccUL
#define TIKU_P4_PMU_DCDC_SWITCH 0x5011510cUL
#define TIKU_P4_PMU_DCDC 0x50115204UL
#define TIKU_P4_CPLL_POWER_UP ((1u << 23) | (1u << 27))
#define TIKU_P4_CPLL_CLOCK_UP (1u << 17)
#define TIKU_P4_CPLL_POWER_DOWN ((1u << 7) | (1u << 11))
#define TIKU_P4_CPLL_CLOCK_DOWN (1u << 1)
#define TIKU_P4_HP_LDO_ENABLE (1u << 18)
#define TIKU_P4_PMU_DBIAS_SELECT (1u << 14)
#define TIKU_P4_ANALOG_CTRL 0x50124000UL
#define TIKU_P4_ANALOG_CONF1 0x5012401cUL
#define TIKU_P4_ANALOG_CONF2 0x50124020UL
#define TIKU_P4_ANALOG_CLK 0x50124034UL
#define TIKU_P4_ANALOG_ENABLE (1u << 27)
#define TIKU_P4_ANALOG_BUSY (1u << 25)
#define TIKU_P4_ANALOG_WRITE (1u << 24)
#define TIKU_P4_EFUSE_BLOCK_VERSION 0x5012d04cUL
#define TIKU_P4_EFUSE_BIAS 0x5012d054UL
#define TIKU_P4_EFUSE_SYS3 0x5012d050UL   /* bits 5:3: LDO VO2 mul */
/* PMU external LDO VO2, the PSRAM supply (ldo_ll.h unit 1, ext_ldo[3]). */
#define TIKU_P4_PSRAM_LDO      0x501151d0UL
#define TIKU_P4_PSRAM_LDO_ANA  0x501151d4UL

#define TIKU_P4_IRQ_LINES 32u
#define TIKU_P4_IRQ_SOURCES 128u
#define TIKU_P4_IRQ_MAP(source) (0x500d6000UL + 4u * (source))
#define TIKU_P4_CLIC_CONFIG 0x20800000UL
#define TIKU_P4_CLIC_THRESHOLD 0x20800008UL
#define TIKU_P4_CLIC_CTRL(id) (0x20801000UL + 4u * (id))
#define TIKU_P4_CLIC_IE (1u << 8)
#define TIKU_P4_SYSTIMER_IRQ0 53u
#define TIKU_P4_SYSTIMER_CONF 0x500e2000UL
#define TIKU_P4_SYSTIMER_OP   0x500e2004UL
#define TIKU_P4_SYSTIMER_TARGET_HI(n) (0x500e201cUL + 8u * (n))
#define TIKU_P4_SYSTIMER_TARGET_LO(n) (0x500e2020UL + 8u * (n))
#define TIKU_P4_SYSTIMER_TARGET_CFG(n) (0x500e2034UL + 4u * (n))
#define TIKU_P4_SYSTIMER_VALUE_HI 0x500e2040UL
#define TIKU_P4_SYSTIMER_VALUE_LO 0x500e2044UL
#define TIKU_P4_SYSTIMER_TARGET_LOAD(n) (0x500e2050UL + 4u * (n))
#define TIKU_P4_SYSTIMER_INT_ENA 0x500e2064UL
#define TIKU_P4_SYSTIMER_INT_CLR 0x500e206cUL
#define TIKU_P4_SYSTIMER_INT_ST  0x500e2070UL
#define TIKU_P4_SYSTIMER_VALID (1u << 29)
#define TIKU_P4_SYSTIMER_UPDATE (1u << 30)
#define TIKU_P4_SYSTIMER_TARGET(n) (1u << (24u - (n)))
#define TIKU_P4_SYSTIMER_MASK ((1ULL << 52) - 1u)
#define TIKU_P4_SYSTIMER_HZ 16000000UL

#ifndef TIKU_P4_REG_READ
#define TIKU_P4_REG_READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#endif
#ifndef TIKU_P4_REG_WRITE
#define TIKU_P4_REG_WRITE(a, v) \
    (*(volatile uint32_t *)(uintptr_t)(a) = (uint32_t)(v))
#endif
#endif
