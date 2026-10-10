/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_esp32c5_regs.h - C5 ROM identity and USB Serial/JTAG registers.
 * Values follow ESP-IDF 4d59230ddff16327812782151ef0afef202dc6d7,
 * esp32c5.rom.version.ld, reg_base.h and usb_serial_jtag_reg.h.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_REGS_H_
#define TIKU_ESP32C5_REGS_H_

#include <stdint.h>
#include "tiku_device_select.h"

#define TIKU_C5_ROM_CHIP_ID     0x40000010UL
#define TIKU_C5_ROM_ECO         0x40000014UL
#define TIKU_C5_USB_EP1         0x6000F000UL
#define TIKU_C5_USB_EP1_CONF    0x6000F004UL
#define TIKU_C5_USB_INT_ENA     0x6000F010UL
#define TIKU_C5_USB_FRAM_NUM    0x6000F024UL
#define TIKU_C5_USB_WR_DONE     (1UL << 0)
#define TIKU_C5_USB_TX_FREE     (1UL << 1)
#define TIKU_C5_USB_RX_AVAIL    (1UL << 2)
#define TIKU_C5_USB_PACKET_SIZE 64u

/* C5 timer_group_reg.h, lp_wdt_reg.h and lpwdt_ll.h at the same IDF pin. */
#define TIKU_C5_TG0_WDT_CONFIG   0x60008048UL
#define TIKU_C5_TG1_WDT_CONFIG   0x60009048UL
#define TIKU_C5_TG0_WDT_PROTECT  0x60008064UL
#define TIKU_C5_TG1_WDT_PROTECT  0x60009064UL
#define TIKU_C5_LP_WDT_CONFIG    0x600B1C00UL
#define TIKU_C5_LP_WDT_PROTECT   0x600B1C18UL
#define TIKU_C5_SWD_CONFIG       0x600B1C1CUL
#define TIKU_C5_SWD_PROTECT      0x600B1C20UL
#define TIKU_C5_WDT_KEY          0x50D83AA1UL
#define TIKU_C5_WDT_ENABLE       (1UL << 31)
#define TIKU_C5_TG_WDT_FLASHBOOT (1UL << 14)
#define TIKU_C5_LP_WDT_FLASHBOOT (1UL << 12)
#define TIKU_C5_SWD_AUTO_FEED    (1UL << 18)

/* C5 clic_reg.h, interrupts.h and interrupt_matrix_reg.h at the IDF pin. */
#define TIKU_C5_IRQ_LINES           32u
#define TIKU_C5_IRQ_SOURCES         84u
#define TIKU_C5_IRQ_MAP(source)     (0x60010000UL + 4u * (source))
#define TIKU_C5_CLIC_CONFIG         0x20800000UL
#define TIKU_C5_CLIC_CTRL(id)       (0x20801000UL + 4u * (id))
#define TIKU_C5_CLIC_IE             (1UL << 8)
#define TIKU_C5_SYSTIMER_IRQ0       61u
#define TIKU_C5_UART0_IRQ_SOURCE    47u
#define TIKU_C5_UART0_IRQ_LINE      3u
#define TIKU_C5_UART0_INT_ENABLE    0x6000000CUL
#define TIKU_C5_UART0_RX_INTERRUPTS 0x11Du

/* C5 pcr_reg.h and systimer_reg.h at the IDF pin. */
#define TIKU_C5_PCR_SYSTIMER            0x6009606CUL
#define TIKU_C5_PCR_SYSTIMER_CLK        0x60096070UL
#define TIKU_C5_SYSTIMER_CONF           0x6000A000UL
#define TIKU_C5_SYSTIMER_OP             0x6000A004UL
#define TIKU_C5_SYSTIMER_TARGET_HI(n)   (0x6000A01CUL + 8u * (n))
#define TIKU_C5_SYSTIMER_TARGET_LO(n)   (0x6000A020UL + 8u * (n))
#define TIKU_C5_SYSTIMER_TARGET_CFG(n)  (0x6000A034UL + 4u * (n))
#define TIKU_C5_SYSTIMER_VALUE_HI       0x6000A040UL
#define TIKU_C5_SYSTIMER_VALUE_LO       0x6000A044UL
#define TIKU_C5_SYSTIMER_TARGET_LOAD(n) (0x6000A050UL + 4u * (n))
#define TIKU_C5_SYSTIMER_INT_ENA        0x6000A064UL
#define TIKU_C5_SYSTIMER_INT_CLR        0x6000A06CUL
#define TIKU_C5_SYSTIMER_INT_ST         0x6000A070UL
#define TIKU_C5_SYSTIMER_VALID          (1UL << 29)
#define TIKU_C5_SYSTIMER_UPDATE         (1UL << 30)
#define TIKU_C5_SYSTIMER_TARGET(n)      (1UL << (24u - (n)))
#define TIKU_C5_SYSTIMER_MASK           ((1ULL << 52) - 1u)
#define TIKU_C5_SYSTIMER_HZ             16000000UL

#ifndef TIKU_C5_SET_THRESHOLD
#define TIKU_C5_SET_THRESHOLD(value)                                           \
    __asm__ volatile("csrw 0x347, %0" ::"r"((uint32_t)(value)) : "memory")
#endif

#ifndef TIKU_C5_REG_READ
#define TIKU_C5_REG_READ(address) (*(volatile uint32_t *)(uintptr_t)(address))
#endif
#ifndef TIKU_C5_REG_WRITE
#define TIKU_C5_REG_WRITE(address, value)                                      \
    (*(volatile uint32_t *)(uintptr_t)(address) = (uint32_t)(value))
#endif

#endif
