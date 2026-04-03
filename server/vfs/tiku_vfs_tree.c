/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree.c - System VFS tree with /sys, /dev, /proc
 *
 * Builds the production VFS tree and calls tiku_vfs_init().
 * LED nodes drive real hardware via tiku_common_led*() API.
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
#include <kernel/cpu/tiku_common.h>
#include <arch/msp430/tiku_gpio_arch.h>
#include <stdio.h>

#if TIKU_SHELL_ENABLE
#include <kernel/shell/commands/tiku_shell_cmd_sleep.h>
#endif

#ifdef PLATFORM_MSP430
#include <msp430.h>
#endif

/*---------------------------------------------------------------------------*/
/* LED STATE TRACKING                                                        */
/*---------------------------------------------------------------------------*/

static uint8_t led0_state;
static uint8_t led1_state;

/*---------------------------------------------------------------------------*/
/* /dev/led0 — LED1 (Red on FR5969 LaunchPad)                                */
/*---------------------------------------------------------------------------*/

static int
led0_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", led0_state);
}

static int
led0_write(const char *buf, size_t len)
{
    (void)len;
    if (buf[0] == '1') {
        tiku_common_led1_on();
        led0_state = 1;
    } else if (buf[0] == '0') {
        tiku_common_led1_off();
        led0_state = 0;
    } else if (buf[0] == 't') {
        /* "toggle" shorthand */
        tiku_common_led1_toggle();
        led0_state = !led0_state;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* /dev/led1 — LED2 (Green on FR5969 LaunchPad)                              */
/*---------------------------------------------------------------------------*/

static int
led1_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", led1_state);
}

static int
led1_write(const char *buf, size_t len)
{
    (void)len;
    if (buf[0] == '1') {
        tiku_common_led2_on();
        led1_state = 1;
    } else if (buf[0] == '0') {
        tiku_common_led2_off();
        led1_state = 0;
    } else if (buf[0] == 't') {
        tiku_common_led2_toggle();
        led1_state = !led1_state;
    }
    return 0;
}

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
 *     ├── led0     (read-write)
 *     └── led1     (read-write)
 */

static const tiku_vfs_node_t sys_mem_children[] = {
    { "sram", TIKU_VFS_FILE, sram_read, NULL, NULL, 0 },
    { "nvm",  TIKU_VFS_FILE, nvm_read,  NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_cpu_children[] = {
    { "freq", TIKU_VFS_FILE, cpu_freq_read, NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_power_children[] = {
    { "mode", TIKU_VFS_FILE, power_mode_read, NULL, NULL, 0 },
    { "wake", TIKU_VFS_FILE, power_wake_read, NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_timer_children[] = {
    { "count", TIKU_VFS_FILE, timer_count_read, NULL, NULL, 0 },
    { "next",  TIKU_VFS_FILE, timer_next_read,  NULL, NULL, 0 },
    { "fired", TIKU_VFS_FILE, timer_fired_read, NULL, NULL, 0 },
};

static const tiku_vfs_node_t sys_children[] = {
    { "version", TIKU_VFS_FILE, version_read, NULL, NULL, 0 },
    { "device",  TIKU_VFS_FILE, device_read,  NULL, NULL, 0 },
    { "uptime",  TIKU_VFS_FILE, uptime_read,  NULL, NULL, 0 },
    { "mem",     TIKU_VFS_DIR,  NULL, NULL, sys_mem_children, 2 },
    { "cpu",     TIKU_VFS_DIR,  NULL, NULL, sys_cpu_children, 1 },
    { "power",   TIKU_VFS_DIR,  NULL, NULL, sys_power_children, 2 },
    { "timer",   TIKU_VFS_DIR,  NULL, NULL, sys_timer_children, 3 },
};

static const tiku_vfs_node_t dev_children[] = {
    { "led0", TIKU_VFS_FILE, led0_read, led0_write, NULL, 0 },
    { "led1", TIKU_VFS_FILE, led1_read, led1_write, NULL, 0 },
    { "gpio", TIKU_VFS_DIR,  NULL, NULL, gpio_children, GPIO_PORT_COUNT },
};

static const tiku_vfs_node_t root_children[] = {
    { "sys", TIKU_VFS_DIR, NULL, NULL, sys_children, 7 },
    { "dev", TIKU_VFS_DIR, NULL, NULL, dev_children, 3 },
};

static const tiku_vfs_node_t vfs_root = {
    "", TIKU_VFS_DIR, NULL, NULL, root_children, 2
};

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

void
tiku_vfs_tree_init(void)
{
    /* Init LED hardware */
    tiku_common_led1_init();
    tiku_common_led2_init();

    led0_state = 0;
    led1_state = 0;

    tiku_vfs_init(&vfs_root);
}
