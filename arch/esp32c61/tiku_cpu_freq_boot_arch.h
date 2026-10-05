/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - ESP32-C61 boot: watchdogs and the core clock.
 *
 * The clock is measured, not assumed: mcycle against SYSTIMER, which runs
 * from the crystal whatever the core is clocked from.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_ESP32C61_CPU_FREQ_BOOT_ARCH_H_

#include <stdint.h>

/* Core rate assumed only when SYSTIMER cannot be read to measure it. */
#ifndef TIKU_ESP32C61_CPU_HZ
#define TIKU_ESP32C61_CPU_HZ        40000000UL
#endif

/* Which watchdogs were running when the image got control. */
#define TIKU_ESP32C61_WDT_MWDT0     0x01U
#define TIKU_ESP32C61_WDT_MWDT1     0x02U
#define TIKU_ESP32C61_WDT_RWDT      0x04U
#define TIKU_ESP32C61_WDT_SWD       0x08U

/**
 * @brief Boot bring-up: stop the watchdogs, unroute interrupts, time the core.
 *
 * Once, before anything attaches a line: a second call detaches them all.
 */
void tiku_cpu_boot_esp32c61_init(void);

/** @brief Idle until an interrupt: the scheduler's idle hook. */
void tiku_cpu_boot_esp32c61_power_wfi_enter(void);

/** @brief Measured core clock in Hz (the fallback until measured). */
unsigned long tiku_cpu_esp32c61_clock_get_hz(void);

/** @brief Peripheral (APB) clock in Hz, for the kernel's SMCLK figure. */
unsigned long tiku_cpu_esp32c61_smclk_get_hz(void);

/** @brief 1 when the clock could not be measured and is assumed. */
int tiku_cpu_esp32c61_clock_has_fault(void);

/** @brief TIKU_ESP32C61_WDT_* bits for the watchdogs found armed at boot. */
uint8_t tiku_cpu_esp32c61_wdt_found(void);

/** @brief SYSTIMER unit 0 (52 bits, 16 MHz); 0 when it would not latch. */
uint64_t tiku_cpu_esp32c61_systimer(void);

/*---------------------------------------------------------------------------*/
/* Core frequency                                                            */
/*---------------------------------------------------------------------------*/

/** @brief The clock tree as the PCR holds it, for `freq probe`. */
typedef struct {
    uint8_t       root;         /**< 0 XTAL, 1 RC_FAST, 2 PLL 160 MHz */
    uint8_t       cpu_div;      /**< CPU = root / cpu_div */
    uint8_t       ahb_div;      /**< AHB = root / ahb_div */
    uint8_t       apb_div;      /**< APB = AHB / apb_div */
    unsigned long cpu_hz;       /**< implied by the tree */
    unsigned long ahb_hz;
    unsigned long apb_hz;
} tiku_esp32c61_clock_t;

/** @brief Read the clock tree into @p out. */
void tiku_cpu_esp32c61_clock_probe(tiku_esp32c61_clock_t *out);

/**
 * @brief Move the core onto the crystal for a sleep that stops the PLL, and
 *        back: park returns the tree as it was, unpark restores it, and the
 *        rate measured at the last change stands -- no 2 ms re-measurement.
 */
uint32_t tiku_cpu_esp32c61_clock_park(void);
void tiku_cpu_esp32c61_clock_unpark(uint32_t saved);

/** @brief 1 when @p mhz is a core rate the tree makes exactly. */
int tiku_cpu_freq_esp32c61_supported(unsigned int mhz);

/** @brief The lowest rate offered: the radios hang on the crystal, so a
 *         build with one offers the PLL's rates alone. */
#if (TIKU_DRV_WIFI_ESP_ENABLE + 0) || (TIKU_DRV_BLE_ESP_ENABLE + 0)
#define TIKU_ESP32C61_OFFER_MIN_MHZ 80U
#else
#define TIKU_ESP32C61_OFFER_MIN_MHZ 10U
#endif

/**
 * @brief Move the core to @p mhz and re-measure it.
 *
 * 160 and 80 come from the PLL with AHB held at 40 MHz; 40, 20 and 10
 * from the crystal.  The console and SYSTIMER run from the crystal.
 *
 * @return 0, or -1 for an unsupported rate (nothing is changed)
 */
int tiku_cpu_freq_esp32c61_set(unsigned int mhz);

#endif /* TIKU_ESP32C61_CPU_FREQ_BOOT_ARCH_H_ */
