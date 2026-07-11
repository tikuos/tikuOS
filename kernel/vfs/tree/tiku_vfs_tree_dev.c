/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_dev.c - /dev subtree (files + assembly)
 *
 * Hardware-facing nodes:
 *
 *   /dev/led0..ledN     (rw) board LEDs; count from TIKU_BOARD_LED_COUNT
 *   /dev/console        (rw) raw UART access (non-blocking read)
 *   /dev/null           (rw) data sink, reads empty
 *   /dev/zero           (r)  NUL-byte source
 *   /dev/uart/overruns  (r)  RX overrun counter
 *   /dev/uart/baud      (r)  configured baud rate
 *   /dev/adc/temp       (r)  raw internal temperature conversion
 *   /dev/adc/battery    (r)  raw supply-voltage conversion
 *   /dev/i2c/scan       (r)  bus scan — probing happens at read time!
 *   /dev/spi/config     (r)  active SPI mode/order/prescaler
 *   /dev/gpio, gpio_dir      stitched in from tiku_vfs_tree_gpio.c
 *
 * LED nodes drive real hardware via the tiku_led_*() API and keep
 * a shadow state in SRAM because PxOUT cannot be read back
 * uniformly across boards (active-low wiring would invert the
 * answer).  Everything else queries its driver live at read time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_dev.h"
#include "tiku_vfs_tree_gpio.h"
#include "tiku.h"
#include <kernel/cpu/tiku_common.h>
#include <kernel/timers/tiku_clock.h>   /* TIKU_CLOCK_SECOND for cache windows */
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

/**
 * Shadow of each LED's logical state (1 = lit), maintained by the
 * write handlers and cleared in tiku_vfs_tree_dev_init().  Reads
 * serve this mirror instead of the output register because board
 * wiring (active-high vs active-low) would make a raw PxOUT read
 * ambiguous; the cost is that direct tiku_led_*() calls from
 * application code bypass the mirror and go unnoticed here.
 */
static uint8_t led_state[TIKU_BOARD_LED_COUNT];

/**
 * @brief Generate the read/write handler pair for LED index N.
 *
 * Read renders the shadow state as "0\n" or "1\n".  Write decodes
 * the first payload byte: '1' on, '0' off, 't' toggle — anything
 * else is silently ignored (return 0) so trailing whitespace from
 * shell input is harmless.  Both the hardware (via tiku_led_*())
 * and the shadow are updated together.
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

/* Generate read/write function pairs for each board LED */
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
 * Renders the count of RX bytes dropped because the ring buffer
 * was full when they arrived, as a decimal line.  A non-zero,
 * growing value means the consumer (usually the shell) is not
 * draining fast enough for the incoming line rate — the first
 * thing to check when pasted text arrives mangled.
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
 * Counts the RX-engine wedge self-heals the UART driver performed since
 * boot (a lost DMA re-arm silences RX until re-started; the driver
 * detects the captured-but-unmoved-byte signature and repairs it).
 * Non-zero after a heavy SLIP/TLS session means the wedge recurred and
 * was healed in place.
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
 * Renders the configured baud rate from the board header as a
 * decimal line ("9600\n" on MSP430 boards, "115200\n" on RP2350).
 * Build-time constant — handy for a host-side script that wants to
 * confirm it opened the port at the right speed.
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
 * Renders the active SPI configuration on one line:
 *
 *   "mode=0 order=msb pre=8\n"   (SPI mode, bit order, prescaler)
 *   "off\n"                      driver compiled in but not configured
 *   "n/a\n"                      TIKU_SPI_ENABLE=0 build
 *
 * Read-only: bus configuration belongs to the driver owning the
 * peripheral, not to a shell one-liner that could yank the clock
 * mode out from under an in-flight transfer.
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
 * Triggers a conversion on the internal temperature channel and
 * renders the RAW ADC count as a decimal line ("2789\n"), or
 * "err\n" if the conversion fails.  Conversion to degrees is left
 * to the consumer: it needs the per-device TLV calibration
 * constants, and keeping the node raw keeps the handler small and
 * the policy out of the kernel.
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
 * "err\n".  Useful for crude battery gauging on coin-cell boards.
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
 * @brief Read handler for /dev/i2c/scan.
 *
 * ACTIVE prober: reading this node performs a live scan of the
 * 7-bit address range 0x08..0x77 (the reserved addresses at both
 * ends are skipped), issuing a zero-length write to each and
 * collecting the ACKs.  Renders the responders as hex words on
 * one line, or "none":
 *
 *   "0x18 0x48\n"    two devices found
 *   "none\n"         empty bus
 *
 * Takes bus-time proportional to 112 probe transactions, and a
 * zero-length write is harmless to almost every part but not
 * guaranteed side-effect-free for exotic devices — this is a
 * debugging aid, not something to poll from a loop.  The scan
 * stops early if the output buffer runs low (pos < max-6 keeps
 * room for one more "0xNN " token plus the terminator).
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
 * Non-blocking: copies only the bytes already waiting in the
 * hardware RX register / ring buffer and returns their count,
 * 0 when the line is idle.  Raw bytes, no newline appended —
 * unlike every other node this is a byte stream, not a rendered
 * value.  Note the shell itself consumes the same UART; reading
 * the console from within an interactive shell session steals
 * bytes from the line editor.
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
 * Returns 0 bytes, matching the Unix namesake: reading null gives
 * instant EOF.
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
 * Accepts and ignores any payload.  Gives scripts a portable
 * "throw this away" target and exercises the write path in tests
 * without side effects.
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
 * Fills the entire buffer with zeros and returns @p max bytes.
 * Programmatic consumers use the return value; shell
 * "read /dev/zero" will display an empty string, which matches
 * the expected Unix behaviour.
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

/* Type descriptors (const, FRAM) for the typed /dev nodes below. */
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
    { "overruns", TIKU_VFS_FILE, uart_overruns_read, NULL, NULL, 0 },
    { "baud",     TIKU_VFS_FILE, uart_baud_read,     NULL, NULL, 0 },
#if defined(PLATFORM_NORDIC)
    { "recoveries", TIKU_VFS_FILE, uart_recoveries_read, NULL, NULL, 0 },
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
    { "scan", TIKU_VFS_FILE, i2c_scan_read, NULL, NULL, 0 },
};

/** /dev/spi directory table — read-only configuration view */
static const tiku_vfs_node_t dev_spi_children[] = {
    { "config", TIKU_VFS_FILE, spi_config_read, NULL, NULL, 0 },
};

/**
 * The /dev directory table — every hardware-facing node.
 *
 * LED entries are gated one by one on TIKU_BOARD_LED_COUNT so a
 * one-LED board exposes exactly /dev/led0; the gpio subtrees come
 * from tiku_vfs_tree_gpio.c with their port count in
 * TIKU_VFS_TREE_GPIO_NPORTS.  To add a /dev node: implement the
 * handler above (or in a new module) and append the entry here.
 */
static const tiku_vfs_node_t dev_children[] = {
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
    { "console",  TIKU_VFS_FILE, console_read, console_write, NULL, 0 },
    { "null",     TIKU_VFS_FILE, devnull_read, devnull_write, NULL, 0 },
    { "zero",     TIKU_VFS_FILE, devzero_read, NULL, NULL, 0 },
    { "gpio",     TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_gpio_children,     TIKU_VFS_TREE_GPIO_NPORTS },
    { "gpio_dir", TIKU_VFS_DIR,  NULL, NULL,
      tiku_vfs_tree_gpio_dir_children, TIKU_VFS_TREE_GPIO_NPORTS },
    { "uart",     TIKU_VFS_DIR,  NULL, NULL, dev_uart_children,
      DEV_UART_NCHILD },
    { "adc",      TIKU_VFS_DIR,  NULL, NULL, dev_adc_children, 2 },
    { "i2c",      TIKU_VFS_DIR,  NULL, NULL, dev_i2c_children, 1 },
    { "spi",      TIKU_VFS_DIR,  NULL, NULL, dev_spi_children, 1 },
};

/**
 * The /dev directory node itself, fully formed with its name and a
 * sizeof-derived child count so the root assembly can copy it by
 * value (getter pattern — see tiku_vfs_tree_dev_get()).
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
 * Configures every board LED pin (direction + initial level) and
 * zeroes the led_state shadow so reads and hardware agree from
 * the first access.  Boards with no LEDs compile this down to the
 * bare tiku_led_init_all() call.
 */
void
tiku_vfs_tree_dev_init(void)
{
    /* Init LED hardware */
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
