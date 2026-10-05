/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_dev.c - /dev subtree (files and assembly).
 *
 * Hardware-facing nodes: board LEDs, console, null, zero, and the small static
 * subtrees for uart, adc, i2c and spi.  LED nodes keep an SRAM shadow because
 * an output pin cannot be read back uniformly; the rest query their drivers.
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
#if TIKU_SPI_ENABLE
#include <interfaces/bus/tiku_spi_bus.h>
#endif
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* LED STATE TRACKING (indexed by TIKU_BOARD_LED_COUNT)                      */
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
/* /dev/uart/overruns                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /dev/uart/overruns.
 *
 * Renders the count of RX bytes dropped because the ring buffer was full.  A
 * rising count means the consumer drains slower than the line delivers.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
uart_overruns_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_uart_overrun_count());
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
 * @brief Read handler for /dev/uart/baud.
 *
 * Renders the board header's configured baud rate as a decimal line ("9600\n"
 * on MSP430 boards, "115200\n" on RP2350), a build-time constant.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
uart_baud_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)TIKU_BOARD_UART_BAUD);
}

/*---------------------------------------------------------------------------*/
/* /dev/spi/config                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /dev/spi/config.
 *
 * Renders the active configuration as "mode=0 order=msb pre=8\n", or "off\n"
 * when the driver is compiled in but unconfigured, or "n/a\n" in a
 * TIKU_SPI_ENABLE=0 build.  Read-only: bus setup belongs to the owning driver.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
spi_config_read(char *buf, size_t max)
{
#if TIKU_SPI_ENABLE
    const tiku_spi_config_t *cfg = tiku_spi_get_config();
    if (cfg == NULL) {
        return snprintf(buf, max, "off\n");
    }
    return snprintf(buf, max, "mode=%u order=%s pre=%u\n",
                    (unsigned)cfg->mode,
                    cfg->bit_order == 0 ? "msb" : "lsb",
                    (unsigned)cfg->prescaler);
#else
    return snprintf(buf, max, "n/a\n");
#endif
}

/*---------------------------------------------------------------------------*/
/* /dev/adc/temp, /dev/adc/battery                                           */
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
 * @return Bytes written, or -1 on error
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
 * @return Bytes written, or -1 on error
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

/*---------------------------------------------------------------------------*/
/* /dev/i2c/scan                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /dev/i2c/scan; each read probes the bus.
 *
 * Reading probes 7-bit addresses 0x08..0x77 with a zero-length write and
 * renders the responders as "0x18 0x48\n", or "none\n".  Each read costs 112
 * bus transactions.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written
 */
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

/*---------------------------------------------------------------------------*/
/* /dev/console — system console (UART)                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /dev/console — drain pending UART RX.
 *
 * Non-blocking: copies only the bytes already waiting and returns their count,
 * 0 when idle.  Raw bytes with no newline appended.
 * The shell consumes the same UART, so reading it mid-session steals bytes.
 *
 * @param buf  Output buffer for the drained bytes
 * @param max  Capacity of @p buf in bytes
 * @return Number of bytes drained (0 when idle)
 */
static int
console_read(char *buf, size_t max)
{
    size_t n = 0;
    while (n < max && tiku_uart_rx_ready()) {
        int c = tiku_uart_getc();
        if (c < 0) {
            break;
        }
        buf[n++] = (char)c;
    }
    return (int)n;
}

/**
 * @brief Write handler for /dev/console — send bytes to the UART.
 *
 * Pushes the payload through tiku_uart_putc() byte by byte,
 * blocking on TX-ready for each.  No CRLF translation — callers
 * that want "\r\n" line endings must send them.
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
        tiku_uart_putc(buf[i]);
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* /dev/null — data sink                                                     */
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
/* /dev/zero — zero source                                                   */
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
static const tiku_vfs_desc_t desc_scan = TIKU_VFS_DESC_FLAGS(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_LIVE, TIKU_VFS_E_BUS,
    TIKU_VFS_DF_READ_EFFECT);
static const tiku_vfs_desc_t desc_console = TIKU_VFS_DESC_FLAGS(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_LIVE, TIKU_VFS_E_CHEAP,
    TIKU_VFS_DF_READ_CONSUMES);
static const tiku_vfs_desc_t desc_bus_config = TIKU_VFS_DESC(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t desc_uart_count = TIKU_VFS_DESC(
    TIKU_VFS_T_U32, TIKU_VFS_U_COUNT, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
/* 12-bit raw conversions, sampled live (each read wakes the ADC), so
 * both carry a read-coalescing window: repeated reads inside the window
 * share one conversion.  Temperature drifts slowly -> ~100 ms; supply
 * voltage slower still -> ~1 s. */
static const tiku_vfs_desc_t desc_adc_temp =
    TIKU_VFS_DESC_RF(TIKU_VFS_T_U32, TIKU_VFS_U_ADC_RAW,
                     TIKU_VFS_FRESH_LIVE, TIKU_VFS_E_PERIPH, 0, 4095,
                     TIKU_CLOCK_SECOND / 10);
static const tiku_vfs_desc_t desc_adc_batt =
    TIKU_VFS_DESC_RF(TIKU_VFS_T_U32, TIKU_VFS_U_ADC_RAW,
                     TIKU_VFS_FRESH_LIVE, TIKU_VFS_E_PERIPH, 0, 4095,
                     TIKU_CLOCK_SECOND);
#if TIKU_BOARD_LED_COUNT >= 1
static const tiku_vfs_desc_t desc_led =
    TIKU_VFS_DESC(TIKU_VFS_T_BOOL, TIKU_VFS_U_BOOL,
                  TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
#endif

/** /dev/uart directory table — RX health + configured baud */
static const tiku_vfs_node_t dev_uart_children[] = {
    { "overruns", TIKU_VFS_FILE, uart_overruns_read, NULL, NULL, 0,
      &desc_uart_count },
    { "baud",     TIKU_VFS_FILE, uart_baud_read,     NULL, NULL, 0,
      &desc_bus_config },
#if defined(PLATFORM_NORDIC)
    { "recoveries", TIKU_VFS_FILE, uart_recoveries_read, NULL, NULL, 0,
      &desc_uart_count },
#endif
};
#define DEV_UART_NCHILD \
    (sizeof dev_uart_children / sizeof dev_uart_children[0])

/** /dev/adc directory table — raw conversions, no calibration */
static const tiku_vfs_node_t dev_adc_children[] = {
    { "temp",    TIKU_VFS_FILE, adc_temp_read,    NULL, NULL, 0, &desc_adc_temp },
    { "battery", TIKU_VFS_FILE, adc_battery_read, NULL, NULL, 0, &desc_adc_batt },
};

/** /dev/i2c directory table — the live bus scanner */
static const tiku_vfs_node_t dev_i2c_children[] = {
    { "scan", TIKU_VFS_FILE, i2c_scan_read, NULL, NULL, 0, &desc_scan },
};

/** /dev/spi directory table — read-only configuration view */
static const tiku_vfs_node_t dev_spi_children[] = {
    { "config", TIKU_VFS_FILE, spi_config_read, NULL, NULL, 0,
      &desc_bus_config },
};

/*
 * The /dev directory table -- every hardware-facing node.  LED entries are
 * gated one by one on TIKU_BOARD_LED_COUNT so a one-LED board exposes exactly
 * /dev/led0; the gpio subtrees come from tiku_vfs_tree_gpio.c and sensors/
 * from tiku_vfs_tree_sensor.c.  To add a node, implement the handler above
 * and append the entry here.
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
    { "uart",     TIKU_VFS_DIR,  NULL, NULL, dev_uart_children,
      DEV_UART_NCHILD },
    { "adc",      TIKU_VFS_DIR,  NULL, NULL, dev_adc_children, 2 },
    { "i2c",      TIKU_VFS_DIR,  NULL, NULL, dev_i2c_children, 1 },
    { "spi",      TIKU_VFS_DIR,  NULL, NULL, dev_spi_children, 1 },
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

/**
 * @brief Get the fully-formed /dev directory node.
 *
 * See the header for the copy-by-value contract with the root
 * assembly.
 *
 * @return Pointer to the static /dev directory node
 */
const tiku_vfs_node_t *
tiku_vfs_tree_dev_get(void)
{
    return &dev_node;
}

/**
 * @brief Initialise /dev hardware state.
 *
 * Configures every board LED pin (direction and initial level) and zeroes the
 * led_state shadow so reads and hardware agree from the first access.
 */
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
