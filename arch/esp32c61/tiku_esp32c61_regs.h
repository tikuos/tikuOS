/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_esp32c61_regs.h - ESP32-C61 registers and CSRs this port touches.
 *
 * Hand-written from Espressif's register headers rather than vendoring an
 * SDK, in the RP2350 and STM32N6 style: each block names its peripheral.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_REGS_H_
#define TIKU_ESP32C61_REGS_H_

#include <stdint.h>

#define TIKU_REG32(a)               (*(volatile uint32_t *)(uintptr_t)(a))

/*---------------------------------------------------------------------------*/
/* CSRS                                                                      */
/*---------------------------------------------------------------------------*/

/* The CSR is named in the instruction itself, so these stay macros; the
 * name expands first, so a numbered CSR may be passed by its macro. */
#define ESP32C61_STR_(x)            #x
#define ESP32C61_STR(x)             ESP32C61_STR_(x)
#define ESP32C61_CSR_READ(csr) __extension__ ({                             \
        uint32_t v_;                                                        \
        __asm__ volatile ("csrr %0, " ESP32C61_STR(csr) : "=r" (v_));       \
        v_; })
#define ESP32C61_CSR_WRITE(csr, v) \
        __asm__ volatile ("csrw " ESP32C61_STR(csr) ", %0" \
                          :: "r" ((uint32_t)(v)) : "memory")
#define ESP32C61_CSR_SET(csr, v) \
        __asm__ volatile ("csrs " ESP32C61_STR(csr) ", %0" \
                          :: "r" ((uint32_t)(v)) : "memory")
#define ESP32C61_CSR_CLEAR(csr, v) \
        __asm__ volatile ("csrc " ESP32C61_STR(csr) ", %0" \
                          :: "r" ((uint32_t)(v)) : "memory")

#define ESP32C61_MSTATUS_MIE        (1UL << 3)

/* mtvec mode 3 is CLIC: exceptions enter at the 64-byte aligned base. */
#define ESP32C61_MTVEC_MODE_CLIC    3UL

/* CLIC CSRs the assembler has no names for: an interrupt is taken only
 * above max(mintstatus level, mintthresh). */
#define ESP32C61_CSR_MINTTHRESH     0x347
#define ESP32C61_CSR_MINTSTATUS     0xFB1

/*---------------------------------------------------------------------------*/
/* ROM ENTRY POINTS (esp32c61.rom.ld)                                        */
/*---------------------------------------------------------------------------*/

/* Reset cause codes, as the ROM reports them. */
#define ESP32C61_ROM_RESET_REASON   ((uint32_t (*)(int))0x40000018UL)
#define ESP32C61_ROM_SOFTWARE_RESET ((void (*)(void))0x40000094UL)
/* The ROM's own delays read this after every core clock change. */
#define ESP32C61_ROM_SET_CPU_MHZ    ((void (*)(uint32_t))0x40000044UL)

/* SPI flash, through the ROM's own routines: 0 is success, 1 an error,
 * 2 a timeout.  Write and read take 4-byte aligned addresses and lengths. */
typedef int (*esp32c61_rom_flash_config_t)(uint32_t id, uint32_t size,
                                           uint32_t block, uint32_t sector,
                                           uint32_t page, uint32_t status_mask);
typedef int (*esp32c61_rom_flash_write_t)(uint32_t addr, const uint32_t *src,
                                          int32_t len);
typedef int (*esp32c61_rom_flash_read_t)(uint32_t addr, uint32_t *dst,
                                         int32_t len);
typedef int (*esp32c61_rom_mmu_set_t)(uint32_t sensitive, uint32_t ext_ram,
                                      uint32_t vaddr, uint32_t paddr,
                                      uint32_t page_kb, uint32_t pages,
                                      uint32_t fixed);

#define ESP32C61_ROM_FLASH_ATTACH   ((void (*)(uint32_t, uint32_t))0x400001F0UL)
#define ESP32C61_ROM_FLASH_CONFIG   ((esp32c61_rom_flash_config_t)0x40000170UL)
#define ESP32C61_ROM_FLASH_UNLOCK   ((int (*)(void))0x40000164UL)
#define ESP32C61_ROM_FLASH_ERASE    ((int (*)(uint32_t))0x40000154UL)  /* sector */
#define ESP32C61_ROM_FLASH_WRITE    ((esp32c61_rom_flash_write_t)0x4000015CUL)
#define ESP32C61_ROM_FLASH_READ     ((esp32c61_rom_flash_read_t)0x40000160UL)
#define ESP32C61_ROM_FLASH_USER_CMD ((int (*)(uint32_t *, uint8_t))0x40000174UL)

/* The cache in front of the flash and the MMU that maps it. */
#define ESP32C61_ROM_MMU_INIT       ((void (*)(void))0x400006B4UL)
#define ESP32C61_ROM_MMU_SET        ((esp32c61_rom_mmu_set_t)0x400006B8UL)
#define ESP32C61_ROM_CACHE_ENABLE   ((void (*)(uint32_t))0x40000694UL)
#define ESP32C61_ROM_CACHE_SUSPEND  ((uint32_t (*)(void))0x40000698UL)
#define ESP32C61_ROM_CACHE_RESUME   ((void (*)(uint32_t))0x4000069CUL)
#define ESP32C61_ROM_CACHE_INVAL    ((int (*)(uint32_t, uint32_t))0x40000634UL)

#define ESP32C61_RESET_POWERON      1U
#define ESP32C61_RESET_SW_SYS       3U
#define ESP32C61_RESET_DEEPSLEEP    5U
#define ESP32C61_RESET_TG0_WDT_SYS  7U
#define ESP32C61_RESET_TG1_WDT_SYS  8U
#define ESP32C61_RESET_RTC_WDT_SYS  9U
#define ESP32C61_RESET_TG0_WDT_CPU  11U
#define ESP32C61_RESET_SW_CPU       12U
#define ESP32C61_RESET_RTC_WDT_CPU  13U
#define ESP32C61_RESET_BROWNOUT     15U
#define ESP32C61_RESET_RTC_WDT_RTC  16U
#define ESP32C61_RESET_TG1_WDT_CPU  17U
#define ESP32C61_RESET_SUPER_WDT    18U
#define ESP32C61_RESET_USB_UART     21U     /* esptool through USB/UART */
#define ESP32C61_RESET_USB_JTAG     22U
#define ESP32C61_RESET_CPU_LOCKUP   26U

/*---------------------------------------------------------------------------*/
/* UART0 -- the console, wired to the CP2102N on the DevKitC                */
/*---------------------------------------------------------------------------*/

#define ESP32C61_UART0_BASE         0x60000000UL
#define ESP32C61_UART_FIFO(b)       ((b) + 0x00UL)
#define ESP32C61_UART_INT_RAW(b)    ((b) + 0x04UL)
#define ESP32C61_UART_INT_ENA(b)    ((b) + 0x0CUL)
#define ESP32C61_UART_INT_CLR(b)    ((b) + 0x10UL)
#define ESP32C61_UART_INT_RXFULL    (1UL << 0)  /* FIFO reached CONF1's level */
#define ESP32C61_UART_INT_RXOVF     (1UL << 4)
#define ESP32C61_UART_INT_RXTOUT    (1UL << 8)  /* line idle with bytes held */
#define ESP32C61_UART_STATUS(b)     ((b) + 0x1CUL)
#define ESP32C61_UART_RXCNT(s)      ((s) & 0xFFUL)
#define ESP32C61_UART_TXCNT(s)      (((s) >> 16) & 0xFFUL)
#define ESP32C61_UART_FIFO_DEPTH    128UL
#define ESP32C61_UART_CONF1(b)      ((b) + 0x24UL)
#define ESP32C61_UART_RXFULL_MSK    0xFFUL      /* RX threshold, bytes */
#define ESP32C61_UART_TOUT_CONF(b)  ((b) + 0x64UL)
#define ESP32C61_UART_TOUT_EN       (1UL << 0)
#define ESP32C61_UART_TOUT_POS      2U          /* idle time, bit-times */
#define ESP32C61_UART_TOUT_MSK      (0x3FFUL << 2)
/* The _SYNC registers above take effect only once this latch clears. */
#define ESP32C61_UART_REG_UPDATE(b) ((b) + 0x98UL)
#define ESP32C61_UART_UPDATE        (1UL << 0)

/*---------------------------------------------------------------------------*/
/* GPIO MATRIX AND IO_MUX                                                    */
/*---------------------------------------------------------------------------*/

#define ESP32C61_GPIO_BASE          0x60091000UL
#define ESP32C61_GPIO_OUT           (ESP32C61_GPIO_BASE + 0x04UL)
#define ESP32C61_GPIO_OUT_W1TS      (ESP32C61_GPIO_BASE + 0x08UL)
#define ESP32C61_GPIO_OUT_W1TC      (ESP32C61_GPIO_BASE + 0x0CUL)
#define ESP32C61_GPIO_ENABLE        (ESP32C61_GPIO_BASE + 0x34UL)
#define ESP32C61_GPIO_ENABLE_W1TS   (ESP32C61_GPIO_BASE + 0x38UL)
#define ESP32C61_GPIO_ENABLE_W1TC   (ESP32C61_GPIO_BASE + 0x3CUL)
#define ESP32C61_GPIO_IN            (ESP32C61_GPIO_BASE + 0x64UL)
#define ESP32C61_GPIO_STATUS_W1TC   (ESP32C61_GPIO_BASE + 0x7CUL)
#define ESP32C61_GPIO_PCPU_INT      (ESP32C61_GPIO_BASE + 0xA4UL)  /* armed+fired */
#define ESP32C61_GPIO_PIN(n)        (ESP32C61_GPIO_BASE + 0xD4UL + 4UL * (n))
#define ESP32C61_GPIO_PIN_TYPE_POS  7U      /* 1 rise, 2 fall, 3 any edge */
#define ESP32C61_GPIO_PIN_TYPE_MSK  (7UL << 7)
#define ESP32C61_GPIO_PIN_ENA_MSK   (0x1FUL << 13)
#define ESP32C61_GPIO_PIN_ENA_PCPU  (1UL << 13)
#define ESP32C61_GPIO_OUT_SEL(n)    (ESP32C61_GPIO_BASE + 0xAD4UL + 4UL * (n))
#define ESP32C61_GPIO_SIG_OUT       256UL   /* plain GPIO_OUT drives the pin */
#define ESP32C61_GPIO_COUNT         30U

#define ESP32C61_IO_MUX_BASE        0x60090000UL
#define ESP32C61_IO_MUX_GPIO(n)     (ESP32C61_IO_MUX_BASE + 4UL * (n))
#define ESP32C61_IO_MUX_FUN_PD      (1UL << 7)
#define ESP32C61_IO_MUX_FUN_PU      (1UL << 8)
#define ESP32C61_IO_MUX_FUN_IE      (1UL << 9)
#define ESP32C61_IO_MUX_DRV_POS     10U
#define ESP32C61_IO_MUX_DRV_MSK     (3UL << 10)
#define ESP32C61_IO_MUX_MCU_SEL_POS 12U
#define ESP32C61_IO_MUX_MCU_SEL_MSK (7UL << 12)
#define ESP32C61_IO_MUX_FUNC_GPIO   1UL

/*---------------------------------------------------------------------------*/
/* PCR -- the clock tree: root mux, CPU and AHB dividers, APB below AHB      */
/*---------------------------------------------------------------------------*/

#define ESP32C61_PCR_BASE           0x60096000UL
#define ESP32C61_PCR_SYSCLK_CONF    (ESP32C61_PCR_BASE + 0xE8UL)
#define ESP32C61_PCR_SOC_CLK_POS    16U
#define ESP32C61_PCR_SOC_CLK_MSK    (3UL << 16)
#define ESP32C61_PCR_SOC_CLK_XTAL   0UL
#define ESP32C61_PCR_SOC_CLK_RCFAST 1UL
#define ESP32C61_PCR_SOC_CLK_PLL160 2UL
#define ESP32C61_PCR_CPU_FREQ_CONF  (ESP32C61_PCR_BASE + 0xF0UL)
#define ESP32C61_PCR_AHB_FREQ_CONF  (ESP32C61_PCR_BASE + 0xF4UL)
#define ESP32C61_PCR_APB_FREQ_CONF  (ESP32C61_PCR_BASE + 0xF8UL)
#define ESP32C61_PCR_DIV_MSK        0xFFUL      /* divider - 1, bits 7:0 */
#define ESP32C61_PCR_APB_DIV_POS    8U          /* APB's sits at 15:8 */
#define ESP32C61_PCR_BUS_CLK_UPDATE (ESP32C61_PCR_BASE + 0x120UL)
#define ESP32C61_PCR_BUS_UPDATE     (1UL << 0)  /* latches the three above */

/*---------------------------------------------------------------------------*/
/* SYSTIMER -- 52-bit, from the 40 MHz crystal through a fixed divider       */
/*---------------------------------------------------------------------------*/

#define ESP32C61_SYSTIMER_BASE      0x6000A000UL
#define ESP32C61_SYSTIMER_CONF      (ESP32C61_SYSTIMER_BASE + 0x00UL)
#define ESP32C61_SYSTIMER_CLK_EN    (1UL << 31)
#define ESP32C61_SYSTIMER_UNIT0_EN  (1UL << 30)
#define ESP32C61_SYSTIMER_UNIT0_OP  (ESP32C61_SYSTIMER_BASE + 0x04UL)
#define ESP32C61_SYSTIMER_VALID     (1UL << 29)
#define ESP32C61_SYSTIMER_UPDATE    (1UL << 30)
#define ESP32C61_SYSTIMER_UNIT0_HI  (ESP32C61_SYSTIMER_BASE + 0x40UL)
#define ESP32C61_SYSTIMER_UNIT0_LO  (ESP32C61_SYSTIMER_BASE + 0x44UL)
#define ESP32C61_SYSTIMER_HZ        16000000UL

/* Three alarms (comparators), each against unit 0 unless told otherwise. */
#define ESP32C61_SYSTIMER_ALARM_EN(n)  (1UL << (24U - (n)))   /* in CONF */
#define ESP32C61_SYSTIMER_TARGET_HI(n) (ESP32C61_SYSTIMER_BASE + 0x1CUL + 8UL * (n))
#define ESP32C61_SYSTIMER_TARGET_LO(n) (ESP32C61_SYSTIMER_BASE + 0x20UL + 8UL * (n))
#define ESP32C61_SYSTIMER_TARGET_CONF(n) (ESP32C61_SYSTIMER_BASE + 0x34UL + 4UL * (n))
#define ESP32C61_SYSTIMER_PERIOD_MSK   0x03FFFFFFUL
#define ESP32C61_SYSTIMER_PERIOD_MODE  (1UL << 30)
#define ESP32C61_SYSTIMER_COMP_LOAD(n) (ESP32C61_SYSTIMER_BASE + 0x50UL + 4UL * (n))
#define ESP32C61_SYSTIMER_INT_ENA   (ESP32C61_SYSTIMER_BASE + 0x64UL)
#define ESP32C61_SYSTIMER_INT_RAW   (ESP32C61_SYSTIMER_BASE + 0x68UL)
#define ESP32C61_SYSTIMER_INT_CLR   (ESP32C61_SYSTIMER_BASE + 0x6CUL)
#define ESP32C61_SYSTIMER_INT_ST    (ESP32C61_SYSTIMER_BASE + 0x70UL)
#define ESP32C61_SYSTIMER_INT(n)    (1UL << (n))

/*---------------------------------------------------------------------------*/
/* INTERRUPTS -- the matrix routes sources to CPU lines, the CLIC takes them  */
/*---------------------------------------------------------------------------*/

/* One map register per source, holding the CLIC id it raises; 0 is none. */
#define ESP32C61_INTMTX_BASE        0x60010000UL
#define ESP32C61_INTMTX_MAP(src)    (ESP32C61_INTMTX_BASE + 4UL * (src))
#define ESP32C61_INTMTX_SOURCES     66U

#define ESP32C61_SRC_GPIO           27U
#define ESP32C61_SRC_UART0          40U
#define ESP32C61_SRC_USB_JTAG       44U
#define ESP32C61_SRC_SYSTIMER(n)    (52U + (n))

/* CPU line n is CLIC id n + 16; the ids below 16 are the core's own. */
#define ESP32C61_CLIC_BASE          0x20800000UL
#define ESP32C61_CLIC_CONFIG        (ESP32C61_CLIC_BASE + 0x0UL)
#define ESP32C61_CLIC_MNLBITS_MSK   0xFUL
#define ESP32C61_CLIC_INFO          (ESP32C61_CLIC_BASE + 0x4UL)
#define ESP32C61_CLIC_CTRL(id)      (ESP32C61_CLIC_BASE + 0x1000UL + 4UL * (id))
#define ESP32C61_CLIC_IP            (1UL << 0)
#define ESP32C61_CLIC_IE            (1UL << 8)
#define ESP32C61_CLIC_SHV           (1UL << 16)
#define ESP32C61_CLIC_TRIG_EDGE     (1UL << 17)
#define ESP32C61_CLIC_TRIG_LOW      (1UL << 18)
#define ESP32C61_CLIC_MODE_M        (3UL << 22)
#define ESP32C61_CLIC_CTL_POS       24U
#define ESP32C61_CLIC_CTL_MSK       (0xFFUL << 24)
#define ESP32C61_CLIC_NLBITS        3U          /* levels 0..7 */
#define ESP32C61_CLIC_LINE_ID(n)    ((n) + 16U)
#define ESP32C61_CLIC_LINES         32U

/*---------------------------------------------------------------------------*/
/* WATCHDOGS -- two TIMG MWDTs, the RTC WDT and the super watchdog           */
/*---------------------------------------------------------------------------*/

#define ESP32C61_WDT_WKEY           0x50D83AA1UL

#define ESP32C61_TIMG0_BASE         0x60008000UL
#define ESP32C61_TIMG1_BASE         0x60009000UL
#define ESP32C61_TIMG_WDTCONFIG0(b) ((b) + 0x48UL)
#define ESP32C61_TIMG_WDTFEED(b)    ((b) + 0x60UL)
#define ESP32C61_TIMG_WDTWPROTECT(b) ((b) + 0x64UL)
#define ESP32C61_TIMG_WDTCONFIG1(b) ((b) + 0x4CUL)
#define ESP32C61_TIMG_WDTCONFIG2(b) ((b) + 0x50UL)      /* stage 0 hold */
#define ESP32C61_TIMG_WDT_EN        (1UL << 31)
#define ESP32C61_TIMG_WDT_STG0_POS  29U
#define ESP32C61_TIMG_WDT_STG_MSK   (0xFFUL << 23)      /* all four stages */
#define ESP32C61_TIMG_WDT_RESET_SYS 3UL
#define ESP32C61_TIMG_WDT_UPDATE    (1UL << 22)
#define ESP32C61_TIMG_WDT_FLASHBOOT (1UL << 14)
#define ESP32C61_TIMG_WDT_PRESCALE_POS 16U              /* in CONFIG1 */
#define ESP32C61_TIMG_WDT_DIVCNT_RST (1UL << 0)

/* The TIMG0 bus clock, and the watchdog's own function clock and source. */
#define ESP32C61_PCR_TG0_CONF       (ESP32C61_PCR_BASE + 0x40UL)
#define ESP32C61_PCR_TG0_CLK_EN     (1UL << 0)
#define ESP32C61_PCR_TG0_WDT_CLK    (ESP32C61_PCR_BASE + 0x48UL)
#define ESP32C61_PCR_TG0_WDT_SEL_MSK (3UL << 20)        /* 0 is the crystal */
#define ESP32C61_PCR_TG0_WDT_EN     (1UL << 22)

#define ESP32C61_LP_WDT_BASE        0x600B1C00UL
#define ESP32C61_RWDT_CONFIG0       (ESP32C61_LP_WDT_BASE + 0x00UL)
#define ESP32C61_RWDT_WPROTECT      (ESP32C61_LP_WDT_BASE + 0x18UL)
#define ESP32C61_SWD_CONFIG         (ESP32C61_LP_WDT_BASE + 0x1CUL)
#define ESP32C61_SWD_WPROTECT       (ESP32C61_LP_WDT_BASE + 0x20UL)
#define ESP32C61_RWDT_EN            (1UL << 31)
#define ESP32C61_RWDT_FLASHBOOT     (1UL << 12)
#define ESP32C61_SWD_DISABLE        (1UL << 30)
#define ESP32C61_SWD_AUTO_FEED      (1UL << 18)

/*---------------------------------------------------------------------------*/
/* LP RNG -- a generator that noise samples feed once sampling is enabled    */
/*---------------------------------------------------------------------------*/

#define ESP32C61_LPPERI_BASE        0x600B2800UL
#define ESP32C61_LPPERI_CLK_EN      (ESP32C61_LPPERI_BASE + 0x00UL)
#define ESP32C61_LPPERI_RNG_CLK     (1UL << 24)
#define ESP32C61_RNG_CFG            (ESP32C61_LPPERI_BASE + 0x24UL)
#define ESP32C61_RNG_SAMPLE_EN      (1UL << 0)
#define ESP32C61_RNG_CNT_POS        24U         /* samples taken, 8-bit, wraps */
#define ESP32C61_RNG_DATA           (ESP32C61_LPPERI_BASE + 0x28UL)  /* synced */

/*---------------------------------------------------------------------------*/
/* EFUSE -- the factory MAC is the part's identity                           */
/*---------------------------------------------------------------------------*/

#define ESP32C61_EFUSE_BASE         0x600B4800UL
#define ESP32C61_EFUSE_MAC0         (ESP32C61_EFUSE_BASE + 0x44UL)
#define ESP32C61_EFUSE_MAC1         (ESP32C61_EFUSE_BASE + 0x48UL)

#endif /* TIKU_ESP32C61_REGS_H_ */
