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
#include <kernel/cpu/tiku_common.h>
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

static const tiku_vfs_node_t sys_children[] = {
    { "version", TIKU_VFS_FILE, version_read, NULL, NULL, 0 },
    { "device",  TIKU_VFS_FILE, device_read,  NULL, NULL, 0 },
    { "uptime",  TIKU_VFS_FILE, uptime_read,  NULL, NULL, 0 },
    { "mem",     TIKU_VFS_DIR,  NULL, NULL, sys_mem_children, 2 },
    { "cpu",     TIKU_VFS_DIR,  NULL, NULL, sys_cpu_children, 1 },
    { "power",   TIKU_VFS_DIR,  NULL, NULL, sys_power_children, 2 },
};

static const tiku_vfs_node_t dev_children[] = {
    { "led0", TIKU_VFS_FILE, led0_read, led0_write, NULL, 0 },
    { "led1", TIKU_VFS_FILE, led1_read, led1_write, NULL, 0 },
};

static const tiku_vfs_node_t root_children[] = {
    { "sys", TIKU_VFS_DIR, NULL, NULL, sys_children, 6 },
    { "dev", TIKU_VFS_DIR, NULL, NULL, dev_children, 2 },
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
