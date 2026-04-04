/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree.c - System VFS tree with /sys, /dev, /proc
 *
 * Builds the production VFS tree and calls tiku_vfs_init().
 * LED nodes drive real hardware via tiku_led_*() API.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree.h"
#include "tiku_vfs.h"
#include "tiku.h"
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_timer.h>
#include <kernel/timers/tiku_htimer.h>
#include <kernel/cpu/tiku_common.h>
#include <interfaces/led/tiku_led.h>
#include <kernel/cpu/tiku_watchdog.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/process/tiku_proc_vfs.h>
#include <arch/msp430/tiku_gpio_arch.h>
#include <arch/msp430/tiku_uart_arch.h>
#include <kernel/process/tiku_process.h>
#include <kernel/scheduler/tiku_sched.h>
#include <interfaces/adc/tiku_adc.h>
#include <interfaces/bus/tiku_i2c_bus.h>
#if TIKU_SPI_ENABLE
#include <interfaces/bus/tiku_spi_bus.h>
#endif
#include <boot/tiku_boot.h>
#include <stdio.h>

#if TIKU_SHELL_ENABLE
#include <kernel/shell/commands/tiku_shell_cmd_sleep.h>
#endif

#ifdef PLATFORM_MSP430
#include <msp430.h>
#endif

/*---------------------------------------------------------------------------*/
/* LED STATE TRACKING (indexed by TIKU_BOARD_LED_COUNT)                      */
/*---------------------------------------------------------------------------*/

#if TIKU_BOARD_LED_COUNT > 0
static uint8_t led_state[TIKU_BOARD_LED_COUNT];

/* Helper — generic read/write for a given LED index */
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
/* /sys/uptime                                                               */
/*---------------------------------------------------------------------------*/

static int
uptime_read(char *buf, size_t max)
{
    tiku_clock_time_t t = tiku_clock_time();
    uint16_t secs = (uint16_t)(t / TIKU_CLOCK_SECOND);
    return snprintf(buf, max, "%u\n", secs);
}

/*---------------------------------------------------------------------------*/
/* /sys/mem/sram, /sys/mem/nvm                                               */
/*---------------------------------------------------------------------------*/

static int
sram_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)TIKU_DEVICE_RAM_SIZE);
}

static int
nvm_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)TIKU_DEVICE_FRAM_SIZE);
}

/*---------------------------------------------------------------------------*/
/* /sys/mem/free — live stack headroom                                        */
/*---------------------------------------------------------------------------*/

extern char _end;
extern char __stack;

static int
mem_free_read(char *buf, size_t max)
{
    uint16_t sp;
    uint16_t end_addr = (uint16_t)(uintptr_t)&_end;

#ifdef PLATFORM_MSP430
    __asm__ volatile ("mov r1, %0" : "=r"(sp));
#else
    sp = (uint16_t)(uintptr_t)&__stack;
#endif

    if (sp > end_addr) {
        return snprintf(buf, max, "%u\n", sp - end_addr);
    }
    return snprintf(buf, max, "0\n");
}

/*---------------------------------------------------------------------------*/
/* /sys/mem/used — sum of per-process SRAM allocation                        */
/*---------------------------------------------------------------------------*/

static int
mem_used_read(char *buf, size_t max)
{
    uint16_t total = 0;
    uint8_t i;
    for (i = 0; i < TIKU_PROCESS_MAX; i++) {
        struct tiku_process *p = tiku_process_get((int8_t)i);
        if (p != NULL) {
            total += p->sram_used;
        }
    }
    return snprintf(buf, max, "%u\n", total);
}

/*---------------------------------------------------------------------------*/
/* /sys/boot/reason — last reset cause from SYSRSTIV                         */
/*---------------------------------------------------------------------------*/

static uint16_t boot_reset_cause;

static const char *
reset_cause_str(uint16_t iv)
{
    switch (iv) {
    case 0x0000: return "none";
    case 0x0002: return "brownout";
    case 0x0004: return "rstnmi";
    case 0x0006: return "sw-bor";
    case 0x0008: return "lpm5-wake";
    case 0x000A: return "security";
    case 0x000E: return "svs";
    case 0x0014: return "sw-por";
    case 0x0016: return "wdt-timeout";
    case 0x0018: return "wdt-pwviol";
    case 0x001A: return "fram-pwviol";
    case 0x001C: return "fram-bit-err";
    case 0x001E: return "periph-fetch";
    case 0x0020: return "pmm-pwviol";
    case 0x0024: return "fll-unlock";
    default:     return "unknown";
    }
}

static int
boot_reason_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", reset_cause_str(boot_reset_cause));
}

static uint32_t boot_count_value;

static int
boot_count_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)boot_count_value);
}

/*---------------------------------------------------------------------------*/
/* /sys/boot/stage                                                           */
/*---------------------------------------------------------------------------*/

static int
boot_stage_read(char *buf, size_t max)
{
    static const char * const stage_names[] = {
        "init", "cpu", "memory", "peripherals", "services", "complete"
    };
    tiku_boot_stage_e s = tiku_boot_get_stage();
    const char *name = (s <= TIKU_BOOT_STAGE_COMPLETE)
                           ? stage_names[s] : "unknown";
    return snprintf(buf, max, "%s\n", name);
}

/*---------------------------------------------------------------------------*/
/* /sys/boot/rstiv — raw SYSRSTIV value (hex, for scripting)                 */
/*---------------------------------------------------------------------------*/

static int
boot_rstiv_read(char *buf, size_t max)
{
    return snprintf(buf, max, "0x%04x\n", boot_reset_cause);
}

/*---------------------------------------------------------------------------*/
/* /sys/boot/clock/ — live clock frequencies                                 */
/*---------------------------------------------------------------------------*/

static int
boot_clock_mclk_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    tiku_cpu_msp430_clock_get_hz());
}

static int
boot_clock_smclk_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    tiku_cpu_msp430_smclk_get_hz());
}

static int
boot_clock_aclk_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    tiku_cpu_msp430_aclk_get_hz());
}

static int
boot_clock_fault_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    tiku_cpu_msp430_clock_has_fault() ? 1u : 0u);
}

/*---------------------------------------------------------------------------*/
/* /sys/boot/mpu/violations — MPU segment violation flags (hex)              */
/*---------------------------------------------------------------------------*/

static int
boot_mpu_violations_read(char *buf, size_t max)
{
    return snprintf(buf, max, "0x%02x\n",
                    tiku_mpu_get_violation_flags());
}

/*---------------------------------------------------------------------------*/
/* /sys/sched/idle                                                           */
/*---------------------------------------------------------------------------*/

static int
sched_idle_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_sched_idle_count());
}

/*---------------------------------------------------------------------------*/
/* /sys/timer/list/<n> — per-timer detail                                    */
/*---------------------------------------------------------------------------*/

static int
timer_detail_read(uint8_t idx, char *buf, size_t max)
{
    struct tiku_timer *t = tiku_timer_get(idx);
    if (t == NULL) {
        return snprintf(buf, max, "(none)\n");
    }
    return snprintf(buf, max, "%s rem=%u int=%u%s\n",
                    t->mode == TIKU_TIMER_MODE_EVENT ? "evt" : "cb",
                    (unsigned)tiku_timer_remaining(t),
                    (unsigned)t->interval,
                    t->p ? "" : " (no target)");
}

#define TIMER_DETAIL(idx)                                                   \
    static int timer_detail_##idx(char *buf, size_t max) {                  \
        return timer_detail_read(idx, buf, max);                            \
    }

TIMER_DETAIL(0) TIMER_DETAIL(1) TIMER_DETAIL(2) TIMER_DETAIL(3)

/*---------------------------------------------------------------------------*/
/* /sys/power/mode                                                           */
/*---------------------------------------------------------------------------*/

static int
power_mode_read(char *buf, size_t max)
{
#if TIKU_SHELL_ENABLE
    return snprintf(buf, max, "%s\n", tiku_shell_sleep_mode_str());
#else
    return snprintf(buf, max, "off\n");
#endif
}

/*---------------------------------------------------------------------------*/
/* /sys/power/wake                                                           */
/*---------------------------------------------------------------------------*/

static int
power_wake_read(char *buf, size_t max)
{
    int pos = 0;

#ifdef PLATFORM_MSP430
    pos += snprintf(buf + pos, max - pos, "timer0:%s ",
                    (TA0CTL & MC__UP) ? "on" : "off");
    pos += snprintf(buf + pos, max - pos, "uart:%s ",
                    (UCA0IE & UCRXIE) ? "on" : "off");
    pos += snprintf(buf + pos, max - pos, "wdt:%s ",
                    (SFRIE1 & WDTIE) ? "on" : "off");
    pos += snprintf(buf + pos, max - pos, "gpio:%s\n",
                    (P1IE | P2IE | P3IE | P4IE) ? "on" : "off");
#else
    pos += snprintf(buf + pos, max - pos, "n/a\n");
#endif

    return pos;
}

/*---------------------------------------------------------------------------*/
/* /sys/timer/count, /sys/timer/next                                         */
/*---------------------------------------------------------------------------*/

static int
timer_count_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_timer_count());
}

static int
timer_fired_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_timer_fired());
}

static int
timer_next_read(char *buf, size_t max)
{
    tiku_clock_time_t next = tiku_timer_next_expiration();
    tiku_clock_time_t now  = tiku_clock_time();

    if (next == 0) {
        return snprintf(buf, max, "none\n");
    }

    if (next > now) {
        return snprintf(buf, max, "%lu\n",
                        (unsigned long)(next - now));
    }

    return snprintf(buf, max, "0\n");
}

/*---------------------------------------------------------------------------*/
/* /sys/clock/ticks                                                          */
/*---------------------------------------------------------------------------*/

static int
clock_ticks_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_clock_time());
}

/*---------------------------------------------------------------------------*/
/* /sys/watchdog/mode                                                        */
/*---------------------------------------------------------------------------*/

static int
watchdog_mode_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", tiku_watchdog_mode_str());
}

static int
watchdog_mode_write(const char *buf, size_t len)
{
    (void)len;
    tiku_wdt_mode_t mode;
    if (buf[0] == 'w') {
        mode = TIKU_WDT_MODE_WATCHDOG;
    } else if (buf[0] == 'i') {
        mode = TIKU_WDT_MODE_INTERVAL;
    } else {
        return -1;
    }
    tiku_watchdog_config(mode, tiku_watchdog_get_clk(),
                         tiku_watchdog_get_interval(), 0, 1);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* /sys/watchdog/clock                                                       */
/*---------------------------------------------------------------------------*/

static int
watchdog_clock_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n",
                    tiku_watchdog_get_clk() == TIKU_WDT_SRC_ACLK
                        ? "aclk" : "smclk");
}

static int
watchdog_clock_write(const char *buf, size_t len)
{
    (void)len;
    tiku_wdt_clk_t clk;
    if (buf[0] == 'a') {
        clk = TIKU_WDT_SRC_ACLK;
    } else if (buf[0] == 's') {
        clk = TIKU_WDT_SRC_SMCLK;
    } else {
        return -1;
    }
    tiku_watchdog_config(tiku_watchdog_get_mode(), clk,
                         tiku_watchdog_get_interval(), 0, 1);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* /sys/watchdog/interval                                                    */
/*---------------------------------------------------------------------------*/

static int
watchdog_interval_read(char *buf, size_t max)
{
    tiku_wdt_interval_t iv = tiku_watchdog_get_interval();
    const char *s;
    if (iv == WDTIS__64) {
        s = "64";
    } else if (iv == WDTIS__512) {
        s = "512";
    } else if (iv == WDTIS__8192) {
        s = "8192";
    } else if (iv == WDTIS__32768) {
        s = "32768";
    } else {
        s = "unknown";
    }
    return snprintf(buf, max, "%s\n", s);
}

static int
watchdog_interval_write(const char *buf, size_t len)
{
    uint16_t val = 0;
    size_t i;
    tiku_wdt_interval_t iv;

    for (i = 0; i < len && buf[i] >= '0' && buf[i] <= '9'; i++) {
        val = val * 10 + (uint16_t)(buf[i] - '0');
    }
    if (val == 64) {
        iv = WDTIS__64;
    } else if (val == 512) {
        iv = WDTIS__512;
    } else if (val == 8192) {
        iv = WDTIS__8192;
    } else if (val == 32768) {
        iv = WDTIS__32768;
    } else {
        return -1;
    }
    tiku_watchdog_config(tiku_watchdog_get_mode(),
                         tiku_watchdog_get_clk(), iv, 0, 1);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* /sys/watchdog/kick                                                        */
/*---------------------------------------------------------------------------*/

static int
watchdog_kick_write(const char *buf, size_t len)
{
    (void)buf;
    (void)len;
    tiku_watchdog_kick();
    return 0;
}

/*---------------------------------------------------------------------------*/
/* /sys/htimer/now, /sys/htimer/scheduled                                    */
/*---------------------------------------------------------------------------*/

static int
htimer_now_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_htimer_arch_now());
}

static int
htimer_scheduled_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    tiku_htimer_is_scheduled() ? 1u : 0u);
}

/*---------------------------------------------------------------------------*/
/* /sys/version                                                              */
/*---------------------------------------------------------------------------*/

static int
version_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", TIKU_VERSION);
}

/*---------------------------------------------------------------------------*/
/* /sys/device                                                               */
/*---------------------------------------------------------------------------*/

static int
device_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", TIKU_DEVICE_NAME);
}

/*---------------------------------------------------------------------------*/
/* /sys/cpu/freq                                                             */
/*---------------------------------------------------------------------------*/

static int
cpu_freq_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n", (unsigned long)TIKU_MAIN_CPU_HZ);
}

/*---------------------------------------------------------------------------*/
/* /dev/uart/overruns                                                        */
/*---------------------------------------------------------------------------*/

static int
uart_overruns_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_uart_overrun_count());
}

static int
uart_baud_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)TIKU_BOARD_UART_BAUD);
}

/*---------------------------------------------------------------------------*/
/* /dev/spi/config                                                           */
/*---------------------------------------------------------------------------*/

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
/* /dev/gpio/dir — per-port direction summary                                */
/*---------------------------------------------------------------------------*/

static int
gpio_dir_read(uint8_t port, char *buf, size_t max)
{
    int pos = 0;
    uint8_t pin;
    for (pin = 0; pin < 8 && pos < (int)max - 4; pin++) {
        int8_t d = tiku_gpio_arch_get_dir(port, pin);
        pos += snprintf(buf + pos, max - pos, "%c",
                        d == 1 ? 'O' : (d == 0 ? 'I' : '?'));
    }
    if (pos < (int)max - 1) {
        buf[pos++] = '\n';
        buf[pos] = '\0';
    }
    return pos;
}

#define GPIO_DIR(p)                                                         \
    static int gpio_dir_##p(char *buf, size_t max) {                        \
        return gpio_dir_read(p, buf, max);                                  \
    }

#if TIKU_DEVICE_HAS_PORT1
GPIO_DIR(1)
#endif
#if TIKU_DEVICE_HAS_PORT2
GPIO_DIR(2)
#endif
#if TIKU_DEVICE_HAS_PORT3
GPIO_DIR(3)
#endif
#if TIKU_DEVICE_HAS_PORT4
GPIO_DIR(4)
#endif

/*---------------------------------------------------------------------------*/
/* /dev/adc/temp, /dev/adc/battery                                           */
/*---------------------------------------------------------------------------*/

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
 * @brief Read drains available UART RX bytes into the buffer.
 *
 * Non-blocking: returns only the bytes already waiting in the
 * hardware RX register / ring buffer.  Returns 0 when idle.
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
 * @brief Write sends each byte through the UART.
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

static int
devnull_read(char *buf, size_t max)
{
    (void)buf;
    (void)max;
    return 0;
}

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
 * @brief Read fills the buffer with NUL bytes.
 *
 * Returns @p max bytes.  Programmatic consumers use the return
 * value; shell "read /dev/zero" will display an empty string,
 * which matches the expected Unix behaviour.
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
/* /dev/gpio — per-pin VFS nodes                                             */
/*---------------------------------------------------------------------------*/

/*
 * Common read/write for GPIO pins.  The macro-generated wrappers
 * below call these with hardcoded port/pin constants.
 *
 * Read:  returns "0\n" or "1\n"
 * Write: "0" = low, "1" = high, "t" = toggle, "i" = input (pull-up)
 */
static int
gpio_pin_read(uint8_t port, uint8_t pin, char *buf, size_t max)
{
    int8_t v = tiku_gpio_arch_read(port, pin);
    if (v < 0) {
        return snprintf(buf, max, "err\n");
    }
    return snprintf(buf, max, "%u\n", (unsigned)v);
}

static int
gpio_pin_write(uint8_t port, uint8_t pin, const char *buf, size_t len)
{
    (void)len;
    if (buf[0] == '1') {
        tiku_gpio_arch_write(port, pin, 1);
    } else if (buf[0] == '0') {
        tiku_gpio_arch_write(port, pin, 0);
    } else if (buf[0] == 't') {
        tiku_gpio_arch_toggle(port, pin);
    } else if (buf[0] == 'i') {
        tiku_gpio_arch_set_input(port, pin);
    }
    return 0;
}

/*
 * Generate a read and write handler for each port/pin combination.
 * Each wrapper is ~10 bytes of code (load constants + call).
 */
#define GPIO_PIN(p, b)                                                      \
    static int gpio_r_##p##_##b(char *buf, size_t max) {                    \
        return gpio_pin_read(p, b, buf, max);                               \
    }                                                                       \
    static int gpio_w_##p##_##b(const char *buf, size_t len) {              \
        return gpio_pin_write(p, b, buf, len);                              \
    }

/* Pin name strings (shared across all ports) */
static const char pn0[] = "0", pn1[] = "1", pn2[] = "2", pn3[] = "3",
                  pn4[] = "4", pn5[] = "5", pn6[] = "6", pn7[] = "7";

/* Generate handlers for each available port */
#if TIKU_DEVICE_HAS_PORT1
GPIO_PIN(1,0) GPIO_PIN(1,1) GPIO_PIN(1,2) GPIO_PIN(1,3)
GPIO_PIN(1,4) GPIO_PIN(1,5) GPIO_PIN(1,6) GPIO_PIN(1,7)
#endif
#if TIKU_DEVICE_HAS_PORT2
GPIO_PIN(2,0) GPIO_PIN(2,1) GPIO_PIN(2,2) GPIO_PIN(2,3)
GPIO_PIN(2,4) GPIO_PIN(2,5) GPIO_PIN(2,6) GPIO_PIN(2,7)
#endif
#if TIKU_DEVICE_HAS_PORT3
GPIO_PIN(3,0) GPIO_PIN(3,1) GPIO_PIN(3,2) GPIO_PIN(3,3)
GPIO_PIN(3,4) GPIO_PIN(3,5) GPIO_PIN(3,6) GPIO_PIN(3,7)
#endif
#if TIKU_DEVICE_HAS_PORT4
GPIO_PIN(4,0) GPIO_PIN(4,1) GPIO_PIN(4,2) GPIO_PIN(4,3)
GPIO_PIN(4,4) GPIO_PIN(4,5) GPIO_PIN(4,6) GPIO_PIN(4,7)
#endif

/* VFS node arrays per port */
#define GPIO_NODE(p, b) \
    { pn##b, TIKU_VFS_FILE, gpio_r_##p##_##b, gpio_w_##p##_##b, NULL, 0 }

#if TIKU_DEVICE_HAS_PORT1
static const tiku_vfs_node_t gpio_p1[] = {
    GPIO_NODE(1,0), GPIO_NODE(1,1), GPIO_NODE(1,2), GPIO_NODE(1,3),
    GPIO_NODE(1,4), GPIO_NODE(1,5), GPIO_NODE(1,6), GPIO_NODE(1,7),
};
#endif
#if TIKU_DEVICE_HAS_PORT2
static const tiku_vfs_node_t gpio_p2[] = {
    GPIO_NODE(2,0), GPIO_NODE(2,1), GPIO_NODE(2,2), GPIO_NODE(2,3),
    GPIO_NODE(2,4), GPIO_NODE(2,5), GPIO_NODE(2,6), GPIO_NODE(2,7),
};
#endif
#if TIKU_DEVICE_HAS_PORT3
static const tiku_vfs_node_t gpio_p3[] = {
    GPIO_NODE(3,0), GPIO_NODE(3,1), GPIO_NODE(3,2), GPIO_NODE(3,3),
    GPIO_NODE(3,4), GPIO_NODE(3,5), GPIO_NODE(3,6), GPIO_NODE(3,7),
};
#endif
#if TIKU_DEVICE_HAS_PORT4
static const tiku_vfs_node_t gpio_p4[] = {
    GPIO_NODE(4,0), GPIO_NODE(4,1), GPIO_NODE(4,2), GPIO_NODE(4,3),
    GPIO_NODE(4,4), GPIO_NODE(4,5), GPIO_NODE(4,6), GPIO_NODE(4,7),
};
#endif

/* Port directory names */
static const tiku_vfs_node_t gpio_children[] = {
#if TIKU_DEVICE_HAS_PORT1
    { "1", TIKU_VFS_DIR, NULL, NULL, gpio_p1, 8 },
#endif
#if TIKU_DEVICE_HAS_PORT2
    { "2", TIKU_VFS_DIR, NULL, NULL, gpio_p2, 8 },
#endif
#if TIKU_DEVICE_HAS_PORT3
    { "3", TIKU_VFS_DIR, NULL, NULL, gpio_p3, 8 },
#endif
#if TIKU_DEVICE_HAS_PORT4
    { "4", TIKU_VFS_DIR, NULL, NULL, gpio_p4, 8 },
#endif
};

#define GPIO_PORT_COUNT ( \
    TIKU_DEVICE_HAS_PORT1 + TIKU_DEVICE_HAS_PORT2 + \
    TIKU_DEVICE_HAS_PORT3 + TIKU_DEVICE_HAS_PORT4)

/*---------------------------------------------------------------------------*/
/* VFS TREE                                                                  */
/*---------------------------------------------------------------------------*/

/*
 * /
 * ├── sys/
 * │   ├── version  (read-only)  — OS version string
 * │   ├── device   (read-only)  — MCU name
 * │   ├── uptime   (read-only)  — seconds since boot
 * │   ├── mem/
 * │   │   ├── sram  (read-only)
 * │   │   └── nvm   (read-only)
 * │   └── cpu/
 * │       └── freq  (read-only) — clock frequency in Hz
 * └── dev/
 *     ├── led0..ledN   (read-write, count from TIKU_BOARD_LED_COUNT)
 */

static const tiku_vfs_node_t sys_mem_children[] = {
    { "sram", TIKU_VFS_FILE, sram_read,      NULL, NULL, 0 },
    { "nvm",  TIKU_VFS_FILE, nvm_read,       NULL, NULL, 0 },
    { "free", TIKU_VFS_FILE, mem_free_read,  NULL, NULL, 0 },
    { "used", TIKU_VFS_FILE, mem_used_read,  NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_cpu_children[] = {
    { "freq", TIKU_VFS_FILE, cpu_freq_read, NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_power_children[] = {
    { "mode", TIKU_VFS_FILE, power_mode_read, NULL, NULL, 0 },
    { "wake", TIKU_VFS_FILE, power_wake_read, NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_timer_list_children[] = {
    { "0", TIKU_VFS_FILE, timer_detail_0, NULL, NULL, 0 },
    { "1", TIKU_VFS_FILE, timer_detail_1, NULL, NULL, 0 },
    { "2", TIKU_VFS_FILE, timer_detail_2, NULL, NULL, 0 },
    { "3", TIKU_VFS_FILE, timer_detail_3, NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_timer_children[] = {
    { "count", TIKU_VFS_FILE, timer_count_read, NULL, NULL, 0 },
    { "next",  TIKU_VFS_FILE, timer_next_read,  NULL, NULL, 0 },
    { "fired", TIKU_VFS_FILE, timer_fired_read, NULL, NULL, 0 },
    { "list",  TIKU_VFS_DIR,  NULL, NULL, sys_timer_list_children, 4 },
};

static const tiku_vfs_node_t sys_clock_children[] = {
    { "ticks", TIKU_VFS_FILE, clock_ticks_read, NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_watchdog_children[] = {
    { "mode",     TIKU_VFS_FILE, watchdog_mode_read,     watchdog_mode_write,     NULL, 0 },
    { "clock",    TIKU_VFS_FILE, watchdog_clock_read,    watchdog_clock_write,    NULL, 0 },
    { "interval", TIKU_VFS_FILE, watchdog_interval_read, watchdog_interval_write, NULL, 0 },
    { "kick",     TIKU_VFS_FILE, NULL,                   watchdog_kick_write,     NULL, 0 },
};

static const tiku_vfs_node_t sys_htimer_children[] = {
    { "now",       TIKU_VFS_FILE, htimer_now_read,       NULL, NULL, 0 },
    { "scheduled", TIKU_VFS_FILE, htimer_scheduled_read, NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_boot_clock_children[] = {
    { "mclk",  TIKU_VFS_FILE, boot_clock_mclk_read,  NULL, NULL, 0 },
    { "smclk", TIKU_VFS_FILE, boot_clock_smclk_read, NULL, NULL, 0 },
    { "aclk",  TIKU_VFS_FILE, boot_clock_aclk_read,  NULL, NULL, 0 },
    { "fault", TIKU_VFS_FILE, boot_clock_fault_read,  NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_boot_mpu_children[] = {
    { "violations", TIKU_VFS_FILE, boot_mpu_violations_read, NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_boot_children[] = {
    { "reason", TIKU_VFS_FILE, boot_reason_read, NULL, NULL, 0 },
    { "count",  TIKU_VFS_FILE, boot_count_read,  NULL, NULL, 0 },
    { "stage",  TIKU_VFS_FILE, boot_stage_read,  NULL, NULL, 0 },
    { "rstiv",  TIKU_VFS_FILE, boot_rstiv_read,  NULL, NULL, 0 },
    { "clock",  TIKU_VFS_DIR,  NULL, NULL, sys_boot_clock_children, 4 },
    { "mpu",    TIKU_VFS_DIR,  NULL, NULL, sys_boot_mpu_children, 1 },
};

static const tiku_vfs_node_t sys_sched_children[] = {
    { "idle", TIKU_VFS_FILE, sched_idle_read, NULL, NULL, 0 },
};

/* GPIO direction nodes — one file per port */
static const tiku_vfs_node_t gpio_dir_children[] = {
#if TIKU_DEVICE_HAS_PORT1
    { "1", TIKU_VFS_FILE, gpio_dir_1, NULL, NULL, 0 },
#endif
#if TIKU_DEVICE_HAS_PORT2
    { "2", TIKU_VFS_FILE, gpio_dir_2, NULL, NULL, 0 },
#endif
#if TIKU_DEVICE_HAS_PORT3
    { "3", TIKU_VFS_FILE, gpio_dir_3, NULL, NULL, 0 },
#endif
#if TIKU_DEVICE_HAS_PORT4
    { "4", TIKU_VFS_FILE, gpio_dir_4, NULL, NULL, 0 },
#endif
};

static const tiku_vfs_node_t dev_spi_children[] = {
    { "config", TIKU_VFS_FILE, spi_config_read, NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_children[] = {
    { "version",  TIKU_VFS_FILE, version_read, NULL, NULL, 0 },
    { "device",   TIKU_VFS_FILE, device_read,  NULL, NULL, 0 },
    { "uptime",   TIKU_VFS_FILE, uptime_read,  NULL, NULL, 0 },
    { "mem",      TIKU_VFS_DIR,  NULL, NULL, sys_mem_children, 4 },
    { "cpu",      TIKU_VFS_DIR,  NULL, NULL, sys_cpu_children, 1 },
    { "power",    TIKU_VFS_DIR,  NULL, NULL, sys_power_children, 2 },
    { "timer",    TIKU_VFS_DIR,  NULL, NULL, sys_timer_children, 4 },
    { "clock",    TIKU_VFS_DIR,  NULL, NULL, sys_clock_children, 1 },
    { "watchdog", TIKU_VFS_DIR,  NULL, NULL, sys_watchdog_children, 4 },
    { "htimer",   TIKU_VFS_DIR,  NULL, NULL, sys_htimer_children, 2 },
    { "boot",     TIKU_VFS_DIR,  NULL, NULL, sys_boot_children, 6 },
    { "sched",    TIKU_VFS_DIR,  NULL, NULL, sys_sched_children, 1 },
};

static const tiku_vfs_node_t dev_uart_children[] = {
    { "overruns", TIKU_VFS_FILE, uart_overruns_read, NULL, NULL, 0 },
    { "baud",     TIKU_VFS_FILE, uart_baud_read,     NULL, NULL, 0 },
};

static const tiku_vfs_node_t dev_adc_children[] = {
    { "temp",    TIKU_VFS_FILE, adc_temp_read,    NULL, NULL, 0 },
    { "battery", TIKU_VFS_FILE, adc_battery_read, NULL, NULL, 0 },
};

static const tiku_vfs_node_t dev_i2c_children[] = {
    { "scan", TIKU_VFS_FILE, i2c_scan_read, NULL, NULL, 0 },
};

static const tiku_vfs_node_t dev_children[] = {
#if TIKU_BOARD_LED_COUNT >= 1
    { "led0",     TIKU_VFS_FILE, led0_read, led0_write, NULL, 0 },
#endif
#if TIKU_BOARD_LED_COUNT >= 2
    { "led1",     TIKU_VFS_FILE, led1_read, led1_write, NULL, 0 },
#endif
#if TIKU_BOARD_LED_COUNT >= 3
    { "led2",     TIKU_VFS_FILE, led2_read, led2_write, NULL, 0 },
#endif
#if TIKU_BOARD_LED_COUNT >= 4
    { "led3",     TIKU_VFS_FILE, led3_read, led3_write, NULL, 0 },
#endif
    { "console",  TIKU_VFS_FILE, console_read, console_write, NULL, 0 },
    { "null",     TIKU_VFS_FILE, devnull_read, devnull_write, NULL, 0 },
    { "zero",     TIKU_VFS_FILE, devzero_read, NULL, NULL, 0 },
    { "gpio",     TIKU_VFS_DIR,  NULL, NULL, gpio_children, GPIO_PORT_COUNT },
    { "gpio_dir", TIKU_VFS_DIR,  NULL, NULL, gpio_dir_children, GPIO_PORT_COUNT },
    { "uart",     TIKU_VFS_DIR,  NULL, NULL, dev_uart_children, 2 },
    { "adc",      TIKU_VFS_DIR,  NULL, NULL, dev_adc_children, 2 },
    { "i2c",      TIKU_VFS_DIR,  NULL, NULL, dev_i2c_children, 1 },
    { "spi",      TIKU_VFS_DIR,  NULL, NULL, dev_spi_children, 1 },
};

/** Mutable root children: sys + dev + proc (FRAM, written at init) */
static tiku_vfs_node_t __attribute__((section(".persistent")))
    root_children[3];

/** Mutable root node (FRAM, written at init) */
static tiku_vfs_node_t __attribute__((section(".persistent")))
    vfs_root;

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

void
tiku_vfs_tree_init(void)
{
    const tiku_vfs_node_t *proc;
    uint16_t mpu_saved;

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

    /* Capture reset cause before anything else clears it */
#ifdef PLATFORM_MSP430
    boot_reset_cause = SYSRSTIV;
#endif

    /* Boot count stays 0 unless hibernate module sets it.
     * tiku_mem_resume() requires a valid FRAM buffer and is called
     * separately by the application if hibernate is used. */
    boot_count_value = 0;

    /* Unlock FRAM — root_children, vfs_root, and /proc/ arrays
     * are in .persistent (FRAM) to conserve SRAM for the stack. */
    mpu_saved = tiku_mpu_unlock_nvm();

    /* Populate root entries */
    root_children[0] = (tiku_vfs_node_t){
        "sys", TIKU_VFS_DIR, NULL, NULL, sys_children, 12
    };
    root_children[1] = (tiku_vfs_node_t){
        "dev", TIKU_VFS_DIR, NULL, NULL, dev_children,
        sizeof(dev_children) / sizeof(dev_children[0])
    };

    /* Build and attach /proc/ (also writes to FRAM arrays) */
    proc = tiku_proc_vfs_get();
    root_children[2] = *proc;

    vfs_root = (tiku_vfs_node_t){
        "", TIKU_VFS_DIR, NULL, NULL, root_children, 3
    };

    tiku_mpu_lock_nvm(mpu_saved);

    tiku_vfs_init(&vfs_root);
}

void
tiku_vfs_set_boot_count(uint32_t count)
{
    boot_count_value = count;
}
