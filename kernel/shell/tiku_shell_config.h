/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_config.h - CLI command selection flags
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file tiku_shell_config.h
 * @brief Enable/disable individual CLI commands
 *
 * To add a new command:
 *   1. Add a TIKU_SHELL_CMD_xxx flag here (set to 1)
 *   2. Create kernel/shell/commands/tiku_shell_cmd_xxx.h and .c
 *   3. Add #include and table entry in tiku_shell.c
 *   4. Add the .c file to the Makefile TIKU_SHELL_ENABLE section
 */

#ifndef TIKU_SHELL_CONFIG_H_
#define TIKU_SHELL_CONFIG_H_

/** @defgroup TIKU_SHELL_CMDS CLI Command Flags
 * @brief Set to 1 to include a command, 0 to exclude.
 * @{
 */

/* Each flag is wrapped in #ifndef so the build system can override
 * with -DTIKU_SHELL_CMD_X=0 (or =1) on the command line via
 * EXTRA_CFLAGS.  The Makefile gates the matching .c on findstring
 * checks against EXTRA_CFLAGS so the disabled command compiles to
 * zero text. */
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
#ifndef TIKU_SHELL_CMD_TRNG
#define TIKU_SHELL_CMD_TRNG    1  /**< trng    - Dump hardware TRNG bytes */
#endif
#ifndef TIKU_SHELL_CMD_HISTORY
#define TIKU_SHELL_CMD_HISTORY 1  /**< history - Last N commands from FRAM */
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
#ifndef TIKU_SHELL_CMD_BLE
/* Auto-on for the EM9305 BLE build (apollo510b); the "ble" command runs the
 * radio first-contact self-test. The .c + the driver are only compiled when
 * TIKU_DRV_BLE_EM9305_ENABLE is set (Makefile-gated). */
#if defined(TIKU_DRV_BLE_EM9305_ENABLE)
#define TIKU_SHELL_CMD_BLE 1  /**< ble - EM9305 radio first-contact probe */
#else
#define TIKU_SHELL_CMD_BLE 0
#endif
#endif
#ifndef TIKU_SHELL_CMD_WIFI
/* Auto-on when the CYW43 driver is enabled; otherwise off. Override
 * with -DTIKU_SHELL_CMD_WIFI=0 to drop the command from the table. */
#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && (TIKU_DRV_WIFI_CYW43_ENABLE == 1)
#define TIKU_SHELL_CMD_WIFI    1
#else
#define TIKU_SHELL_CMD_WIFI    0
#endif
#endif
#ifndef TIKU_SHELL_CMD_BT
/* Auto-on when the CYW43 BT extension is enabled; otherwise off. */
#if defined(TIKU_DRV_WIFI_CYW43_BT_ENABLE) && (TIKU_DRV_WIFI_CYW43_BT_ENABLE == 1)
#define TIKU_SHELL_CMD_BT      1
#else
#define TIKU_SHELL_CMD_BT      0
#endif
#endif
#ifndef TIKU_SHELL_CMD_LS
#define TIKU_SHELL_CMD_LS      1  /**< ls      - List VFS directory contents */
#endif
#ifndef TIKU_SHELL_CMD_CD
#define TIKU_SHELL_CMD_CD      1  /**< cd/pwd  - Change/print working directory */
#endif
#ifndef TIKU_SHELL_CMD_TOGGLE
#define TIKU_SHELL_CMD_TOGGLE  1  /**< toggle  - Binary state flip via VFS */
#endif
#ifndef TIKU_SHELL_CMD_START
#define TIKU_SHELL_CMD_START   1  /**< start   - Launch/resume process by name */
#endif
#ifndef TIKU_SHELL_CMD_WRITE
#define TIKU_SHELL_CMD_WRITE   1  /**< write   - Write value to VFS node */
#endif
#ifndef TIKU_SHELL_CMD_FS
#define TIKU_SHELL_CMD_FS      1  /**< rm/touch- Remove / create /data files */
#endif
#ifndef TIKU_SHELL_CMD_NVMPROBE
#define TIKU_SHELL_CMD_NVMPROBE 0 /**< nvmprobe- Carved NVM region diagnostic (opt-in) */
#endif
#ifndef TIKU_SHELL_CMD_CRYPTOPROBE
#define TIKU_SHELL_CMD_CRYPTOPROBE 0 /**< cryptoprobe- CRACEN bring-up probe (opt-in) */
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
#ifndef TIKU_SHELL_CMD_SLEEP
#define TIKU_SHELL_CMD_SLEEP   1  /**< sleep   - Enter low-power idle mode */
#endif
#ifndef TIKU_SHELL_CMD_WAKE
#define TIKU_SHELL_CMD_WAKE    1  /**< wake    - Show active wake sources */
#endif
#ifndef TIKU_SHELL_CMD_FREQ
#define TIKU_SHELL_CMD_FREQ    1  /**< freq    - Show/set CPU core frequency */
#endif
#ifndef TIKU_SHELL_CMD_NAME
#define TIKU_SHELL_CMD_NAME    1  /**< name    - Read or set device name */
#endif
/* `if` is opt-in: it costs ~1 KB of FRAM, and the default FR5969 build
 * (MEMORY_MODEL=small) sits within a few hundred bytes of the 48 KB
 * lower-FRAM cap once arrow-key history navigation is included.  `on`
 * (rules) covers most interactive use cases.  Re-enable with
 *   EXTRA_CFLAGS="-DTIKU_SHELL_CMD_IF=1"
 * paired with a comparable disable (e.g. -DTIKU_SHELL_CMD_CALC=0). */
#ifndef TIKU_SHELL_CMD_IF
#define TIKU_SHELL_CMD_IF      0  /**< if      - Conditional VFS-driven action */
#endif
#ifndef TIKU_SHELL_CMD_IRQ
#define TIKU_SHELL_CMD_IRQ     1  /**< irq     - GPIO edge interrupt -> event */
#endif
#ifndef TIKU_SHELL_CMD_ALIAS
#define TIKU_SHELL_CMD_ALIAS   1  /**< alias   - FRAM-backed shell shortcuts */
#endif
#ifndef TIKU_SHELL_CMD_CAT
#define TIKU_SHELL_CMD_CAT     1  /**< cat     - Alias for read */
#endif
#ifndef TIKU_SHELL_CMD_ECHO
#define TIKU_SHELL_CMD_ECHO    1  /**< echo    - Print arguments + newline */
#endif
/* `lcd` is only useful on boards that physically wire an LCD panel
 * to the LCD_C peripheral (currently FR6989 LaunchPad). Default it
 * on / off based on the board header's TIKU_BOARD_HAS_LCD; user
 * override via -DTIKU_SHELL_CMD_LCD still wins. */
#ifndef TIKU_SHELL_CMD_LCD
#  if defined(TIKU_BOARD_HAS_LCD) && TIKU_BOARD_HAS_LCD
#    define TIKU_SHELL_CMD_LCD 1  /**< lcd     - Drive segment-LCD interface */
#  else
#    define TIKU_SHELL_CMD_LCD 0
#  endif
#endif
#ifndef TIKU_SHELL_CMD_WATCH
#define TIKU_SHELL_CMD_WATCH   1  /**< watch   - Periodic VFS read until Ctrl+C */
#endif
/* slip: hand the console UART to SLIP/IP networking.  Auto-on only when the
 * net stack is compiled in (TIKU_KIT_NET_ENABLE=1) -- the command starts the
 * net process, which does not exist otherwise. */
#ifndef TIKU_SHELL_CMD_SLIP
#if defined(TIKU_KIT_NET_ENABLE) && TIKU_KIT_NET_ENABLE
#define TIKU_SHELL_CMD_SLIP    1  /**< slip    - Hand the UART to SLIP/IP net */
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
#define TIKU_SHELL_CMD_IP      1  /**< ip      - Print the device IPv4 address */
#else
#define TIKU_SHELL_CMD_IP      0
#endif
#endif
/* ntp: fetch wall-clock time over SLIP (SNTP).  Same gating as slip/ping/ip;
 * the Makefile pulls in the time kit (TIKU_KIT_TIME_ENABLE) when it compiles. */
#ifndef TIKU_SHELL_CMD_NTP
#if defined(TIKU_KIT_NET_ENABLE) && TIKU_KIT_NET_ENABLE
#define TIKU_SHELL_CMD_NTP     1  /**< ntp     - Fetch network time (SNTP) */
#else
#define TIKU_SHELL_CMD_NTP     0
#endif
#endif
/* dns: resolve a hostname (A record) over SLIP.  Same gating as slip/ping/ip;
 * the DNS stub resolver is already compiled with the net kit. */
#ifndef TIKU_SHELL_CMD_DNS
#if defined(TIKU_KIT_NET_ENABLE) && TIKU_KIT_NET_ENABLE
#define TIKU_SHELL_CMD_DNS     1  /**< dns     - Resolve a hostname (A record) */
#else
#define TIKU_SHELL_CMD_DNS     0
#endif
#endif
/* syslog: send a remote log line (UDP 514) over SLIP.  Tracks the net kit,
 * but only in non-MIN builds: the syslog client (tiku_kits_net_syslog.c)
 * ships in the non-MIN ipv4 wildcard, so a MIN build (lean WiFi/SLIP) omits
 * it -- auto-drop the command there to avoid an undefined-reference link. */
#ifndef TIKU_SHELL_CMD_SYSLOG
#if defined(TIKU_KIT_NET_ENABLE) && TIKU_KIT_NET_ENABLE && \
    !(defined(TIKU_KIT_NET_MIN) && TIKU_KIT_NET_MIN)
#define TIKU_SHELL_CMD_SYSLOG  1  /**< syslog  - Send a remote log line (514) */
#else
#define TIKU_SHELL_CMD_SYSLOG  0
#endif
#endif
/* mqtt: connect/publish to an MQTT broker over SLIP+TCP.  Opt-in -- it needs
 * the heavier MQTT kit + TCP, so it tracks TIKU_KITS_NET_MQTT_ENABLE rather
 * than auto-on with net.  TikuBench's net-test build turns the kit on. */
#ifndef TIKU_SHELL_CMD_MQTT
#if defined(TIKU_KITS_NET_MQTT_ENABLE) && TIKU_KITS_NET_MQTT_ENABLE
#define TIKU_SHELL_CMD_MQTT    1  /**< mqtt    - Connect/publish to an MQTT broker */
#else
#define TIKU_SHELL_CMD_MQTT    0
#endif
#endif
#ifndef TIKU_SHELL_CMD_CALC
#define TIKU_SHELL_CMD_CALC    1  /**< calc    - Integer arithmetic */
#endif
/* `basic` is opt-in: the interpreter is ~3.5 KB of code plus an
 * arena allocation (~1.3 KB at default sizing).  The 3.5 KB push
 * most shell-enabled builds past the 48 KB lower-FRAM cap, so the
 * Makefile *requires* MEMORY_MODEL=large alongside BASIC and
 * refuses the build otherwise (override with
 * TIKU_SHELL_BASIC_ALLOW_SMALL=1 only if you know your part has
 * lower-FRAM headroom).  Recommended invocation:
 *
 *   make MCU=msp430fr5994 TIKU_SHELL_ENABLE=1 \
 *        TIKU_SHELL_BASIC_ENABLE=1 MEMORY_MODEL=large
 *
 * On larger parts (FR5994 / FR6989) you can pair this with bumped
 * arena sizing via EXTRA_CFLAGS, e.g.
 *   EXTRA_CFLAGS="-DTIKU_BASIC_PROGRAM_LINES=64"  */
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
#define TIKU_SHELL_CMD_CHANGED 1  /**< changed - Block until VFS value changes */
#endif
/* I2C is opt-in: it pulls in tiku_i2c_bus and arch driver, which
 * together cost ~1.4 KB of FRAM.  The default FR5969 shell build
 * already sits at ~44 KB of the 48 KB FRAM cap, so enabling I2C
 * requires turning off something else of comparable size.  Two
 * recipes that fit comfortably:
 *
 *   make MCU=msp430fr5969 TIKU_SHELL_ENABLE=1 \
 *        EXTRA_CFLAGS="-DTIKU_SHELL_CMD_I2C=1 -DTIKU_SHELL_CMD_HISTORY=0"
 *
 *   make MCU=msp430fr5969 TIKU_SHELL_ENABLE=1 \
 *        EXTRA_CFLAGS="-DTIKU_SHELL_CMD_I2C=1 -DTIKU_SHELL_CMD_CALC=0"
 */
#ifndef TIKU_SHELL_CMD_I2C
#define TIKU_SHELL_CMD_I2C    0  /**< i2c    - Bus scan / read / write */
#endif
#ifndef TIKU_SHELL_CMD_TREE
#define TIKU_SHELL_CMD_TREE   1  /**< tree   - Recursive VFS dump */
#endif
#ifndef TIKU_SHELL_CMD_CLEAR
#define TIKU_SHELL_CMD_CLEAR  1  /**< clear  - ANSI clear screen */
#endif

/* Scripting and debugging extras: enabled per-build via EXTRA_CFLAGS
 * because the default FR5969 shell already sits ~250 B from the
 * 48 KB FRAM cap.  Pick the combination you need; multiple flags
 * are independent.  Example:
 *   make MCU=msp430fr5969 TIKU_SHELL_ENABLE=1 \
 *        EXTRA_CFLAGS="-DTIKU_SHELL_CMD_DELAY=1 -DTIKU_SHELL_CMD_REPEAT=1"
 *
 * To enable the larger ones (peek/poke/i2c), pair them with a
 * disable of a comparable existing command, e.g.
 *   EXTRA_CFLAGS="-DTIKU_SHELL_CMD_PEEK=1 -DTIKU_SHELL_CMD_POKE=1 -DTIKU_SHELL_CMD_HISTORY=0"
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
#define TIKU_SHELL_CMD_INIT    TIKU_INIT_ENABLE  /**< init - FRAM boot entries */
#endif

/** @} */

/** @defgroup TIKU_SHELL_COLOR ANSI Color Output
 * @brief Enable colored shell output via ANSI escape codes.
 *
 * Build with:  make TIKU_SHELL_COLOR=1 MCU=msp430fr5969
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
 * @brief Enable TCP (telnet) backend on port 23.
 *
 * Requires the TikuKits TCP stack (TIKU_KITS_NET_TCP_ENABLE=1).
 * The net process must be auto-started alongside the CLI process.
 * Build with:
 *   make APP=cli MCU=msp430fr5969 \
 *        EXTRA_CFLAGS="-DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_SHELL_TCP_ENABLE=1"
 */
#ifndef TIKU_SHELL_TCP_ENABLE
#define TIKU_SHELL_TCP_ENABLE 0
#endif

/**
 * @brief Bring the net test servers (UDP echo + TCP + CoAP) up inside the shell.
 *
 * Off by default so the normal shell stays lean.  When set -- TikuBench's net
 * suite enables it on boards without a working APP=net (Ambiq) -- the shell
 * inits UDP/TCP and registers the CoAP server; the existing `slip` RX demux
 * feeds tiku_kits_net_ipv4_input(), which then dispatches to them, so the
 * device answers the suite's UDP/TCP/CoAP tests over SLIP.  No net process is
 * started (the shell owns the UART RX, so a second reader would conflict).
 */
#ifndef TIKU_SHELL_NET_TEST
#define TIKU_SHELL_NET_TEST 0
#endif

/** @} */

#endif /* TIKU_SHELL_CONFIG_H_ */
