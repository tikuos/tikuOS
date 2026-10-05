/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_config.h - shell command flags and backend selection.
 *
 * One #ifndef flag per command, defaulted per platform and capability, then
 * forced off at the bottom of the file where the build lacks the hardware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * To add a command:
 *   1. Add a TIKU_SHELL_CMD_XXX flag here.
 *   2. Create kernel/shell/commands/tiku_shell_cmd_xxx.h and .c.
 *   3. Include the header and add a table entry in tiku_shell.c.
 *   4. Add the .c to the Makefile's TIKU_SHELL_ENABLE source list.
 *   5. If it drives hardware not every target has, add a rule to the
 *      HARDWARE REQUIREMENTS section at the bottom of this file so the
 *      flag resolves to 0 where the capability is absent.  Gate the
 *      whole .c on the resolved flag; no run-time "not available" stubs.
 */

#ifndef TIKU_SHELL_CONFIG_H_
#define TIKU_SHELL_CONFIG_H_

/** @defgroup TIKU_SHELL_CMDS CLI Command Flags
 * @brief Set to 1 to include a command, 0 to exclude.
 * @{
 */

/* Each flag is wrapped in #ifndef so EXTRA_CFLAGS can override it with
 * -DTIKU_SHELL_CMD_X=0 (or =1).  Set it there: the Makefile looks for that
 * string in EXTRA_CFLAGS to add or leave out some commands' .c files.  A
 * disabled command loses its table row, and the linker drops its
 * unreferenced code. */
#ifndef TIKU_SHELL_CMD_HELP
#define TIKU_SHELL_CMD_HELP    1  /**< help    - List available commands */
#endif
#ifndef TIKU_SHELL_CMD_PS
#define TIKU_SHELL_CMD_PS      1  /**< ps      - List active processes */
#endif
#ifndef TIKU_SHELL_CMD_INFO
#define TIKU_SHELL_CMD_INFO    1  /**< info    - System overview */
#endif
#ifndef TIKU_SHELL_CMD_HTIMER
#define TIKU_SHELL_CMD_HTIMER  1  /**< htimer  - Hardware-timer self-test */
#endif
#ifndef TIKU_SHELL_CMD_CONSOLE
#define TIKU_SHELL_CMD_CONSOLE 1  /**< console - Channels and counters */
#endif
#ifndef TIKU_SHELL_CMD_TIMER
#define TIKU_SHELL_CMD_TIMER   1  /**< timer   - Software timer status */
#endif
#ifndef TIKU_SHELL_CMD_KILL
#define TIKU_SHELL_CMD_KILL    1  /**< kill    - Stop a process */
#endif
#ifndef TIKU_SHELL_CMD_RESUME
#define TIKU_SHELL_CMD_RESUME  1  /**< resume  - Resume a stopped process */
#endif
#ifndef TIKU_SHELL_CMD_QUEUE
#define TIKU_SHELL_CMD_QUEUE   1  /**< queue   - List pending events */
#endif
#ifndef TIKU_SHELL_CMD_REBOOT
#define TIKU_SHELL_CMD_REBOOT  1  /**< reboot  - System reset */
#endif
#ifndef TIKU_SHELL_CMD_CPU1
/* Auto-on where the build carries the second-core driver; off elsewhere.
 * TIKU_DRV_CPU1_ENABLE is a -D from the Makefile, so this resolves the same
 * way in every translation unit regardless of include order. */
#if (TIKU_DRV_CPU1_ENABLE + 0)
#define TIKU_SHELL_CMD_CPU1    1  /**< cpu1    - the RA8P1 Cortex-M33 */
#else
#define TIKU_SHELL_CMD_CPU1    0
#endif
#endif
#ifndef TIKU_SHELL_CMD_NPU
/* On when the build includes the Ethos-U55 driver, which is opt-in on the
 * RA8P1 because its static buffers are large.  TIKU_HAS_NPU is a -D from the
 * Makefile, so this resolves the same way in every translation unit
 * regardless of include order. */
#if (TIKU_HAS_NPU + 0)
#define TIKU_SHELL_CMD_NPU     1  /**< npu     - the RA8P1 Ethos-U55 */
#else
#define TIKU_SHELL_CMD_NPU     0
#endif
#endif
#ifndef TIKU_SHELL_CMD_CAM
/* Follows the driver opt-in: the camera is an expansion board. */
#if (TIKU_HAS_CAM + 0)
#define TIKU_SHELL_CMD_CAM     1  /**< cam     - camera bring-up/capture */
#else
#define TIKU_SHELL_CMD_CAM     0
#endif
#endif
#ifndef TIKU_SHELL_CMD_PANEL
/* Follows the driver opt-in: the parallel RGB panel is an expansion board,
 * and a build without the display controller has nothing to drive. */
#if (TIKU_HAS_GLCDC + 0)
#define TIKU_SHELL_CMD_PANEL   1  /**< panel   - the parallel RGB display */
#else
#define TIKU_SHELL_CMD_PANEL   0
#endif
#endif
#ifndef TIKU_SHELL_CMD_TRNG
#define TIKU_SHELL_CMD_TRNG    1  /**< trng    - Dump hardware TRNG bytes */
#endif
#ifndef TIKU_SHELL_CMD_USBHS
/* Auto-on where the build carries the USB-HS device driver; off elsewhere.
 * TIKU_DRV_USBHS_ENABLE is a -D from the Makefile, so this resolves the same
 * way in every translation unit regardless of include order. */
#if (TIKU_DRV_USBHS_ENABLE + 0)
#define TIKU_SHELL_CMD_USBHS   1  /**< usb, store - device disk + model store */
#else
#define TIKU_SHELL_CMD_USBHS   0
#endif
#endif
#ifndef TIKU_SHELL_CMD_XFLASH
/* Auto-on where an XSPI NOR is wired; off elsewhere. */
#if defined(PLATFORM_STM32N6)
#define TIKU_SHELL_CMD_XFLASH  1  /**< xflash - external NOR over XSPI */
#else
#define TIKU_SHELL_CMD_XFLASH  0
#endif
#endif
#ifndef TIKU_SHELL_CMD_CACHE
/* Auto-on where the port owns the core's caches; off elsewhere. */
#if defined(PLATFORM_STM32N6)
#define TIKU_SHELL_CMD_CACHE   1  /**< cache - CPU cache state/toggle/bench */
#else
#define TIKU_SHELL_CMD_CACHE   0
#endif
#endif
#ifndef TIKU_SHELL_CMD_DIAG
/* Auto-on where the port owns fault/EXTI/watchdog silicon; off elsewhere. */
#if defined(PLATFORM_STM32N6) || defined(PLATFORM_RA8P1) || \
    defined(PLATFORM_ESP32C61)
#define TIKU_SHELL_CMD_DIAG    1  /**< diag - faults, EXTI and the watchdog */
#else
#define TIKU_SHELL_CMD_DIAG    0
#endif
#endif
#ifndef TIKU_SHELL_CMD_HISTORY
#define TIKU_SHELL_CMD_HISTORY 1  /**< history - Recall the last N commands */
#endif
#ifndef TIKU_SHELL_CMD_SDRAM
/* Auto-on where the board wires external SDRAM to the bus. */
#if defined(PLATFORM_RA8P1)
#define TIKU_SHELL_CMD_SDRAM   1  /**< sdram - bring up, attach, bench */
#else
#define TIKU_SHELL_CMD_SDRAM   0
#endif
#endif
#ifndef TIKU_SHELL_CMD_MRAMBENCH
/* Auto-on on Ambiq (benches the Ambiq bootrom MRAM programmer); off
 * elsewhere. The .c is only compiled on Ambiq (Makefile-gated). */
#if defined(PLATFORM_AMBIQ)
#define TIKU_SHELL_CMD_MRAMBENCH 1  /**< mrambench - Time the MRAM programmer */
#else
#define TIKU_SHELL_CMD_MRAMBENCH 0
#endif
#endif
#ifndef TIKU_SHELL_CMD_FAT
/* Auto-on wherever the eMMC driver is built: the card is the only FAT32
 * volume in the system, so the command has nothing to read without it. */
#if defined(TIKU_DRV_EMMC_ENABLE)
#define TIKU_SHELL_CMD_FAT 1  /**< fat - read the card's FAT32 volume */
#else
#define TIKU_SHELL_CMD_FAT 0
#endif
#endif
#ifndef TIKU_SHELL_CMD_BLE
/* Auto-on for the EM9305 BLE build (apollo510b); the "ble" command probes
 * the EM9305, advertises a beacon and runs a shell session over BLE UART.
 * The .c + the driver are only compiled when TIKU_DRV_BLE_EM9305_ENABLE is
 * set (Makefile-gated). */
#if defined(TIKU_DRV_BLE_EM9305_ENABLE)
#define TIKU_SHELL_CMD_BLE 1  /**< ble - EM9305 probe, beacon, BLE shell */
#else
#define TIKU_SHELL_CMD_BLE 0
#endif
#endif
#ifndef TIKU_SHELL_CMD_WIFI
/* Auto-on when a Wi-Fi driver (CYW43 or ESP32-C61) is enabled; otherwise
 * off. Override with -DTIKU_SHELL_CMD_WIFI=0 to drop the command. */
#if (defined(TIKU_DRV_WIFI_CYW43_ENABLE) && (TIKU_DRV_WIFI_CYW43_ENABLE == 1)) || \
    (defined(TIKU_DRV_WIFI_ESP_ENABLE) && (TIKU_DRV_WIFI_ESP_ENABLE == 1))
#define TIKU_SHELL_CMD_WIFI    1  /**< wifi    - Wi-Fi radio control */
#else
#define TIKU_SHELL_CMD_WIFI    0
#endif
#endif
#ifndef TIKU_SHELL_CMD_BT
/* Auto-on with a radio under the BLE host stack (the CYW43's BT extension,
 * the ESP32-C61's controller); otherwise off. */
#if (defined(TIKU_DRV_WIFI_CYW43_BT_ENABLE) && \
     (TIKU_DRV_WIFI_CYW43_BT_ENABLE == 1)) || \
    (defined(TIKU_DRV_BLE_ESP_ENABLE) && (TIKU_DRV_BLE_ESP_ENABLE == 1))
#define TIKU_SHELL_CMD_BT      1  /**< bt      - BLE host radio status */
#else
#define TIKU_SHELL_CMD_BT      0
#endif
#endif
#ifndef TIKU_SHELL_CMD_SDR
/* Auto-on with the ESP32-C61's radio built as a receiver (raw I/Q). */
#if defined(TIKU_DRV_SDR_ESP_ENABLE) && (TIKU_DRV_SDR_ESP_ENABLE == 1)
#define TIKU_SHELL_CMD_SDR     1  /**< sdr     - Radio I/Q snapshots */
#else
#define TIKU_SHELL_CMD_SDR     0
#endif
#endif
#ifndef TIKU_SHELL_CMD_LS
#define TIKU_SHELL_CMD_LS      1  /**< ls      - List VFS directory contents */
#endif
#ifndef TIKU_SHELL_CMD_CD
#define TIKU_SHELL_CMD_CD      1  /**< cd/pwd  - Change/print the cwd */
#endif
#ifndef TIKU_SHELL_CMD_TOGGLE
#define TIKU_SHELL_CMD_TOGGLE  1  /**< toggle  - Binary state flip via VFS */
#endif
#ifndef TIKU_SHELL_CMD_START
#define TIKU_SHELL_CMD_START   1  /**< start   - Start/resume a process */
#endif
#ifndef TIKU_SHELL_CMD_WRITE
#define TIKU_SHELL_CMD_WRITE   1  /**< write   - Write value to VFS node */
#endif
#ifndef TIKU_SHELL_CMD_FS
#define TIKU_SHELL_CMD_FS      1  /**< rm, touch, mkdir, rmdir, recv, send */
#endif
#ifndef TIKU_SHELL_CMD_NVMPROBE
#define TIKU_SHELL_CMD_NVMPROBE 0 /**< nvmprobe - NVM region diagnostic */
#endif
#ifndef TIKU_SHELL_CMD_CRYPTOPROBE
#define TIKU_SHELL_CMD_CRYPTOPROBE 0 /**< cryptoprobe - CRACEN probe */
#endif
#ifndef TIKU_SHELL_CMD_BLEADV
#define TIKU_SHELL_CMD_BLEADV 0 /**< bleadv - nRF54L BLE beacon */
#endif
#ifndef TIKU_SHELL_CMD_RADIO154
#define TIKU_SHELL_CMD_RADIO154 0 /**< radio154 - 802.15.4 PHY test */
#endif
#ifndef TIKU_SHELL_CMD_RFTEST
#define TIKU_SHELL_CMD_RFTEST 0 /**< rftest - RF test carrier, bench only */
#endif
#ifndef TIKU_SHELL_CMD_AXONSPROBE
#define TIKU_SHELL_CMD_AXONSPROBE 0 /**< axonsprobe - Axon NPU probe */
#endif
#ifndef TIKU_SHELL_CMD_USBMSC
#if defined(TIKU_USBHS_MSC)
#define TIKU_SHELL_CMD_USBMSC 1  /**< usbmsc - present the board as a disk */
#else
#define TIKU_SHELL_CMD_USBMSC 0
#endif
#endif

#ifndef TIKU_SHELL_CMD_USBPROBE
#define TIKU_SHELL_CMD_USBPROBE 0 /**< usbprobe - USB high-speed probe */
#endif
#ifndef TIKU_SHELL_CMD_READ
#define TIKU_SHELL_CMD_READ    1  /**< read    - Read value from VFS node */
#endif
#ifndef TIKU_SHELL_CMD_GPIO
#define TIKU_SHELL_CMD_GPIO    1  /**< gpio    - Direct GPIO pin control */
#endif
#ifndef TIKU_SHELL_CMD_ADC
#define TIKU_SHELL_CMD_ADC     1  /**< adc     - Read analog channels */
#endif
#ifndef TIKU_SHELL_CMD_FREE
#define TIKU_SHELL_CMD_FREE    1  /**< free    - Memory usage summary */
#endif
#ifndef TIKU_SHELL_CMD_DF
#define TIKU_SHELL_CMD_DF      1  /**< df      - /data file-store usage */
#endif
#ifndef TIKU_SHELL_CMD_LAYOUT
#define TIKU_SHELL_CMD_LAYOUT  1  /**< layout  - memory budgets for next boot */
#endif
#ifndef TIKU_SHELL_CMD_SLEEP
#define TIKU_SHELL_CMD_SLEEP   1  /**< sleep   - Enter low-power idle mode */
#endif
#ifndef TIKU_SHELL_CMD_WAKE
#define TIKU_SHELL_CMD_WAKE    1  /**< wake    - Show active wake sources */
#endif
#ifndef TIKU_SHELL_CMD_POWER
#define TIKU_SHELL_CMD_POWER   1  /**< power   - Cache/DCDC/idle power knobs */
#endif
#ifndef TIKU_SHELL_CMD_FREQ
#define TIKU_SHELL_CMD_FREQ    1  /**< freq    - Show/set CPU core frequency */
#endif
#ifndef TIKU_SHELL_CMD_NAME
#define TIKU_SHELL_CMD_NAME    1  /**< name    - Read or set device name */
#endif
/* `if` is opt-in: EXTRA_CFLAGS="-DTIKU_SHELL_CMD_IF=1". */
#ifndef TIKU_SHELL_CMD_IF
#define TIKU_SHELL_CMD_IF      0  /**< if      - Conditional VFS action */
#endif
#ifndef TIKU_SHELL_CMD_IRQ
#define TIKU_SHELL_CMD_IRQ     1  /**< irq     - GPIO edge interrupt -> event */
#endif
#ifndef TIKU_SHELL_CMD_ALIAS
#define TIKU_SHELL_CMD_ALIAS   1  /**< alias   - Durable shell shortcuts */
#endif
#ifndef TIKU_SHELL_CMD_CAT
#define TIKU_SHELL_CMD_CAT     1  /**< cat     - Alias for read */
#endif
#ifndef TIKU_SHELL_CMD_ECHO
#define TIKU_SHELL_CMD_ECHO    1  /**< echo    - Print arguments + newline */
#endif
/* `lcd` drives a segment LCD wired to the LCD_C peripheral, which the
 * FR6989 LaunchPad has.  It defaults from TIKU_BOARD_HAS_LCD, a Makefile
 * BOARD_CAPS -D visible in every translation unit; -DTIKU_SHELL_CMD_LCD=0
 * drops it, and the rule at the bottom clears it on a board without one. */
#ifndef TIKU_SHELL_CMD_LCD
#  if defined(TIKU_BOARD_HAS_LCD) && TIKU_BOARD_HAS_LCD
#    define TIKU_SHELL_CMD_LCD 1  /**< lcd     - Drive segment-LCD interface */
#  else
#    define TIKU_SHELL_CMD_LCD 0
#  endif
#endif
#ifndef TIKU_SHELL_CMD_WATCH
#define TIKU_SHELL_CMD_WATCH   1  /**< watch   - Live VFS view until Ctrl+C */
#endif
/* slip: carry SLIP/IP on the console line beside the shell's text.  Auto-on
 * only with the net stack (TIKU_KIT_NET_ENABLE=1), whose SLIP link and IPv4
 * input the command uses. */
#ifndef TIKU_SHELL_CMD_SLIP
#if defined(TIKU_KIT_NET_ENABLE) && TIKU_KIT_NET_ENABLE
#define TIKU_SHELL_CMD_SLIP    1  /**< slip    - SLIP/IP on the console line */
#else
#define TIKU_SHELL_CMD_SLIP    0
#endif
#endif
/* ping: ICMP echo over SLIP.  Same gating as slip (needs the net stack). */
#ifndef TIKU_SHELL_CMD_PING
#if defined(TIKU_KIT_NET_ENABLE) && TIKU_KIT_NET_ENABLE
#define TIKU_SHELL_CMD_PING    1  /**< ping    - ICMP echo a host over SLIP */
#else
#define TIKU_SHELL_CMD_PING    0
#endif
#endif
/* ip: print the device's IPv4 address.  Same gating as slip/ping. */
#ifndef TIKU_SHELL_CMD_IP
#if defined(TIKU_KIT_NET_ENABLE) && TIKU_KIT_NET_ENABLE
#define TIKU_SHELL_CMD_IP      1  /**< ip      - Print the IPv4 address */
#else
#define TIKU_SHELL_CMD_IP      0
#endif
#endif
/* ntp: fetch wall-clock time over SLIP (SNTP).  Same gating as slip/ping/ip;
 * the Makefile pulls in the time kit (TIKU_KIT_TIME_ENABLE) with it. */
#ifndef TIKU_SHELL_CMD_NTP
#if defined(TIKU_KIT_NET_ENABLE) && TIKU_KIT_NET_ENABLE
#define TIKU_SHELL_CMD_NTP     1  /**< ntp     - Fetch network time (SNTP) */
#else
#define TIKU_SHELL_CMD_NTP     0
#endif
#endif
/* dns: resolve a hostname (A record) over SLIP.  Same gating as slip/ping/ip;
 * the net kit compiles the DNS stub resolver. */
#ifndef TIKU_SHELL_CMD_DNS
#if defined(TIKU_KIT_NET_ENABLE) && TIKU_KIT_NET_ENABLE
#define TIKU_SHELL_CMD_DNS     1  /**< dns     - Resolve a hostname (A) */
#else
#define TIKU_SHELL_CMD_DNS     0
#endif
#endif
/* syslog: send a remote log line (UDP 514) over SLIP.  On with the net kit
 * except in a MIN build (TIKU_KIT_NET_MIN), which does not compile the syslog
 * client (tiku_kits_net_syslog.c): the command would not link there. */
#ifndef TIKU_SHELL_CMD_SYSLOG
#if defined(TIKU_KIT_NET_ENABLE) && TIKU_KIT_NET_ENABLE && \
    !(defined(TIKU_KIT_NET_MIN) && TIKU_KIT_NET_MIN)
#define TIKU_SHELL_CMD_SYSLOG  1  /**< syslog  - Send a remote log line (514) */
#else
#define TIKU_SHELL_CMD_SYSLOG  0
#endif
#endif
/* mqtt: connect/publish to an MQTT broker over SLIP and TCP.  On when the
 * MQTT kit is (TIKU_KITS_NET_MQTT_ENABLE).  The Makefile compiles the command
 * only in TIKU_SHELL_NET_TEST builds and passes -DTIKU_SHELL_CMD_MQTT=0 to
 * any other build that enables the kit. */
#ifndef TIKU_SHELL_CMD_MQTT
#if defined(TIKU_KITS_NET_MQTT_ENABLE) && TIKU_KITS_NET_MQTT_ENABLE
#define TIKU_SHELL_CMD_MQTT    1  /**< mqtt    - MQTT connect/publish */
#else
#define TIKU_SHELL_CMD_MQTT    0
#endif
#endif
#ifndef TIKU_SHELL_CMD_CALC
#define TIKU_SHELL_CMD_CALC    1  /**< calc    - Integer arithmetic */
#endif
/* `basic` is on by default on Ambiq and opt-in elsewhere: the Makefile's
 * TIKU_SHELL_BASIC_ENABLE=1 sets this flag.  On MSP430 the Makefile refuses
 * BASIC without MEMORY_MODEL=large:
 *
 *   make MCU=msp430fr5994 TIKU_SHELL_ENABLE=1 \
 *        TIKU_SHELL_BASIC_ENABLE=1 MEMORY_MODEL=large
 *
 * Program capacity is TIKU_BASIC_PROGRAM_LINES (tiku_basic_config.h). */
#ifndef TIKU_SHELL_CMD_BASIC
#define TIKU_SHELL_CMD_BASIC   0  /**< basic   - Tiku BASIC interpreter REPL */
#endif
#ifndef TIKU_SHELL_CMD_JOBS
#define TIKU_SHELL_CMD_JOBS    1  /**< jobs    - every/once/jobs */
#endif
#ifndef TIKU_SHELL_CMD_RULES
#define TIKU_SHELL_CMD_RULES   1  /**< rules   - on/rules */
#endif
#ifndef TIKU_SHELL_CMD_CHANGED
#define TIKU_SHELL_CMD_CHANGED 1  /**< changed - Wait for a VFS change */
#endif
/* I2C is opt-in: it pulls tiku_i2c_bus and the arch I2C driver into the
 * image.  The Makefile compiles the command only when enabled with
 *   EXTRA_CFLAGS="-DTIKU_SHELL_CMD_I2C=1" */
#ifndef TIKU_SHELL_CMD_I2C
#define TIKU_SHELL_CMD_I2C    0  /**< i2c    - Bus scan / read / write */
#endif
#ifndef TIKU_SHELL_CMD_TREE
#define TIKU_SHELL_CMD_TREE   1  /**< tree   - Recursive VFS dump */
#endif
#ifndef TIKU_SHELL_CMD_CLEAR
#define TIKU_SHELL_CMD_CLEAR  1  /**< clear  - ANSI clear screen */
#endif

/* Scripting and debugging extras, each enabled per build through
 * EXTRA_CFLAGS; the flags are independent, and the Makefile compiles each
 * command only when its flag is set to 1.  Example:
 *   make MCU=msp430fr5994 TIKU_SHELL_ENABLE=1 \
 *        EXTRA_CFLAGS="-DTIKU_SHELL_CMD_DELAY=1 -DTIKU_SHELL_CMD_REPEAT=1"
 */
#ifndef TIKU_SHELL_CMD_DELAY
#define TIKU_SHELL_CMD_DELAY  0  /**< delay  - Synchronous ms wait */
#endif
#ifndef TIKU_SHELL_CMD_REPEAT
#define TIKU_SHELL_CMD_REPEAT 0  /**< repeat - Run command N times */
#endif
#ifndef TIKU_SHELL_CMD_PEEK
#define TIKU_SHELL_CMD_PEEK   0  /**< peek   - Read raw memory */
#endif
#ifndef TIKU_SHELL_CMD_POKE
#define TIKU_SHELL_CMD_POKE   0  /**< poke   - Write raw memory */
#endif
#ifndef TIKU_SHELL_CMD_INIT
#define TIKU_SHELL_CMD_INIT    TIKU_INIT_ENABLE  /**< init - NVM boot entries */
#endif

/** @} */

/** @defgroup TIKU_SHELL_COLOR ANSI Color Output
 * @brief Enable colored shell output via ANSI escape codes.
 *
 * Build with:  make TIKU_SHELL_COLOR=1 MCU=msp430fr5994
 *
 * Requires a terminal that renders ANSI escapes (picocom, screen,
 * minicom, PuTTY, telnet).  Disable for raw serial logging.
 * @{
 */

#ifndef TIKU_SHELL_COLOR
#define TIKU_SHELL_COLOR  0
#endif

#if TIKU_SHELL_COLOR

#define SH_RST     "\033[0m"      /**< Reset all attributes */
#define SH_BOLD    "\033[1m"      /**< Bold / bright */
#define SH_DIM     "\033[2m"      /**< Dim / faint */

#define SH_RED     "\033[31m"     /**< Red text */
#define SH_GREEN   "\033[32m"     /**< Green text */
#define SH_YELLOW  "\033[33m"     /**< Yellow text */
#define SH_BLUE    "\033[34m"     /**< Blue text */
#define SH_MAGENTA "\033[35m"     /**< Magenta text */
#define SH_CYAN    "\033[36m"     /**< Cyan text */
#define SH_WHITE   "\033[37m"     /**< White text */

#else /* !TIKU_SHELL_COLOR */

#define SH_RST     ""
#define SH_BOLD    ""
#define SH_DIM     ""
#define SH_RED     ""
#define SH_GREEN   ""
#define SH_YELLOW  ""
#define SH_BLUE    ""
#define SH_MAGENTA ""
#define SH_CYAN    ""
#define SH_WHITE   ""

#endif /* TIKU_SHELL_COLOR */

/** @} */

/** @defgroup TIKU_SHELL_BACKENDS CLI Backend Selection
 * @brief Enable optional I/O backends (UART is always available).
 * @{
 */

/**
 * @brief Enable the TCP (telnet) backend on port 23.
 *
 * TIKU_SHELL_NET_TEST=1 sets it and is the only build that compiles
 * tiku_shell_io_tcp.c.  Set without net-test, it selects a TCP-only shell (no
 * local console; a net process owns SLIP) that no Makefile path builds.
 *
 * @note Needs the TikuKits TCP stack (TIKU_KITS_NET_TCP_ENABLE=1).
 */
#ifndef TIKU_SHELL_TCP_ENABLE
#define TIKU_SHELL_TCP_ENABLE 0
#endif

/**
 * @brief Bring the net test servers (UDP echo, TCP, CoAP) up in the shell.
 *
 * Off by default.  When set, the shell starts UDP and TCP, registers the CoAP
 * server and listens for telnet; the slip command's console channel feeds
 * tiku_kits_net_ipv4_input(), which dispatches to them.
 *
 * @note No net process is started: the console, pumped by the shell, owns
 *       the wire, and a second reader would take its bytes.  The TikuBench
 *       net suite uses this build.
 */
#ifndef TIKU_SHELL_NET_TEST
#define TIKU_SHELL_NET_TEST 0
#endif

/** @} */

/*---------------------------------------------------------------------------*/
/* HARDWARE REQUIREMENTS                                                     */
/*---------------------------------------------------------------------------*/
/*
 * A command that drives hardware the build does not have is forced off
 * here, after every default and user override above.  This is the one
 * place where "command X needs capability Y" is written down; the shell
 * table, the command .c and the help listing all read the resolved
 * flag, so a `-DTIKU_SHELL_CMD_X=1` on a target without the hardware
 * yields a build without the command -- no stub in `help` and no
 * undefined reference at link.
 *
 * The Makefile mirrors these rules where it owns the decision to
 * compile the command's .c at all, and prints a warning when a command
 * was requested on a target that cannot have it.
 *
 * Capability macros used here are -D flags from the Makefile, except
 * USBPROBE's TIKU_DEVICE_HAS_USBHS: a device-header macro, meaningful only
 * in translation units that include tiku.h first, which tiku_shell.c does.
 * Keep each rule to the same three-line shape.
 */

/* rftest drives the on-die 2.4 GHz RADIO. */
#if TIKU_SHELL_CMD_RFTEST && !(TIKU_HAS_BLE_ADV + 0)
#undef  TIKU_SHELL_CMD_RFTEST
#define TIKU_SHELL_CMD_RFTEST 0
#endif
/* usbprobe drives the USB high-speed block directly. */
#if TIKU_SHELL_CMD_USBPROBE && !(TIKU_DEVICE_HAS_USBHS + 0)
#undef  TIKU_SHELL_CMD_USBPROBE
#define TIKU_SHELL_CMD_USBPROBE 0
#endif

/* bleadv drives the on-die 2.4 GHz RADIO (broadcast BLE). */
#if TIKU_SHELL_CMD_BLEADV && !(TIKU_HAS_BLE_ADV + 0)
#undef  TIKU_SHELL_CMD_BLEADV
#define TIKU_SHELL_CMD_BLEADV 0
#endif

/* radio154 drives the 802.15.4 PHY. */
#if TIKU_SHELL_CMD_RADIO154 && !(TIKU_HAS_154 + 0)
#undef  TIKU_SHELL_CMD_RADIO154
#define TIKU_SHELL_CMD_RADIO154 0
#endif

/* cryptoprobe drives the CRACEN CryptoMaster. */
#if TIKU_SHELL_CMD_CRYPTOPROBE && !(TIKU_CRACEN_PK_ENABLE + 0)
#undef  TIKU_SHELL_CMD_CRYPTOPROBE
#define TIKU_SHELL_CMD_CRYPTOPROBE 0
#endif

/* wifi drives a Wi-Fi driver, bt the BLE host stack's radio (CYW43439,
 * ESP32-C61); ble drives the EM9305. */
#if TIKU_SHELL_CMD_WIFI && !(TIKU_DRV_WIFI_CYW43_ENABLE + 0) && \
    !(TIKU_DRV_WIFI_ESP_ENABLE + 0)
#undef  TIKU_SHELL_CMD_WIFI
#define TIKU_SHELL_CMD_WIFI 0
#endif
#if TIKU_SHELL_CMD_BT && !(TIKU_DRV_WIFI_CYW43_BT_ENABLE + 0) && \
    !(TIKU_DRV_BLE_ESP_ENABLE + 0)
#undef  TIKU_SHELL_CMD_BT
#define TIKU_SHELL_CMD_BT 0
#endif
#if TIKU_SHELL_CMD_BLE && !(TIKU_DRV_BLE_EM9305_ENABLE + 0)
#undef  TIKU_SHELL_CMD_BLE
#define TIKU_SHELL_CMD_BLE 0
#endif

/* layout divides the carved NVM region, which MSP430 does not have. */
#if TIKU_SHELL_CMD_LAYOUT && defined(PLATFORM_MSP430)
#undef  TIKU_SHELL_CMD_LAYOUT
#define TIKU_SHELL_CMD_LAYOUT 0
#endif

/* mrambench drives the Ambiq bootrom MRAM programmer. */
#if TIKU_SHELL_CMD_MRAMBENCH && !defined(PLATFORM_AMBIQ)
#undef  TIKU_SHELL_CMD_MRAMBENCH
#define TIKU_SHELL_CMD_MRAMBENCH 0
#endif

/* lcd drives the segment-LCD controller (a board capability). */
#if TIKU_SHELL_CMD_LCD && !(TIKU_BOARD_HAS_LCD + 0)
#undef  TIKU_SHELL_CMD_LCD
#define TIKU_SHELL_CMD_LCD 0
#endif

/* axonsprobe (Axon NPU) has no rule here: its capability macro
 * TIKU_DEVICE_HAS_AXONS is in the device header, which
 * tiku_shell_cmd_axonsprobe.h includes after this file, so a rule here
 * would clear the flag in that translation unit only.  The Makefile
 * compiles the command only for nrf54lm20b. */

#endif /* TIKU_SHELL_CONFIG_H_ */
