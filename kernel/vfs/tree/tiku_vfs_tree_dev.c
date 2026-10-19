/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_dev.c - /dev subtree (files and assembly).
 *
 * The static /dev nodes: board LEDs, console, null, zero, and the small
 * subtrees for the console port, adc, i2c and spi.  LED nodes keep an SRAM
 * shadow because an output pin cannot be read back uniformly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_dev.h"
#include "tiku_vfs_tree_gpio.h"
#include "tiku_vfs_tree_sensor.h"
#include "tiku.h"
#include <kernel/cpu/tiku_common.h>
#include <kernel/timers/tiku_clock.h>   /* TIKU_CLOCK_SECOND */
#include <interfaces/led/tiku_led.h>
#include <interfaces/adc/tiku_adc.h>
#include <interfaces/bus/tiku_i2c_bus.h>
#include <interfaces/bus/tiku_spi_bus.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* LED STATE                                                                 */
/*---------------------------------------------------------------------------*/

#if TIKU_BOARD_LED_COUNT > 0

/*
 * Shadow of each LED's logical state (1 = lit), maintained by the write
 * handlers and cleared in tiku_vfs_tree_dev_init().  Reads serve the mirror
 * because board wiring (active-high vs active-low) makes a raw output-register
 * read ambiguous; direct tiku_led_*() calls therefore go unnoticed here.
 */
static uint8_t led_state[TIKU_BOARD_LED_COUNT];

/**
 * @brief Generate the read/write handler pair for LED index N.
 *
 * Read renders the shadow state as "0\n" or "1\n".  Write decodes the first
 * payload byte -- '1' on, '0' off, 't' toggle, anything else ignored -- and
 * updates hardware and shadow together.
 */
#define LED_VFS_FUNCS(N)                                                      \
static int                                                                    \
led##N##_read(char *buf, size_t max)                                          \
{                                                                             \
    return snprintf(buf, max, "%u\n", led_state[N]);                          \
}                                                                             \
                                                                              \
static int                                                                    \
led##N##_write(const char *buf, size_t len)                                   \
{                                                                             \
    (void)len;                                                                \
    if (buf[0] == '1') {                                                      \
        tiku_led_on(N);                                                       \
        led_state[N] = 1;                                                     \
    } else if (buf[0] == '0') {                                               \
        tiku_led_off(N);                                                      \
        led_state[N] = 0;                                                     \
    } else if (buf[0] == 't') {                                               \
        tiku_led_toggle(N);                                                   \
        led_state[N] = !led_state[N];                                         \
    }                                                                         \
    return 0;                                                                 \
}

LED_VFS_FUNCS(0)
#if TIKU_BOARD_LED_COUNT >= 2
LED_VFS_FUNCS(1)
#endif
#if TIKU_BOARD_LED_COUNT >= 3
LED_VFS_FUNCS(2)
#endif
#if TIKU_BOARD_LED_COUNT >= 4
LED_VFS_FUNCS(3)
#endif
#endif /* TIKU_BOARD_LED_COUNT > 0 */

/*---------------------------------------------------------------------------*/
/* CONSOLE PORT                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /dev/uart/overruns, served as
 *        /dev/usb_serial_jtag/tx_dropped on a TIKU_CONSOLE_JTAG build.
 *
 * Renders the RX bytes the UART dropped on a full ring buffer, a count that
 * rises when the consumer drains slower than the line delivers, or on a
 * TIKU_CONSOLE_JTAG build the console bytes the transmit transport rejected.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style)
 */
static int
uart_overruns_read(char *buf, size_t max)
{
#if defined(TIKU_CONSOLE_JTAG)
    return snprintf(buf, max, "%lu\n", (unsigned long)tiku_debug_arch_dropped());
#else
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_uart_overrun_count());
#endif
}

#if defined(PLATFORM_NORDIC)
/**
 * @brief Read handler for /dev/uart/recoveries (nRF54L only).
 *
 * Renders the RX-engine recoveries since boot: a lost DMA re-arm silences RX,
 * and the driver, finding a byte captured but never moved, re-arms in place.
 */
static int
uart_recoveries_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_uart_rx_recovery_count());
}
#endif

/**
 * @brief Read handler for /dev/uart/baud, served as
 *        /dev/usb_serial_jtag/transport on a TIKU_CONSOLE_JTAG build.
 *
 * Renders the board header's configured baud rate, TIKU_BOARD_UART_BAUD, as
 * a decimal line ("9600\n" on MSP430 boards, "115200\n" on RP2350), or
 * "usb-serial-jtag\n" on a TIKU_CONSOLE_JTAG build.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style)
 */
static int
uart_baud_read(char *buf, size_t max)
{
#if defined(TIKU_CONSOLE_JTAG)
    return snprintf(buf, max, "usb-serial-jtag\n");
#else
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)TIKU_BOARD_UART_BAUD);
#endif
}

/*---------------------------------------------------------------------------*/
/* SPI CONFIGURATION                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /dev/spi/config.
 *
 * Renders the active configuration as "mode=0 order=msb pre=8\n", or "off\n"
 * while the bus is closed.  Read-only: bus setup belongs to the owning driver.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style)
 */
#if !defined(PLATFORM_ESP32P4)
static int
spi_config_read(char *buf, size_t max)
{
    const tiku_spi_config_t *cfg = tiku_spi_get_config();
    if (cfg == NULL) {
        return snprintf(buf, max, "off\n");
    }
    return snprintf(buf, max, "mode=%u order=%s pre=%u\n",
                    (unsigned)cfg->mode,
                    cfg->bit_order == 0 ? "msb" : "lsb",
                    (unsigned)cfg->prescaler);
}

/*---------------------------------------------------------------------------*/
/* ADC                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /dev/adc/temp.
 *
 * Triggers a conversion on the internal temperature channel and renders the
 * raw ADC count ("2789\n"), or "err\n" on failure.  Degrees are left to the
 * consumer, which needs the per-device calibration constants (TLV on MSP430).
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style)
 */
static int
adc_temp_read(char *buf, size_t max)
{
    uint16_t val;
    int rc = tiku_adc_read(TIKU_ADC_CH_TEMP, &val);
    if (rc != TIKU_ADC_OK) {
        return snprintf(buf, max, "err\n");
    }
    return snprintf(buf, max, "%u\n", (unsigned)val);
}

/**
 * @brief Read handler for /dev/adc/battery.
 *
 * Same contract as adc_temp_read() but on the supply-voltage
 * channel (internally divided VCC on MSP430): raw count or
 * "err\n".
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The full length of the text (snprintf-style)
 */
static int
adc_battery_read(char *buf, size_t max)
{
    uint16_t val;
    int rc = tiku_adc_read(TIKU_ADC_CH_BATTERY, &val);
    if (rc != TIKU_ADC_OK) {
        return snprintf(buf, max, "err\n");
    }
    return snprintf(buf, max, "%u\n", (unsigned)val);
}
#endif

/*---------------------------------------------------------------------------*/
/* I2C SCAN                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /dev/i2c/scan; each read probes the bus.
 *
 * Probes 7-bit addresses 0x08..0x77 with a zero-length write, up to 112 bus
 * transactions, stopping once @p buf has under 7 bytes left; renders the
 * responders as "0x18 0x48\n", or "none\n".
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return The length of the text kept in @p buf, not its full length: a scan
 *         cut short by a small @p buf reports only the addresses it reached
 */
#if !defined(PLATFORM_ESP32C5) && !defined(PLATFORM_ESP32P4)
static int
i2c_scan_read(char *buf, size_t max)
{
    int pos = 0;
    uint8_t addr;
    uint8_t found = 0;
    uint8_t dummy = 0;

    for (addr = 0x08; addr <= 0x77 && pos < (int)max - 6; addr++) {
        if (tiku_i2c_write(addr, &dummy, 0) == TIKU_I2C_OK) {
            pos += snprintf(buf + pos, max - pos, "0x%02x ", addr);
            found++;
        }
    }

    if (found == 0) {
        pos += snprintf(buf + pos, max - pos, "none");
    }

    if (pos < (int)max - 1) {
        buf[pos++] = '\n';
        buf[pos] = '\0';
    }

    return pos;
}
#endif

/*---------------------------------------------------------------------------*/
/* CONSOLE                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /dev/console: drain pending console input.
 *
 * Non-blocking: copies the raw bytes already waiting on the UART, or on the
 * USB-serial-JTAG on a TIKU_CONSOLE_JTAG build, and returns their count.  The
 * shell reads the same input, so a read during a session takes its bytes.
 *
 * @param buf  Output buffer for the drained bytes
 * @param max  Capacity of @p buf in bytes
 * @return Number of bytes drained (0 when idle)
 */
static int
console_read(char *buf, size_t max)
{
    size_t n = 0;
#if defined(TIKU_CONSOLE_JTAG)
    while (n < max && tiku_debug_arch_rx_ready()) {
        int c = tiku_debug_arch_getc();
#else
    while (n < max && tiku_uart_rx_ready()) {
        int c = tiku_uart_getc();
#endif
        if (c < 0) {
            break;
        }
        buf[n++] = (char)c;
    }
    return (int)n;
}

/**
 * @brief Write handler for /dev/console: send bytes to the console.
 *
 * Sends the payload byte by byte through tiku_uart_putc(), or
 * tiku_debug_arch_putc() on a TIKU_CONSOLE_JTAG build, with no CRLF
 * translation: a caller that wants "\r\n" line endings sends them.
 *
 * @param buf  Bytes to transmit
 * @param len  Number of bytes to transmit
 * @return 0 always
 */
static int
console_write(const char *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
#if defined(TIKU_CONSOLE_JTAG)
        tiku_debug_arch_putc(buf[i]);
#else
        tiku_uart_putc(buf[i]);
#endif
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* NULL DEVICE                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /dev/null — always empty.
 *
 * Returns 0 bytes.
 *
 * @param buf  Unused
 * @param max  Unused
 * @return 0 always
 */
static int
devnull_read(char *buf, size_t max)
{
    (void)buf;
    (void)max;
    return 0;
}

/**
 * @brief Write handler for /dev/null — discard everything.
 *
 * Accepts and ignores any payload.
 *
 * @param buf  Ignored
 * @param len  Ignored
 * @return 0 always
 */
static int
devnull_write(const char *buf, size_t len)
{
    (void)buf;
    (void)len;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* ZERO DEVICE                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /dev/zero — fill with NUL bytes.
 *
 * Fills the buffer with zeros and returns @p max; printed as a string, the
 * result is empty.
 *
 * @param buf  Output buffer, fully zeroed on return
 * @param max  Capacity of @p buf in bytes
 * @return @p max
 */
static int
devzero_read(char *buf, size_t max)
{
    size_t i;
    for (i = 0; i < max; i++) {
        buf[i] = '\0';
    }
    return (int)max;
}

/*---------------------------------------------------------------------------*/
/* NODE TABLES                                                               */
/*---------------------------------------------------------------------------*/

/* Type descriptors for the typed /dev nodes below.  A bus scan
 * addresses every device on the bus and a console read takes input, so
 * neither is a passive read. */
#if !defined(PLATFORM_ESP32C5) && !defined(PLATFORM_ESP32P4)
static const tiku_vfs_desc_t desc_scan = TIKU_VFS_DESC_FLAGS(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_LIVE, TIKU_VFS_E_BUS,
    TIKU_VFS_DF_READ_EFFECT);
#endif
static const tiku_vfs_desc_t desc_console = TIKU_VFS_DESC_FLAGS(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_LIVE, TIKU_VFS_E_CHEAP,
    TIKU_VFS_DF_READ_CONSUMES);
static const tiku_vfs_desc_t desc_bus_config = TIKU_VFS_DESC(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t desc_uart_count = TIKU_VFS_DESC(
    TIKU_VFS_T_U32, TIKU_VFS_U_COUNT, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
/* 12-bit raw conversions, sampled live (each read wakes the ADC), so both
 * carry a read-coalescing window in which repeated reads share one
 * conversion: TIKU_CLOCK_SECOND / 10 for temperature, TIKU_CLOCK_SECOND for
 * the supply voltage. */
#if !defined(PLATFORM_ESP32P4)
static const tiku_vfs_desc_t desc_adc_temp =
    TIKU_VFS_DESC_RF(TIKU_VFS_T_U32, TIKU_VFS_U_ADC_RAW,
                     TIKU_VFS_FRESH_LIVE, TIKU_VFS_E_PERIPH, 0, 4095,
                     TIKU_CLOCK_SECOND / 10);
static const tiku_vfs_desc_t desc_adc_batt =
    TIKU_VFS_DESC_RF(TIKU_VFS_T_U32, TIKU_VFS_U_ADC_RAW,
                     TIKU_VFS_FRESH_LIVE, TIKU_VFS_E_PERIPH, 0, 4095,
                     TIKU_CLOCK_SECOND);
#endif
#if TIKU_BOARD_LED_COUNT >= 1
static const tiku_vfs_desc_t desc_led =
    TIKU_VFS_DESC(TIKU_VFS_T_BOOL, TIKU_VFS_U_BOOL,
                  TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
#endif

/**
 * /dev/uart directory table, /dev/usb_serial_jtag on a TIKU_CONSOLE_JTAG
 * build: the console's drop count and its baud rate or transport.
 */
static const tiku_vfs_node_t dev_uart_children[] = {
#if defined(TIKU_CONSOLE_JTAG)
    { "tx_dropped", TIKU_VFS_FILE, uart_overruns_read, NULL, NULL, 0, &desc_uart_count },
    { "transport", TIKU_VFS_FILE, uart_baud_read, NULL, NULL, 0, &desc_bus_config },
#else
    { "overruns", TIKU_VFS_FILE, uart_overruns_read, NULL, NULL, 0,
      &desc_uart_count },
    { "baud",     TIKU_VFS_FILE, uart_baud_read,     NULL, NULL, 0,
      &desc_bus_config },
#if defined(PLATFORM_NORDIC)
    { "recoveries", TIKU_VFS_FILE, uart_recoveries_read, NULL, NULL, 0,
      &desc_uart_count },
#endif
#endif
};
#define DEV_UART_NCHILD \
    (sizeof dev_uart_children / sizeof dev_uart_children[0])

/** /dev/adc directory table — raw conversions, no calibration */
#if !defined(PLATFORM_ESP32P4)
static const tiku_vfs_node_t dev_adc_children[] = {
    { "temp",    TIKU_VFS_FILE, adc_temp_read,    NULL, NULL, 0, &desc_adc_temp },
    { "battery", TIKU_VFS_FILE, adc_battery_read, NULL, NULL, 0, &desc_adc_batt },
};
#endif

/** /dev/i2c directory table — the live bus scanner */
#if !defined(PLATFORM_ESP32C5) && !defined(PLATFORM_ESP32P4)
static const tiku_vfs_node_t dev_i2c_children[] = {
    { "scan", TIKU_VFS_FILE, i2c_scan_read, NULL, NULL, 0, &desc_scan },
};
#endif

/** /dev/spi directory table — read-only configuration view */
#if !defined(PLATFORM_ESP32P4)
static const tiku_vfs_node_t dev_spi_children[] = {
    { "config", TIKU_VFS_FILE, spi_config_read, NULL, NULL, 0,
      &desc_bus_config },
};
#endif

/*
 * The static /dev directory table; the driver registry mounts its
 * /dev/<class> directories after these.  LED entries are gated one by one on
 * TIKU_BOARD_LED_COUNT so a one-LED board exposes exactly /dev/led0; the gpio
 * subtrees come from tiku_vfs_tree_gpio.c and sensors/ from
 * tiku_vfs_tree_sensor.c.  To add a node, implement the handler above and
 * append the entry here.
 */
static const tiku_vfs_node_t dev_children[] = {
    { "sensors", TIKU_VFS_DIR, NULL, NULL,
      tiku_vfs_tree_sensor_children, TIKU_VFS_TREE_SENSOR_NCHILD },
#if TIKU_BOARD_LED_COUNT >= 1
    { "led0",     TIKU_VFS_FILE, led0_read, led0_write, NULL, 0, &desc_led, NULL, TIKU_VFS_CAP_HW },
#endif
#if TIKU_BOARD_LED_COUNT >= 2
    { "led1",     TIKU_VFS_FILE, led1_read, led1_write, NULL, 0, &desc_led, NULL, TIKU_VFS_CAP_HW },
#endif
#if TIKU_BOARD_LED_COUNT >= 3
    { "led2",     TIKU_VFS_FILE, led2_read, led2_write, NULL, 0, &desc_led, NULL, TIKU_VFS_CAP_HW },
#endif
#if TIKU_BOARD_LED_COUNT >= 4
    { "led3",     TIKU_VFS_FILE, led3_read, led3_write, NULL, 0, &desc_led, NULL, TIKU_VFS_CAP_HW },
#endif
    { "console",  TIKU_VFS_FILE, console_read, console_write, NULL, 0,
      &desc_console },
    { "null",     TIKU_VFS_FILE, devnull_read, devnull_write, NULL, 0 },
    { "zero",     TIKU_VFS_FILE, devzero_read, NULL, NULL, 0 },
    { "gpio",     TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_gpio_children,     TIKU_VFS_TREE_GPIO_NPORTS },
    { "gpio_dir", TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_gpio_dir_children, TIKU_VFS_TREE_GPIO_NPORTS },
    { "gpio_owner", TIKU_VFS_DIR, NULL, NULL,
      tiku_vfs_tree_gpio_owner_children, TIKU_VFS_TREE_GPIO_NPORTS },
#if defined(TIKU_CONSOLE_JTAG)
    { "usb_serial_jtag", TIKU_VFS_DIR, NULL, NULL, dev_uart_children,
#else
    { "uart",     TIKU_VFS_DIR,  NULL, NULL, dev_uart_children,
#endif
      DEV_UART_NCHILD },
#if !defined(PLATFORM_ESP32P4)
    { "adc",      TIKU_VFS_DIR,  NULL, NULL, dev_adc_children, 2 },
    { "spi",      TIKU_VFS_DIR,  NULL, NULL, dev_spi_children, 1 },
#endif
#if !defined(PLATFORM_ESP32C5) && !defined(PLATFORM_ESP32P4)
    { "i2c",      TIKU_VFS_DIR,  NULL, NULL, dev_i2c_children, 1 },
#endif
};

/**
 * The /dev directory node, with a sizeof-derived child count;
 * tiku_vfs_tree_dev_get() returns it for the root assembly to copy.
 */
static const tiku_vfs_node_t dev_node = {
    "dev", TIKU_VFS_DIR, NULL, NULL, dev_children,
    sizeof(dev_children) / sizeof(dev_children[0])
};

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

const tiku_vfs_node_t *
tiku_vfs_tree_dev_get(void)
{
    return &dev_node;
}

void
tiku_vfs_tree_dev_init(void)
{
    tiku_led_init_all();

#if TIKU_BOARD_LED_COUNT > 0
    {
        uint8_t i;
        for (i = 0; i < TIKU_BOARD_LED_COUNT; i++) {
            led_state[i] = 0;
        }
    }
#endif
}
