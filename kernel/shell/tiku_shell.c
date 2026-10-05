/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell.c - shell process, command table and line editor.
 *
 * One cooperative protothread drains its input each poll, runs the line editor
 * and hands finished lines to the parser; line state is file-scope, since
 * protothread locals do not survive a yield.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell.h"
#if TIKU_INIT_ENABLE
#include <kernel/init/tiku_init.h>
#endif
#if (TIKU_DRV_USB_ENABLE + 0)
#endif
#include "tiku_shell_config.h"
#include "tiku_shell_parser.h"
#include "tiku_shell_cwd.h"          /* cwd for the path-aware prompt */
#include <kernel/timers/tiku_timer.h>
#include <kernel/timers/tiku_htimer.h>   /* htimer self-test command */
#include <kernel/timers/tiku_clock.h>
#include <kernel/cpu/tiku_watchdog.h>    /* liveness kick in net_getc waits */
#include <kernel/console/tiku_console.h> /* the wire's text, frames cut out */
#if TIKU_SHELL_CMD_JOBS
#include "tiku_shell_jobs.h"
#endif
#if TIKU_SHELL_CMD_RULES
#include "tiku_shell_rules.h"
#endif

#if TIKU_SHELL_TCP_ENABLE
#include "tiku_shell_io_tcp.h"
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>  /* tiku_kits_net_process */
#endif
#if defined(TIKU_CONSOLE_USB)
#if defined(PLATFORM_NORDIC)
#include <arch/nordic/tiku_usb_cdc_arch.h>
#else
#include <arch/arm-rp2350/tiku_usb_cdc_arch.h>
#endif
#endif
#if TIKU_SHELL_NET_TEST
#include <tikukits/net/ipv4/tiku_kits_net_udp.h>     /* udp_init, port 7 echo */
#if TIKU_KITS_NET_TCP_ENABLE
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>     /* tcp_init/periodic */
#endif
#endif

/*---------------------------------------------------------------------------*/
/* COMMAND HEADERS                                                           */
/*---------------------------------------------------------------------------*/

#if TIKU_SHELL_CMD_PS
#include "commands/tiku_shell_cmd_ps.h"
#endif
#if TIKU_SHELL_CMD_INFO
#include "commands/tiku_shell_cmd_info.h"
#endif
#if TIKU_SHELL_CMD_CONSOLE
#include "commands/tiku_shell_cmd_console.h"
#endif
#if TIKU_SHELL_CMD_TIMER
#include "commands/tiku_shell_cmd_timer.h"
#endif
#if TIKU_SHELL_CMD_KILL
#include "commands/tiku_shell_cmd_kill.h"
#endif
#if TIKU_SHELL_CMD_RESUME
#include "commands/tiku_shell_cmd_resume.h"
#endif
#if TIKU_SHELL_CMD_QUEUE
#include "commands/tiku_shell_cmd_queue.h"
#endif
#if TIKU_SHELL_CMD_REBOOT
#include "commands/tiku_shell_cmd_reboot.h"
#endif
#if TIKU_SHELL_CMD_USBHS
#include "commands/tiku_shell_cmd_usbhs.h"
#endif
#if TIKU_SHELL_CMD_CPU1
#include "commands/tiku_shell_cmd_cpu1.h"
#endif
#if TIKU_SHELL_CMD_NPU
#include "commands/tiku_shell_cmd_npu.h"
#endif
#if TIKU_SHELL_CMD_PANEL
#include "commands/tiku_shell_cmd_panel.h"
#endif
#if TIKU_SHELL_CMD_CAM
#include "commands/tiku_shell_cmd_cam.h"
#endif
#if TIKU_SHELL_CMD_TRNG
#include "commands/tiku_shell_cmd_trng.h"
#include "commands/tiku_shell_cmd_xflash.h"
#endif
#if TIKU_SHELL_CMD_CACHE
#include "commands/tiku_shell_cmd_cache.h"
#endif
#if TIKU_SHELL_CMD_DIAG
#include "commands/tiku_shell_cmd_diag.h"
#endif
#if TIKU_SHELL_CMD_SDRAM
#include "commands/tiku_shell_cmd_sdram.h"
#endif
#if TIKU_SHELL_CMD_MRAMBENCH
#include "commands/tiku_shell_cmd_mrambench.h"
#endif
#if TIKU_SHELL_CMD_BLE
#include "commands/tiku_shell_cmd_ble.h"
#endif
#if TIKU_SHELL_CMD_HISTORY
#include "commands/tiku_shell_cmd_history.h"
#endif
#if TIKU_SHELL_CMD_WIFI
#include "commands/tiku_shell_cmd_wifi.h"
#endif
#if TIKU_SHELL_CMD_BT
#include "commands/tiku_shell_cmd_bt.h"
#endif
#if TIKU_SHELL_CMD_SDR
#include "commands/tiku_shell_cmd_sdr.h"
#endif
#if TIKU_SHELL_CMD_INIT
#include "commands/tiku_shell_cmd_init.h"
#endif
#if TIKU_SHELL_CMD_LS
#include "commands/tiku_shell_cmd_ls.h"
#endif
#if TIKU_SHELL_CMD_CD
#include "commands/tiku_shell_cmd_cd.h"
#endif
#if TIKU_SHELL_CMD_TOGGLE
#include "commands/tiku_shell_cmd_toggle.h"
#endif
#if TIKU_SHELL_CMD_START
#include "commands/tiku_shell_cmd_start.h"
#endif
#if TIKU_SHELL_CMD_WRITE
#include "commands/tiku_shell_cmd_write.h"
#endif
#if TIKU_SHELL_CMD_FS
#include "commands/tiku_shell_cmd_fs.h"
#endif
#if TIKU_SHELL_CMD_DF
#include "commands/tiku_shell_cmd_df.h"
#include "commands/tiku_shell_cmd_fat.h"
#endif
#if TIKU_SHELL_CMD_LAYOUT
#include "commands/tiku_shell_cmd_layout.h"
#endif
#if TIKU_MEM_RECLAIM_ENABLE
#include "commands/tiku_shell_cmd_reclaim.h"
#endif
#if (TIKU_DRV_PSRAM_ENABLE + 0)
#if defined(TIKU_EXP_LLM)
#include <experiment/llm/tiku_shell_cmd_llm.h>  /* overlay repo, see Makefile */
#endif
#if defined(TIKU_EXP_VIT)
#include <experiment/vit/tiku_shell_cmd_vit.h>  /* overlay repo, see Makefile */
#endif
#if defined(TIKU_EXP_ASR)
#include <experiment/asr/tiku_shell_cmd_asr.h>  /* overlay repo, see Makefile */
#endif
#endif
#if TIKU_SHELL_CMD_NVMPROBE
#include "commands/tiku_shell_cmd_nvmprobe.h"
#endif
#if TIKU_SHELL_CMD_CRYPTOPROBE
#include "commands/tiku_shell_cmd_cryptoprobe.h"
#endif
#if TIKU_SHELL_CMD_AXONSPROBE
#include "commands/tiku_shell_cmd_axonsprobe.h"
#endif
#if TIKU_SHELL_CMD_USBMSC
#include "commands/tiku_shell_cmd_usbmsc.h"
#endif
#if TIKU_SHELL_CMD_USBPROBE
#include "commands/tiku_shell_cmd_usbprobe.h"
#endif
#if TIKU_SHELL_CMD_BLEADV
#include "commands/tiku_shell_cmd_bleadv.h"
#endif
#if TIKU_SHELL_CMD_RADIO154
#include "commands/tiku_shell_cmd_radio154.h"
#endif
#if TIKU_SHELL_CMD_RFTEST
#include "commands/tiku_shell_cmd_rftest.h"
#endif
#if TIKU_SHELL_CMD_READ
#include "commands/tiku_shell_cmd_read.h"
#endif
#if TIKU_SHELL_CMD_WATCH
#include "commands/tiku_shell_cmd_watch.h"
#endif
#if TIKU_SHELL_CMD_SLIP
#include "commands/tiku_shell_cmd_slip.h"
#endif
#if TIKU_SHELL_CMD_PING
#include "commands/tiku_shell_cmd_ping.h"
#include "commands/tiku_shell_cmd_ip.h"
#endif
#if TIKU_SHELL_CMD_NTP
#include "commands/tiku_shell_cmd_ntp.h"
#endif
#if TIKU_SHELL_CMD_DNS
#include "commands/tiku_shell_cmd_dns.h"
#endif
#if TIKU_SHELL_CMD_SYSLOG
#include "commands/tiku_shell_cmd_syslog.h"
#endif
#if TIKU_SHELL_CMD_MQTT
#include "commands/tiku_shell_cmd_mqtt.h"
#endif
#if TIKU_SHELL_CMD_CALC
#include "commands/tiku_shell_cmd_calc.h"
#endif
#if TIKU_SHELL_CMD_BASIC
#include "commands/tiku_shell_cmd_basic.h"
#include <kernel/shell/basic/tiku_basic.h>   /* non-blocking BASIC mode hooks */
#endif
#if TIKU_SHELL_CMD_JOBS
#include "commands/tiku_shell_cmd_every.h"
#include "commands/tiku_shell_cmd_once.h"
#include "commands/tiku_shell_cmd_jobs.h"
#endif
#if TIKU_SHELL_CMD_RULES
#include "commands/tiku_shell_cmd_on.h"
#include "commands/tiku_shell_cmd_rules.h"
#endif
#if TIKU_SHELL_CMD_CHANGED
#include "commands/tiku_shell_cmd_changed.h"
#endif
#if TIKU_SHELL_CMD_NAME
#include "commands/tiku_shell_cmd_name.h"
#endif
#if TIKU_SHELL_CMD_IF
#include "commands/tiku_shell_cmd_if.h"
#endif
#if TIKU_SHELL_CMD_IRQ
#include "commands/tiku_shell_cmd_irq.h"
#endif
#if TIKU_SHELL_CMD_I2C
#include "commands/tiku_shell_cmd_i2c.h"
#endif
#if TIKU_SHELL_CMD_TREE
#include "commands/tiku_shell_cmd_tree.h"
#endif
#if TIKU_SHELL_CMD_CLEAR
#include "commands/tiku_shell_cmd_clear.h"
#endif
#if TIKU_SHELL_CMD_DELAY
#include "commands/tiku_shell_cmd_delay.h"
#endif
#if TIKU_SHELL_CMD_REPEAT
#include "commands/tiku_shell_cmd_repeat.h"
#endif
#if TIKU_SHELL_CMD_PEEK || TIKU_SHELL_CMD_POKE
#include "commands/tiku_shell_cmd_mem.h"
#endif
#if TIKU_SHELL_CMD_ECHO
#include "commands/tiku_shell_cmd_echo.h"
#endif
#if TIKU_SHELL_CMD_LCD
#include "commands/tiku_shell_cmd_lcd.h"
#endif
#if TIKU_SHELL_CMD_ALIAS
#include "commands/tiku_shell_cmd_alias.h"
#include "commands/tiku_shell_cmd_unalias.h"
#include "tiku_shell_alias.h"
#endif
#if TIKU_SHELL_CMD_GPIO
#include "commands/tiku_shell_cmd_gpio.h"
#endif
#if TIKU_SHELL_CMD_ADC
#include "commands/tiku_shell_cmd_adc.h"
#endif
#if TIKU_SHELL_CMD_FREE
#include "commands/tiku_shell_cmd_free.h"
#endif
#if TIKU_SHELL_CMD_SLEEP
#include "commands/tiku_shell_cmd_sleep.h"
#endif
#if TIKU_SHELL_CMD_WAKE
#include "commands/tiku_shell_cmd_wake.h"
#endif
#if TIKU_SHELL_CMD_FREQ
#include "commands/tiku_shell_cmd_freq.h"
#include "commands/tiku_shell_cmd_power.h"
#endif

/*---------------------------------------------------------------------------*/
/* DECLARATIONS                                                              */
/*---------------------------------------------------------------------------*/

/*
 * The "help" built-in is defined later in this file but referenced
 * by the command table above it, so it needs a forward declaration.
 * Gated on TIKU_SHELL_CMD_HELP so a build without help compiles the
 * declaration away along with the definition and its table entry.
 */
#if TIKU_SHELL_CMD_HELP
static void tiku_shell_cmd_help(uint8_t argc, const char *argv[]);
#endif

/**
 * @brief Emit a category-header table entry.
 *
 * Expands to a tiku_shell_cmd_t with handler == NULL and the help field unused,
 * so @p label is the only meaningful field.  "help" renders these as section
 * titles; the dispatch loop skips any entry whose handler is NULL.
 *
 * @param label  Static string shown as the section heading.
 */
#define CMD_CATEGORY(label)  { label, NULL, NULL }

/*---------------------------------------------------------------------------*/
/* PROMPT AND INPUT                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Print the prompt, which shows the working directory.
 *
 * Every reprint goes through here (the banner, after a command, Ctrl+C, a
 * cancelled watch, a TCP client connecting), e.g. "tikuOS:/sys/device> ".
 */
static void shell_print_prompt(void) {
    SHELL_PRINTF(SH_GREEN SH_BOLD "tikuOS:%s> " SH_RST, tiku_shell_cwd_get());
}

/**
 * @brief The next keystroke for the line editor, or -1 when there is none.
 *
 * A telnet client that owns the line supplies its TCP bytes.  Otherwise the
 * console hands over the wire's text, every frame met on the way already
 * dispatched to its channel (the IP stack, a desktop's window session).
 */
static int
shell_getc(void)
{
    const tiku_shell_io_t *io = tiku_shell_io_get_backend();

    if (io == (const tiku_shell_io_t *)0) {
        return -1;
    }
#if TIKU_SHELL_TCP_ENABLE
    if (io == &tiku_shell_io_tcp) {
        return tiku_shell_io_getc();
    }
#endif
    return tiku_console_getc();
}

#if TIKU_SHELL_CMD_SLIP
/*
 * Drain the wire on behalf of a blocking builtin (e.g. BASIC HTTPGET$) that
 * has taken over the shell loop.  While such a builtin busy-waits, the main
 * loop is not running, so without this incoming SLIP frames (DNS reply,
 * TCP/TLS data) are never delivered to the IP stack.  The console's decoder
 * keeps its state across calls, so a frame that arrives across many calls
 * (the bytes trickle in at the line rate) is reassembled correctly.
 */
void
tiku_shell_net_pump(void)
{
    tiku_console_pump();
}

/*
 * Console-aware non-blocking getc -- see the header.  A blocking builtin that
 * reads the keyboard while a SLIP link is up (the BASIC REPL / INPUT after a
 * BROWSE) calls this instead of tiku_shell_io_getc(): frame bytes (a closed
 * connection's lingering teardown / retransmits) go to their channel rather
 * than landing in the line editor as garbage and wedging the console.
 */
int
tiku_shell_net_getc(void)
{
    /* A blocking builtin's input wait is liveness, not a hang: the BASIC
     * REPL prompt, INPUT, DELAY and the RUN loop's Ctrl-C poll all spin on
     * this call for unbounded time inside one dispatch of the shell process,
     * so the scheduler heartbeat is frozen for the whole session.  Kick here
     * (which also feeds the check-in hang detector) like the net pumps do,
     * or the detector blames the shell and warm-resets a quiet BASIC prompt
     * once TIKU_HANG_THRESHOLD_TICKS pass. */
    tiku_watchdog_kick();
    return shell_getc();
}
#endif

/*---------------------------------------------------------------------------*/
/* HTIMER SELF-TEST                                                          */
/*---------------------------------------------------------------------------*/

#if TIKU_SHELL_CMD_HTIMER
/** @brief Set by htimer_selftest_cb() when the compare fires. */
static volatile uint8_t s_htimer_selftest_fired;

/** @brief htimer callback, in ISR context: record that the compare fired. */
static void htimer_selftest_cb(struct tiku_htimer *t, void *ptr) {
    (void)t;
    (void)ptr;
    s_htimer_selftest_fired = 1u;
}

/**
 * @brief "htimer": self-test the hardware one-shot timer.
 *
 * Measures the htimer's count rate against the system tick, then schedules a
 * compare a tenth of a second ahead (at most 30000 ticks) and checks that
 * schedule -> compare -> ISR -> callback completes, timed on the system tick.
 *
 * @note Run it after boot, once a crystal-clocked htimer (e.g. Apollo510's
 *       STIMER, off the 32 kHz crystal) has settled.
 */
static void tiku_shell_cmd_htimer(uint8_t argc, const char *argv[]) {
    static struct tiku_htimer ht;   /* static: the ISR uses it later */
    tiku_htimer_clock_t now;
    tiku_clock_time_t   t0;
    unsigned long       elapsed;
    unsigned long       delay_ticks;
    unsigned long       target_ms;
    int                 rc_set;
    (void)argc;
    (void)argv;

    /* Measure the raw htimer count rate against one second of system ticks,
     * independent of TIKU_HTIMER_ARCH_SECOND. */
    {
        tiku_htimer_clock_t rc0 = tiku_htimer_arch_now();
        tiku_clock_time_t   rm0 = tiku_clock_time();
        while ((unsigned long)(tiku_clock_time() - rm0) < (unsigned long)TIKU_CLOCK_SECOND) {
            /* wait ~1 real second */
        }
        SHELL_PRINTF("htimer: STIMER measured ~%u Hz (configured %lu)\n",
                     (unsigned)(uint16_t)(tiku_htimer_arch_now() - rc0),
                     (unsigned long)TIKU_HTIMER_SECOND);
    }

    s_htimer_selftest_fired = 0u;

    /* The htimer clock is 16-bit, so a deadline can be at most ~2^15 ticks
     * ahead (the kernel's signed CLOCK_DIFF guard must stay positive).  A
     * fixed 100 ms target only fits a slow (kHz-class) htimer: at 1 MHz it is
     * 100000 ticks, which wraps to a negative diff and is rejected as "in the
     * past".  Cap the delay to a safe sub-range value so the test works at any
     * TIKU_HTIMER_SECOND (30 ms at 1 MHz, a full 100 ms at 16 kHz). */
    delay_ticks = (unsigned long)TIKU_HTIMER_SECOND / 10UL;
    if (delay_ticks > 30000UL) {
        delay_ticks = 30000UL;
    }
    target_ms = (delay_ticks * 1000UL) / (unsigned long)TIKU_HTIMER_SECOND;

    now = tiku_htimer_arch_now();
    rc_set = tiku_htimer_set(&ht,
                             (tiku_htimer_clock_t)(now +
                                 (tiku_htimer_clock_t)delay_ticks),
                             htimer_selftest_cb, NULL);
    if (rc_set != TIKU_HTIMER_OK) {
        SHELL_PRINTF("htimer: schedule rejected (%d)\n", rc_set);
        return;
    }

    /* Wait up to ~1 s (measured on the system tick) for the compare to fire. */
    t0 = tiku_clock_time();
    while (!s_htimer_selftest_fired &&
           ((unsigned long)(tiku_clock_time() - t0) < (unsigned long)TIKU_CLOCK_SECOND)) {
        /* spin */
    }
    elapsed = (unsigned long)(tiku_clock_time() - t0);

    if (s_htimer_selftest_fired) {
        SHELL_PRINTF("htimer: fired in ~%lu ms (target ~%lu) -- OK\n",
                     (elapsed * 1000UL) / (unsigned long)TIKU_CLOCK_SECOND,
                     target_ms);
    } else {
        SHELL_PRINTF("htimer: TIMEOUT (~1 s) -- compare not firing\n");
    }
}
#endif /* TIKU_SHELL_CMD_HTIMER */

/*---------------------------------------------------------------------------*/
/* COMMAND TABLE                                                             */
/*---------------------------------------------------------------------------*/

/*
 * Static command table, terminated by { NULL, NULL, NULL }.
 *
 * Two kinds of entry: commands { "name", "help text", handler }, and category
 * headers from CMD_CATEGORY() with a NULL handler, which group the "help"
 * listing and are skipped by the parser and by tab completion.
 *
 * Each command is gated by a build flag, mostly its TIKU_SHELL_CMD_* flag
 * from tiku_shell_config.h.  A flag set to 0 removes the row; the command's
 * code is then unreferenced and the linker drops it.  "cat" is gated on CAT
 * and READ because it reuses the "read" handler, "pwd" rides the CD flag, and
 * the "Networking" and "Boot" headers sit inside their commands' guards so
 * those categories never print empty.
 *
 * The steps for adding a command are at the top of tiku_shell_config.h.
 */
static const tiku_shell_cmd_t tiku_shell_commands[] = {
    CMD_CATEGORY("System"),
#if TIKU_SHELL_CMD_HELP
    {"help",    "Show available commands",     tiku_shell_cmd_help},
#endif
#if TIKU_SHELL_CMD_INFO
    {"info",    "Device, CPU, uptime, clock",  tiku_shell_cmd_info},
#endif
#if TIKU_SHELL_CMD_CONSOLE
    {"console", "The line's channels, counters", tiku_shell_cmd_console},
#endif
#if TIKU_SHELL_CMD_FREE
    {"free",    "Memory usage (SRAM/" TIKU_DEVICE_NVM_LABEL ")", tiku_shell_cmd_free},
#endif
#if TIKU_SHELL_CMD_REBOOT
    {"reboot",  "System reset",                tiku_shell_cmd_reboot},
#endif
#if TIKU_SHELL_CMD_USBHS
    {"usb",     "USB device disk: up|down|info", tiku_shell_cmd_usb},
    {"store",   "Model kept in external flash",  tiku_shell_cmd_store},
#endif
#if TIKU_SHELL_CMD_CPU1
    {"cpu1",    "Cortex-M33 core: start|stop|info", tiku_shell_cmd_cpu1},
#endif
#if TIKU_SHELL_CMD_PANEL
    {"panel",   "parallel RGB display: paint a colour", tiku_shell_cmd_panel},
#endif
#if TIKU_SHELL_CMD_CAM
    {"cam",     "camera: power, identify, capture", tiku_shell_cmd_cam},
#endif
#if TIKU_SHELL_CMD_NPU
    {"npu",     "Ethos-U55: release and report", tiku_shell_cmd_npu},
    {"npu-test", "Ethos-U55 vs the M85",        tiku_shell_cmd_npu_test},
#endif
#if TIKU_SHELL_CMD_TRNG
    {"trng",    "Dump hardware TRNG bytes",    tiku_shell_cmd_trng},
#endif
#if TIKU_SHELL_CMD_XFLASH
    {"xflash",  "External NOR: id | test | dump", tiku_shell_cmd_xflash},
#endif
#if TIKU_SHELL_CMD_CACHE
    {"cache",   "CPU caches: on | off | bench | dma", tiku_shell_cmd_cache},
#endif
#if TIKU_SHELL_CMD_DIAG
    {"diag",    "Faults, EXTI and the watchdog", tiku_shell_cmd_diag},
#endif
#if TIKU_SHELL_CMD_SDRAM
    {"sdram",   "External SDRAM: up | bench", tiku_shell_cmd_sdram},
#endif
#if TIKU_SHELL_CMD_MRAMBENCH
    {"mrambench","Time the MRAM programmer",   tiku_shell_cmd_mrambench},
#endif
#if TIKU_SHELL_CMD_BLE
    {"ble",     "EM9305 BLE radio: probe | beacon [name] | stop", tiku_shell_cmd_ble},
#endif
#if TIKU_SHELL_CMD_HISTORY
    {"history", "Last N commands from " TIKU_DEVICE_NVM_LABEL, tiku_shell_cmd_history},
#endif
#if TIKU_SHELL_CMD_WIFI
    {"wifi",    "WiFi: on/off/scan/connect/status", tiku_shell_cmd_wifi},
#endif
#if TIKU_SHELL_CMD_BT
    {"bt",      "CYW43 BT: status",             tiku_shell_cmd_bt},
#endif
#if TIKU_SHELL_CMD_SDR
    {"sdr",     "Radio receiver: I/Q snapshots", tiku_shell_cmd_sdr},
#endif
#if TIKU_SHELL_CMD_CALC
    {"calc",    "Integer arithmetic",          tiku_shell_cmd_calc},
#endif
#if TIKU_SHELL_CMD_BASIC
    {"basic",   "Tiku BASIC interpreter",      tiku_shell_cmd_basic},
#endif
#if TIKU_SHELL_CMD_CLEAR
    {"clear",   "Clear screen (ANSI)",         tiku_shell_cmd_clear},
#endif
#if TIKU_SHELL_CMD_DELAY
    {"delay",   "Wait <ms> (no LPM)",          tiku_shell_cmd_delay},
#endif
#if TIKU_SHELL_CMD_REPEAT
    {"repeat",  "Run command N times",         tiku_shell_cmd_repeat},
#endif

    CMD_CATEGORY("Processes"),
#if TIKU_SHELL_CMD_PS
    {"ps",      "List active processes",       tiku_shell_cmd_ps},
#endif
#if TIKU_SHELL_CMD_START
    {"start",   "Start/resume by name",        tiku_shell_cmd_start},
#endif
#if TIKU_SHELL_CMD_KILL
    {"kill",    "Stop a process (by pid)",     tiku_shell_cmd_kill},
#endif
#if TIKU_SHELL_CMD_RESUME
    {"resume",  "Resume a stopped process",    tiku_shell_cmd_resume},
#endif
#if TIKU_SHELL_CMD_QUEUE
    {"queue",   "List pending events",         tiku_shell_cmd_queue},
#endif
#if TIKU_SHELL_CMD_TIMER
    {"timer",   "Software timer status",       tiku_shell_cmd_timer},
#endif
#if TIKU_SHELL_CMD_JOBS
    {"every",   "Schedule a recurring command", tiku_shell_cmd_every},
    {"once",    "Schedule a one-shot command", tiku_shell_cmd_once},
    {"jobs",    "List/delete scheduled jobs",  tiku_shell_cmd_jobs},
#endif
#if TIKU_SHELL_CMD_RULES
    {"on",      "Register a reactive rule",    tiku_shell_cmd_on},
    {"rules",   "List/delete reactive rules",  tiku_shell_cmd_rules},
#endif

    CMD_CATEGORY("Filesystem"),
#if TIKU_SHELL_CMD_LS
    {"ls",      "List directory",              tiku_shell_cmd_ls},
#endif
#if TIKU_SHELL_CMD_TREE
    {"tree",    "Recursive directory listing", tiku_shell_cmd_tree},
#endif
#if TIKU_SHELL_CMD_CD
    {"cd",      "Change directory",            tiku_shell_cmd_cd},
    {"pwd",     "Print working directory",     tiku_shell_cmd_pwd},
#endif
#if TIKU_SHELL_CMD_READ
    {"read",    "Read a VFS node",             tiku_shell_cmd_read},
#endif
#if TIKU_SHELL_CMD_WATCH
    {"watch",   "Read VFS node every N sec",   tiku_shell_cmd_watch},
#endif
#if TIKU_SHELL_CMD_CHANGED
    {"changed", "Block until VFS node changes", tiku_shell_cmd_changed},
#endif
#if TIKU_SHELL_CMD_WRITE
    {"write",   "Write a VFS node",            tiku_shell_cmd_write},
#endif
#if TIKU_SHELL_CMD_FS
    {"rm",      "Delete a /data file",         tiku_shell_cmd_rm},
    {"touch",   "Create an empty /data file",  tiku_shell_cmd_touch},
    {"mkdir",   "Create a /data folder",       tiku_shell_cmd_mkdir},
    {"rmdir",   "Remove an empty /data folder", tiku_shell_cmd_rmdir},
    {"recv",    "Receive a file: recv <p> <n>", tiku_shell_cmd_recv},
    {"send",    "Send a file: send <p>",       tiku_shell_cmd_send},
#endif
#if TIKU_SHELL_CMD_DF
    {"df",      "/data file-store usage",      tiku_shell_cmd_df},
    {"mkfs",    "Format /data: mkfs [--erase-data]", tiku_shell_cmd_mkfs},
#endif
#if TIKU_SHELL_CMD_LAYOUT
    {"layout",  "Memory budgets: layout [show|limits|plan|stage|...]",
     tiku_shell_cmd_layout},
#endif
#if TIKU_MEM_RECLAIM_ENABLE
    {"mem", "Reconstruction: mem reclaim [status|owners|mode|retry]", tiku_shell_cmd_reclaim},
#endif
#if TIKU_SHELL_CMD_FAT
    {"fat",     "FAT32 on the eMMC: mount|ls|hash|runs", tiku_shell_cmd_fat},
#endif
#if (TIKU_DRV_PSRAM_ENABLE + 0)
#if defined(TIKU_EXP_LLM)
    {"llm",     "run a staged .tgf model: bind|verify|run", tiku_shell_cmd_llm},
#endif
#if defined(TIKU_EXP_VIT)
    {"vit",     "run a staged .tvf vision model: bind|verify|run",
                tiku_shell_cmd_vit},
#endif
#if defined(TIKU_EXP_ASR)
    {"asr",     "transcribe a .twf speech model: bindf|wavf|run",
                tiku_shell_cmd_asr},
#endif
#endif
#if TIKU_SHELL_CMD_NVMPROBE
    {"nvmprobe","Carved NVM region diagnostic", tiku_shell_cmd_nvmprobe},
#endif
#if TIKU_SHELL_CMD_CRYPTOPROBE
    {"cryptoprobe","CRACEN bring-up probe",   tiku_shell_cmd_cryptoprobe},
#endif
#if TIKU_SHELL_CMD_AXONSPROBE
    {"axonsprobe","Axon NPU bring-up probe",  tiku_shell_cmd_axonsprobe},
#endif
#if TIKU_SHELL_CMD_USBPROBE
    {"usbprobe","USB high-speed probe",       tiku_shell_cmd_usbprobe},
#endif
#if TIKU_SHELL_CMD_USBMSC
    {"usbmsc",  "USB mass storage disk",       tiku_shell_cmd_usbmsc},
#endif
#if TIKU_SHELL_CMD_BLEADV
    {"bleadv",  "BLE beacon (nRF54L)",        tiku_shell_cmd_bleadv},
#endif
#if TIKU_SHELL_CMD_RADIO154
    {"radio154","802.15.4 PHY (nRF54L)",      tiku_shell_cmd_radio154},
#endif
#if TIKU_SHELL_CMD_RFTEST
    {"rftest",  "RF test carrier (nRF54L)",   tiku_shell_cmd_rftest},
#endif
#if TIKU_SHELL_CMD_NAME
    {"name",    "Read/set device name",        tiku_shell_cmd_name},
#endif
#if TIKU_SHELL_CMD_IF
    {"if",      "Conditional: if <path> <op> <value> <cmd>",
                                               tiku_shell_cmd_if},
#endif
#if TIKU_SHELL_CMD_IRQ
    {"irq",     "Enable/disable GPIO edge IRQ", tiku_shell_cmd_irq},
#endif
#if TIKU_SHELL_CMD_ALIAS
    {"alias",   "Define/list " TIKU_DEVICE_NVM_LABEL "-backed aliases",
                                               tiku_shell_cmd_alias},
    {"unalias", "Remove an alias",             tiku_shell_cmd_unalias},
#endif
#if TIKU_SHELL_CMD_TOGGLE
    {"toggle",  "Flip a binary VFS node",      tiku_shell_cmd_toggle},
#endif
#if TIKU_SHELL_CMD_CAT && TIKU_SHELL_CMD_READ
    {"cat",     "Read (alias)",                tiku_shell_cmd_read},
#endif
#if TIKU_SHELL_CMD_ECHO
    {"echo",    "Print arguments + newline",   tiku_shell_cmd_echo},
#endif

#if TIKU_SHELL_CMD_SLIP || TIKU_SHELL_CMD_PING || TIKU_SHELL_CMD_IP ||      \
    TIKU_SHELL_CMD_NTP || TIKU_SHELL_CMD_DNS || TIKU_SHELL_CMD_SYSLOG ||    \
    TIKU_SHELL_CMD_MQTT
    CMD_CATEGORY("Networking"),
#endif
#if TIKU_SHELL_CMD_SLIP
    {"slip",    "Hand the UART to SLIP/IP net", tiku_shell_cmd_slip},
#endif
#if TIKU_SHELL_CMD_PING
    {"ping",    "ICMP echo a host over SLIP",   tiku_shell_cmd_ping},
#endif
#if TIKU_SHELL_CMD_IP
    {"ip",      "Print the device IPv4 address", tiku_shell_cmd_ip},
#endif
#if TIKU_SHELL_CMD_NTP
    {"ntp",     "Fetch network time (SNTP)",   tiku_shell_cmd_ntp},
#endif
#if TIKU_SHELL_CMD_DNS
    {"dns",     "Resolve a hostname (A record)", tiku_shell_cmd_dns},
#endif
#if TIKU_SHELL_CMD_SYSLOG
    {"syslog",  "Send a remote log line (514)", tiku_shell_cmd_syslog},
#endif
#if TIKU_SHELL_CMD_MQTT
    {"mqtt",    "Connect/publish to an MQTT broker", tiku_shell_cmd_mqtt},
#endif

    CMD_CATEGORY("Hardware"),
#if TIKU_SHELL_CMD_GPIO
    {"gpio",    "Read/write GPIO pins",        tiku_shell_cmd_gpio},
#endif
#if TIKU_SHELL_CMD_HTIMER
    {"htimer",  "Self-test the hardware timer", tiku_shell_cmd_htimer},
#endif
#if TIKU_SHELL_CMD_ADC
    {"adc",     "Read analog channel",         tiku_shell_cmd_adc},
#endif
#if TIKU_SHELL_CMD_I2C
    {"i2c",     "I2C scan/read/write",         tiku_shell_cmd_i2c},
#endif
#if TIKU_SHELL_CMD_PEEK
    {"peek",    "Read N bytes from address",   tiku_shell_cmd_peek},
#endif
#if TIKU_SHELL_CMD_POKE
    {"poke",    "Write byte to address",       tiku_shell_cmd_poke},
#endif
#if TIKU_SHELL_CMD_LCD
    {"lcd",     "Drive segment LCD",           tiku_shell_cmd_lcd},
#endif

    CMD_CATEGORY("Power"),
#if TIKU_SHELL_CMD_SLEEP
    {"sleep",   "Set low-power idle mode",     tiku_shell_cmd_sleep},
#endif
#if TIKU_SHELL_CMD_WAKE
    {"wake",    "Show active wake sources",    tiku_shell_cmd_wake},
#endif
#if TIKU_SHELL_CMD_FREQ
    {"freq",    "Show/set CPU core frequency", tiku_shell_cmd_freq},
#if TIKU_SHELL_CMD_POWER
    {"power",   "Cache/DC-DC/idle power knobs", tiku_shell_cmd_power},
#endif
#endif

#if TIKU_SHELL_CMD_INIT
    CMD_CATEGORY("Boot"),
    {"init",    "Manage " TIKU_DEVICE_NVM_LABEL " boot entries", tiku_shell_cmd_init},
#endif

    {NULL, NULL, NULL}
};

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return a pointer to the shell command table.
 *
 * The table is a NULL-terminated array of tiku_shell_cmd_t entries.
 * Entries with handler == NULL are category headers used by the
 * "help" command for grouping.
 *
 * @return Pointer to the first element of the static command table.
 */
const tiku_shell_cmd_t *
tiku_shell_get_commands(void)
{
    return tiku_shell_commands;
}

/*---------------------------------------------------------------------------*/
/* BUILT-IN COMMANDS                                                         */
/*---------------------------------------------------------------------------*/

#if TIKU_SHELL_CMD_HELP
/**
 * @brief "help" — print every registered command grouped by category.
 *
 * Walks tiku_shell_commands to the sentinel, printing a CMD_CATEGORY() marker
 * as a section title and every real entry as a left-justified name plus its
 * one-line help.
 *
 * @param argc  Argument count (ignored).
 * @param argv  Argument vector (ignored).
 */
static void
tiku_shell_cmd_help(uint8_t argc, const char *argv[])
{
    const tiku_shell_cmd_t *cmd;

    (void)argc;
    (void)argv;

    for (cmd = tiku_shell_commands; cmd->name != NULL; cmd++) {
        if (cmd->handler == NULL) {
            /* Category header */
            SHELL_PRINTF(SH_CYAN " --- %s ---" SH_RST "\n", cmd->name);
        } else {
            SHELL_PRINTF("  " SH_BOLD "%-10s" SH_RST " %s\n",
                         cmd->name, cmd->help);
        }
    }
}
#endif

/*---------------------------------------------------------------------------*/
/* CLI PROCESS                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Persistent line-editor state for the shell process.
 *
 * Statically allocated and file-scope rather than a protothread local, because
 * protothread locals do not survive a yield and every field here must persist
 * across the wait at the top of the poll loop.  There is exactly one shell.
 */
static struct {
    char               buf[TIKU_SHELL_LINE_SIZE];
                                    /**< Current input line being edited;
                                     *   NUL-terminated before dispatch. */
    uint8_t            pos;         /**< Count of bytes held in buf
                                     *   (also the cursor position, since
                                     *   editing is append/backspace only). */
    uint8_t            esc_state;   /**< ANSI CSI decoder state:
                                     *   0 = normal, 1 = saw ESC,
                                     *   2 = saw ESC then '['. */
    int8_t             hist_age;    /**< History recall position: -1 means
                                     *   not recalling, 0 = newest entry,
                                     *   larger = older (see history get). */
    struct tiku_timer  timer;       /**< Periodic I/O poll timer; posts
                                     *   TIKU_EVENT_TIMER to this process. */
} cli;

/* cli.pos and the tab-completion lengths are uint8_t, so a line must index
 * within 0..255: widen them before raising TIKU_SHELL_LINE_SIZE past 256. */
_Static_assert(TIKU_SHELL_LINE_SIZE <= 256,
               "cli.pos is uint8_t; widen it before TIKU_SHELL_LINE_SIZE > 256");

#if TIKU_SHELL_CMD_HISTORY
/**
 * @brief Replace the current input line with a recalled history entry.
 *
 * The up/down-arrow behaviour: @p up walks towards older entries, zero towards
 * newer.  Stepping newer past the newest clears the line and resets hist_age to
 * -1; stepping older past the oldest is a no-op.
 *
 * @note Erases the held characters with "\b \b", then echoes the entry from
 *       the history ring (tiku_shell_history_get()).
 * @param up  Non-zero to recall an older entry, zero to step newer.
 */
static void
shell_history_arrow(uint8_t up)
{
    int8_t      age;
    const char *line;

    if (up) {
        age = (cli.hist_age < 0) ? 0 : (int8_t)(cli.hist_age + 1);
    } else {
        age = (cli.hist_age <= 0) ? -1 : (int8_t)(cli.hist_age - 1);
    }
    line = (age < 0) ? (const char *)0
                     : tiku_shell_history_get((uint8_t)age);
    if (up && line == (const char *)0) {
        return;                          /* no older entry */
    }

    while (cli.pos > 0) {
        tiku_shell_io_putc('\b');
        tiku_shell_io_putc(' ');
        tiku_shell_io_putc('\b');
        cli.pos--;
    }
    while (line != (const char *)0 && *line != '\0' &&
           cli.pos < TIKU_SHELL_LINE_SIZE - 1) {
        cli.buf[cli.pos] = *line;
        tiku_shell_io_putc(*line);
        cli.pos++;
        line++;
    }
    cli.buf[cli.pos] = '\0';
    cli.hist_age     = age;
}
#endif /* TIKU_SHELL_CMD_HISTORY */

/*---------------------------------------------------------------------------*/
/* TAB COMPLETION                                                            */
/*---------------------------------------------------------------------------*/

/* VFS path completion is available whenever a path-consuming command (and
 * therefore the VFS + cwd resolver) is linked in.  Without it, Tab still
 * completes command names against the table. */
#if TIKU_SHELL_CMD_READ || TIKU_SHELL_CMD_LS || TIKU_SHELL_CMD_CD ||         \
    TIKU_SHELL_CMD_WRITE || TIKU_SHELL_CMD_WATCH
#define SHELL_TAB_VFS 1
#include <kernel/vfs/tiku_vfs.h>
#include "tiku_shell_cwd.h"
#endif

/** @brief Length of a NUL-terminated string (libc-free, byte-bounded). */
static uint8_t
tab_strlen(const char *s)
{
    uint8_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

/** @brief 1 if @p s begins with the first @p n bytes of @p pfx. */
static uint8_t
tab_has_prefix(const char *s, const char *pfx, uint8_t n)
{
    uint8_t i;

    for (i = 0; i < n; i++) {
        if (s[i] != pfx[i]) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Append up to @p n bytes of @p s to the live line and echo them.
 *
 * Mirrors the printable-key path: stores into the line buffer (bounded by
 * TIKU_SHELL_LINE_SIZE) and echoes when the backend wants local echo.
 */
static void
tab_emit(const char *s, uint8_t n)
{
    uint8_t k;

    for (k = 0; k < n && cli.pos < TIKU_SHELL_LINE_SIZE - 1; k++) {
        cli.buf[cli.pos++] = s[k];
        if (tiku_shell_io_has_echo()) {
            tiku_shell_io_putc(s[k]);
        }
    }
    cli.buf[cli.pos] = '\0';
}

/**
 * @brief Fold one candidate into the running match (count + common prefix).
 *
 * Streaming, so completion needs no candidate array: the first match seeds
 * @p first / @p lcp; each later match shrinks @p lcp to the longest prefix
 * still shared with the first.
 */
static void
tab_accum(const char *nm, uint8_t is_dir, const char **first,
          uint8_t *first_dir, uint8_t *count, uint8_t *lcp)
{
    if (*count == 0u) {
        *first     = nm;
        *first_dir = is_dir;
        *lcp       = tab_strlen(nm);
    } else {
        uint8_t k = 0;
        while (k < *lcp && nm[k] != '\0' && nm[k] == (*first)[k]) {
            k++;
        }
        *lcp = k;
    }
    (*count)++;
}

/**
 * @brief Tab-complete the token at the end of the current line.
 *
 * The first token completes against the command table; later tokens complete
 * against the VFS, split into a directory part resolved against the cwd and a
 * leaf prefix whose siblings supply the candidates.
 *
 * @note A unique match is filled in ('/' for a directory, ' ' otherwise); an
 *       ambiguous one extends to the longest common prefix, and a Tab with
 *       nothing left to extend lists the matches and redraws the line.
 */
static void
shell_tab_complete(void)
{
    uint8_t     tok_start, tok_len, i;
    const char *pfx;
    uint8_t     pfx_len;
    uint8_t     is_cmd;
    const char *first     = (const char *)0;
    uint8_t     first_dir = 0;
    uint8_t     count     = 0;
    uint8_t     lcp       = 0;
#if SHELL_TAB_VFS
    const tiku_vfs_node_t *dir = (const tiku_vfs_node_t *)0;
    char                   dirbuf[TIKU_SHELL_CWD_SIZE];
#endif

    cli.buf[cli.pos] = '\0';

    /* The token under the cursor is the trailing run of non-space bytes
     * (editing is append-only, so the cursor is always at the end). */
    tok_start = cli.pos;
    while (tok_start > 0 && cli.buf[tok_start - 1] != ' ') {
        tok_start--;
    }
    tok_len = (uint8_t)(cli.pos - tok_start);
    is_cmd  = (uint8_t)(tok_start == 0);

    if (is_cmd) {
        pfx     = cli.buf + tok_start;
        pfx_len = tok_len;
    }
#if SHELL_TAB_VFS
    else {
        const char *tok = cli.buf + tok_start;
        uint8_t     have_slash = 0, slash_at = 0, j;

        for (i = 0; i < tok_len; i++) {
            if (tok[i] == '/') {
                slash_at   = i;
                have_slash = 1;
            }
        }
        if (!have_slash) {
            tiku_shell_cwd_resolve(".", dirbuf, sizeof(dirbuf));
            pfx     = tok;
            pfx_len = tok_len;
        } else {
            char    raw[TIKU_SHELL_CWD_SIZE];
            uint8_t dlen = (slash_at == 0) ? 1u : slash_at;  /* "/x" -> "/" */

            for (j = 0; j < dlen && j < sizeof(raw) - 1u; j++) {
                raw[j] = tok[j];
            }
            raw[j]  = '\0';
            tiku_shell_cwd_resolve(raw, dirbuf, sizeof(dirbuf));
            pfx     = tok + slash_at + 1;
            pfx_len = (uint8_t)(tok_len - slash_at - 1u);
        }
        dir = tiku_vfs_resolve(dirbuf);
        if (dir == (const tiku_vfs_node_t *)0 || dir->type != TIKU_VFS_DIR) {
            return;
        }
    }
#else
    else {
        return;   /* no VFS in this build: only command names complete */
    }
#endif

    /* Pass 1: count matches, remember the first, shrink the common prefix. */
    if (is_cmd) {
        const tiku_shell_cmd_t *c;

        for (c = tiku_shell_commands; c->name != (const char *)0; c++) {
            if (c->handler != (tiku_shell_handler_t)0 &&
                tab_has_prefix(c->name, pfx, pfx_len)) {
                tab_accum(c->name, 0, &first, &first_dir, &count, &lcp);
            }
        }
    }
#if SHELL_TAB_VFS
    else {
        uint8_t j;

        for (j = 0; j < dir->child_count; j++) {
            const tiku_vfs_node_t *ch = &dir->children[j];

            if (tab_has_prefix(ch->name, pfx, pfx_len)) {
                tab_accum(ch->name,
                          (uint8_t)(ch->type == TIKU_VFS_DIR),
                          &first, &first_dir, &count, &lcp);
            }
        }
    }
#endif

    if (count == 0) {
        return;                         /* nothing matches */
    }
    if (lcp > pfx_len) {
        tab_emit(first + pfx_len, (uint8_t)(lcp - pfx_len));
    }
    if (count == 1) {
        tab_emit(first_dir ? "/" : " ", 1u);   /* unique: finish the token */
        return;
    }
    if (lcp != pfx_len) {
        return;                         /* extended; Tab again to list */
    }

    /* Pass 2: ambiguous with no further common prefix -> list, then redraw. */
    SHELL_PRINTF("\n");
    if (is_cmd) {
        const tiku_shell_cmd_t *c;

        for (c = tiku_shell_commands; c->name != (const char *)0; c++) {
            if (c->handler != (tiku_shell_handler_t)0 &&
                tab_has_prefix(c->name, pfx, pfx_len)) {
                SHELL_PRINTF("  %s", c->name);
            }
        }
    }
#if SHELL_TAB_VFS
    else {
        uint8_t j;

        for (j = 0; j < dir->child_count; j++) {
            const tiku_vfs_node_t *ch = &dir->children[j];

            if (tab_has_prefix(ch->name, pfx, pfx_len)) {
                SHELL_PRINTF("  %s%s", ch->name,
                             (ch->type == TIKU_VFS_DIR) ? "/" : "");
            }
        }
    }
#endif
    SHELL_PRINTF("\n");
    shell_print_prompt();
    if (tiku_shell_io_has_echo()) {
        for (i = 0; i < cli.pos; i++) {
            tiku_shell_io_putc(cli.buf[i]);
        }
    }
}

/**
 * @brief Define the shell process control block.
 *
 * Declares the tiku_process struct backing the shell and ties it to the
 * protothread body below; tiku_shell_init() registers it under the name
 * "Shell", which ps and /proc show.
 */
TIKU_PROCESS(tiku_shell_process, "CLI");

/*---------------------------------------------------------------------------*/
/* PUMP REGISTRY                                                             */
/*---------------------------------------------------------------------------*/
/* Static and bounded: a full table is refused at registration
 * (tiku_shell_add_pump() returns -1).  #ifndef so a build with more pumping
 * drivers can raise it. */
#ifndef TIKU_SHELL_PUMP_MAX
#define TIKU_SHELL_PUMP_MAX 4
#endif

static tiku_shell_pump_fn shell_pumps[TIKU_SHELL_PUMP_MAX];
static uint8_t shell_pump_count;

int tiku_shell_add_pump(tiku_shell_pump_fn fn)
{
    uint8_t i;

    if (fn == NULL) { return -1; }
    for (i = 0u; i < shell_pump_count; i++) {
        if (shell_pumps[i] == fn) { return 0; }   /* idempotent */
    }
    if (shell_pump_count >= (uint8_t)TIKU_SHELL_PUMP_MAX) { return -1; }
    shell_pumps[shell_pump_count++] = fn;
    return 0;
}

void tiku_shell_remove_pump(tiku_shell_pump_fn fn)
{
    uint8_t i, j;

    for (i = 0u; i < shell_pump_count; i++) {
        if (shell_pumps[i] != fn) { continue; }
        for (j = (uint8_t)(i + 1u); j < shell_pump_count; j++) {
            shell_pumps[j - 1u] = shell_pumps[j];
        }
        shell_pump_count--;
        shell_pumps[shell_pump_count] = NULL;
        return;
    }
}

/** @brief Run every registered pump once, in registration order. */
static void shell_run_pumps(void)
{
    uint8_t i;

    for (i = 0u; i < shell_pump_count; i++) {
        shell_pumps[i]();
    }
}

/**
 * @brief The shell process: line editor and command dispatcher.
 *
 * After a one-time setup pass, each poll drains the input through the line
 * editor, hands a finished line to tiku_shell_parser_execute() and then runs
 * the scheduled work; a TIKU_EVENT_VFS goes to the event-side hooks instead.
 */
TIKU_PROCESS_THREAD(tiku_shell_process, ev, data)
{
    /* The wait is a yield, so no local survives it: editor state lives in
     * `cli`, and ch is set and used within one pass of the drain loop. */
    int ch;

    (void)data;

    TIKU_PROCESS_BEGIN();

    /* ---- One-time init ---- */
    tiku_shell_parser_init(tiku_shell_commands);
#if TIKU_SHELL_CMD_ALIAS
    tiku_shell_alias_init();
#endif
#if TIKU_SHELL_CMD_JOBS
    tiku_shell_jobs_init();
#endif
#if TIKU_SHELL_CMD_RULES
    tiku_shell_rules_init();
#endif
    cli.pos       = 0;
    cli.esc_state = 0;
    cli.hist_age  = -1;

#if TIKU_SHELL_TCP_ENABLE
    tiku_shell_io_tcp_init();
#if TIKU_SHELL_NET_TEST
#if defined(TIKU_CONSOLE_USB) && !defined(TIKU_CONSOLE_BOTH)
    /* Net-test on a native-USB console build (RP2350, nRF54LM20): the USB
     * CDC port is the only wired console and carries SLIP too, so the
     * shell's output goes there as well. */
    tiku_shell_io_set_backend(&tiku_shell_io_usbcdc);
#else
    /* Net-test: the UART is both the local console and the SLIP transport,
     * so keep it as the default backend now; the telnet backend is installed
     * on connect (loop below) and reverts to UART on disconnect. */
    tiku_shell_io_set_backend(&tiku_shell_io_uart);
#endif
#else
    /* TCP-only shell (TIKU_SHELL_TCP_ENABLE without TIKU_SHELL_NET_TEST): no
     * local console, and the banner waits for a TCP client (loop below).
     * The Makefile compiles tiku_shell_io_tcp.c only with
     * TIKU_SHELL_NET_TEST, so a build of this branch must add it itself. */
#endif
#else
#if defined(TIKU_CONSOLE_USB) && !defined(TIKU_CONSOLE_BOTH)
    /* usb: the interactive shell is the native USB CDC-ACM port. */
    tiku_shell_io_set_backend(&tiku_shell_io_usbcdc);
#else
    /* uart, or both: the UART is the console wire and carries the shell. */
    tiku_shell_io_set_backend(&tiku_shell_io_uart);
#endif
#endif

#if !TIKU_SHELL_TCP_ENABLE || TIKU_SHELL_NET_TEST
    /* Boot banner: shown whenever there is a local console at boot, which is
     * every build but a TCP-only shell. */
    SHELL_PRINTF("\n");
    SHELL_PRINTF(SH_CYAN SH_BOLD);
    SHELL_PRINTF("  ___ _ _         ___  ___\n");
    SHELL_PRINTF(" |_ _|_) |_ _  _/ _ \\/ __|\n");
    SHELL_PRINTF("  | || | / / || | (_) \\__ \\\n");
    SHELL_PRINTF("  |_||_|_\\_\\\\_,_|\\___/|___/");
    SHELL_PRINTF(SH_RST SH_DIM "  v%s\n", TIKU_VERSION);
    SHELL_PRINTF("  %s" SH_RST "\n", TIKU_TAGLINE);
    SHELL_PRINTF("\n");
    SHELL_PRINTF("  " SH_BOLD "%s" SH_RST "  |  SRAM %luB  %s %luKB\n",
                 TIKU_DEVICE_NAME,
                 (unsigned long)TIKU_DEVICE_RAM_SIZE,
                 TIKU_DEVICE_NVM_LABEL,
                 (unsigned long)(TIKU_DEVICE_FRAM_SIZE / 1024));
    SHELL_PRINTF(SH_DIM "  Type 'help' for commands." SH_RST "\n\n");
#if TIKU_INIT_ENABLE
    /* Init-table entries run here, in the shell's first pass, so each one
     * behaves like a typed command: the parser's command table and the
     * backend are set up above, and main() has already run the VFS and
     * driver init. */
    tiku_init_run_all();
#endif
    shell_print_prompt();
#endif

    tiku_timer_set_event(&cli.timer, TIKU_SHELL_POLL_TICKS);

    /* ---- Main loop ---- */
    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER
                                      || ev == TIKU_EVENT_VFS);

#if defined(TIKU_CONSOLE_USB)
        /* Native-USB builds: service the USB CDC stack every pass, whatever
         * backend owns the shell.  The RP2350 stack has no interrupt, so its
         * EP0 class requests (SET_LINE_CODING, SET_CONTROL_LINE_STATE) are
         * answered only when something polls it, and a host blocks in
         * open()/tcsetattr() until they are; on the nRF54LM20 this brings
         * the stack up and down with VBUS.  Each call also sends queued
         * output. */
        tiku_usb_cdc_poll();
#endif

        /*
         * Registered pumps, in process context with interrupts enabled.  The
         * scheduler's idle hook runs inside tiku_atomic_enter() (interrupts
         * masked), and a USB mass-storage data phase blocks for a whole 64 KB
         * transfer: that long with interrupts masked stops the tick and the
         * console, and the debugger cannot halt the CPU.
         */
        shell_run_pumps();

#if TIKU_SHELL_CMD_RULES || TIKU_SHELL_CMD_WATCH || TIKU_SHELL_CMD_BASIC
        /* A watched VFS node changed: dispatch to the event-side
         * consumers (rules armed on that node, the live watch view if
         * it matches, and BASIC's event-driven ON CHANGE), then go
         * straight back to waiting.  The poll timer is periodic and
         * keeps running untouched, so input draining, jobs, and
         * sensor-side rules stay on their tick cadence. */
        if (ev == TIKU_EVENT_VFS) {
            const tiku_vfs_node_t *changed = tiku_event_node(ev, data);
#if TIKU_SHELL_CMD_RULES
            tiku_shell_rules_on_vfs(changed);
#endif
#if TIKU_SHELL_CMD_WATCH
            tiku_shell_cmd_watch_on_vfs(changed);
#endif
#if TIKU_SHELL_CMD_BASIC
            tiku_basic_mode_on_vfs(changed);
#endif
            continue;
        }
#endif

#if TIKU_SHELL_TCP_ENABLE
        /* --- TCP connection lifecycle --- */
        if (!tiku_shell_io_tcp_is_connected()) {
            if (tiku_shell_io_get_backend() == &tiku_shell_io_tcp) {
#if TIKU_SHELL_NET_TEST
#if defined(TIKU_CONSOLE_USB) && !defined(TIKU_CONSOLE_BOTH)
                /* Net-test, native-USB console: revert to the USB CDC port
                 * (the local console this build booted with). */
                tiku_shell_io_set_backend(&tiku_shell_io_usbcdc);
#else
                /* Net-test: the shell still owns the UART (console + SLIP
                 * transport), so revert to UART rather than going dark. */
                tiku_shell_io_set_backend(&tiku_shell_io_uart);
#endif
#else
                tiku_shell_io_set_backend((void *)0);
#endif
                cli.pos = 0;
            }
#if !TIKU_SHELL_NET_TEST
            /* TCP-only shell: idle until a client connects; the net process
             * services SLIP meanwhile. */
            tiku_timer_reset(&cli.timer);
            continue;
#endif
            /* Net-test falls through: the input drain below pumps the
             * console, whose IPv4 channel carries ping, UDP, TCP and
             * telnet. */
        }
        /* New connection arrived — install backend and show banner */
        if (tiku_shell_io_tcp_is_connected() &&
            tiku_shell_io_get_backend() != &tiku_shell_io_tcp) {
            tiku_shell_io_set_backend(&tiku_shell_io_tcp);
            cli.pos = 0;
            SHELL_PRINTF("\n");
            SHELL_PRINTF(SH_CYAN SH_BOLD);
            SHELL_PRINTF("  ___ _ _         ___  ___\n");
            SHELL_PRINTF(" |_ _|_) |_ _  _/ _ \\/ __|\n");
            SHELL_PRINTF("  | || | / / || | (_) \\__ \\\n");
            SHELL_PRINTF("  |_||_|_\\_\\\\_,_|\\___/|___/");
            SHELL_PRINTF(SH_RST SH_DIM "  v%s\n", TIKU_VERSION);
            SHELL_PRINTF("  %s" SH_RST "\n", TIKU_TAGLINE);
            SHELL_PRINTF("\n");
            SHELL_PRINTF("  " SH_BOLD "%s" SH_RST "  |  Telnet Shell\n",
                         TIKU_DEVICE_NAME);
            SHELL_PRINTF(SH_DIM "  Type 'help' for commands." SH_RST "\n\n");
            shell_print_prompt();
            tiku_shell_io_tcp_flush();
        }
#endif

        /* Re-arm poll timer first so commands that inspect
         * /sys/timer/count see it as active during execution. */
        tiku_timer_reset(&cli.timer);

#if TIKU_SHELL_TCP_ENABLE && TIKU_SHELL_NET_TEST && TIKU_SHELL_CMD_SLIP
        /* Net-test telnet: the console wire is the SLIP transport carrying
         * the telnet TCP, but a connected client makes the TCP backend
         * active -- so the drain below reads the client, not the wire,
         * which would starve telnet RX.  Pump the wire here so the telnet
         * transport keeps flowing; console keystrokes are dropped while
         * the remote client owns the line editor. */
        if (tiku_shell_cmd_slip_active() &&
            tiku_shell_io_get_backend() == &tiku_shell_io_tcp) {
            tiku_console_pump();
        }
#endif

        /* Drain every keystroke.  The console cuts the wire's frames out
         * before a byte reaches here: IP packets go to the stack and a
         * desktop's window session to its channel. */
        while ((ch = shell_getc()) >= 0) {
#if TIKU_SHELL_CMD_WATCH
            /* A live watch is streaming: keystrokes are routed to
             * the mode — Ctrl+C cancels it, everything else is
             * discarded. */
            if (tiku_shell_cmd_watch_active()) {
                if (ch == 0x03) {
                    tiku_shell_cmd_watch_cancel();
                    SHELL_PRINTF("^C\n");
                    shell_print_prompt();
                }
                continue;
            }
#endif

#if TIKU_SHELL_CMD_BASIC
            /* BASIC mode owns the console: route every byte to its own line
             * editor (printable echo, backspace, CR dispatch, Ctrl-C).  The
             * shell's line editor and command dispatch are bypassed until the
             * mode exits. */
            if (tiku_basic_mode_active() && !tiku_basic_mode_streamed()) {
                tiku_basic_mode_feed_char(ch);
                continue;
            }
#endif

            /* ANSI CSI arrow-key sequence: ESC [ A/B/C/D.
             * State 1 = saw ESC, state 2 = saw ESC[. */
            if (cli.esc_state == 1) {
                cli.esc_state = (ch == '[') ? 2 : 0;
                continue;
            }
            if (cli.esc_state == 2) {
#if TIKU_SHELL_CMD_HISTORY
                if (ch == 'A') {
                    shell_history_arrow(1);     /* up */
                } else if (ch == 'B') {
                    shell_history_arrow(0);     /* down */
                }
                /* Right (C) and left (D) ignored — no in-line cursor. */
#endif
                cli.esc_state = 0;
                continue;
            }
            if (ch == 0x1B) {
                cli.esc_state = 1;
                continue;
            }

            if (ch == '\r' || ch == '\n') {
                /* End of line — parse and dispatch */
                SHELL_PRINTF("\n");
                cli.buf[cli.pos] = '\0';
                if (cli.pos > 0) {
#if TIKU_SHELL_CMD_HISTORY
                    tiku_shell_history_record(cli.buf);
#endif
                    tiku_shell_parser_execute(cli.buf);
                }
                cli.pos      = 0;
                cli.hist_age = -1;
                /* A command still running (ping, ntp, dns, mqtt, BASIC mode)
                 * gets its prompt back when it finishes, so none is printed
                 * now. */
                {
                    uint8_t streaming = 0;
#if TIKU_SHELL_CMD_PING
                    if (tiku_shell_cmd_ping_active()) {
                        streaming = 1;
                    }
#endif
#if TIKU_SHELL_CMD_NTP
                    if (tiku_shell_cmd_ntp_active()) {
                        streaming = 1;
                    }
#endif
#if TIKU_SHELL_CMD_DNS
                    if (tiku_shell_cmd_dns_active()) {
                        streaming = 1;
                    }
#endif
#if TIKU_SHELL_CMD_MQTT
                    if (tiku_shell_cmd_mqtt_active()) {
                        streaming = 1;
                    }
#endif
#if TIKU_SHELL_CMD_BASIC
                    /* `basic` entered its own mode and printed the BASIC
                     * prompt; don't also print the shell prompt.  A mode
                     * driven from a stream prints its prompt down the stream
                     * and has not taken this line, so the shell's own prompt
                     * still ends a command here. */
                    if (tiku_basic_mode_active() &&
                        !tiku_basic_mode_streamed()) {
                        streaming = 1;
                    }
#endif
                    if (!streaming) {
                        shell_print_prompt();
                    }
                }

            } else if (ch == '\b' || ch == 127) {
                /* Backspace */
                if (cli.pos > 0) {
                    cli.pos--;
                    if (tiku_shell_io_has_echo()) {
                        SHELL_PRINTF("\b \b");
                    }
                }

            } else if (ch == 0x03) {
                /* Ctrl+C — cancel auto-firing jobs/rules, abort the
                 * current line, and reprint the prompt.  The escape
                 * hatch when an `every` job is flooding the shell. */
#if TIKU_SHELL_CMD_JOBS
                tiku_shell_jobs_clear();
#endif
#if TIKU_SHELL_CMD_RULES
                tiku_shell_rules_clear();
#endif
                cli.pos      = 0;
                cli.hist_age = -1;
                SHELL_PRINTF("^C\n");
                shell_print_prompt();

            } else if (ch == '\t') {
                /* Tab: complete the command name (first token) or a VFS
                 * path (later tokens) against the table / namespace. */
                shell_tab_complete();

            } else if (cli.pos < TIKU_SHELL_LINE_SIZE - 1) {
                /* Printable character — store and optionally echo.
                 * Typing past a recalled line exits recall mode. */
                cli.hist_age = -1;
                cli.buf[cli.pos++] = (char)ch;
                if (tiku_shell_io_has_echo()) {
                    tiku_shell_io_putc((char)ch);
                }
            }
        }

#if TIKU_SHELL_CMD_JOBS
        /* Fire any due scheduled jobs.  This runs after the input
         * drain so that user keystrokes get processed first; jobs
         * dispatch through the same parser as interactive commands. */
        tiku_shell_jobs_tick();
#endif
#if TIKU_SHELL_CMD_RULES
        /* Re-evaluate the poll-path rules (sensor nodes, unresolved
         * paths); rules on writable nodes run on TIKU_EVENT_VFS above. */
        tiku_shell_rules_tick();
#endif
#if TIKU_SHELL_CMD_WATCH
        /* Service the live watch: interval re-reads in sensor
         * mode, idempotent re-subscribe (self-heal) in event
         * mode. */
        tiku_shell_cmd_watch_tick();
#endif
#if TIKU_SHELL_CMD_BASIC
        /* Advance a running BASIC program by one batch of lines (no-op at the
         * REPL prompt), then -- if the mode just exited (BYE, Ctrl-C, or a
         * headless `basic run` finishing) -- restore the shell's own prompt. */
        tiku_basic_mode_tick();
        if (tiku_basic_mode_take_exit()) {
            shell_print_prompt();
        }
#endif
#if TIKU_SHELL_CMD_PING
        /* Service an active ping run: send/await probes across ticks.  When
         * the run completes the mode clears and the prompt is restored. */
        if (tiku_shell_cmd_ping_active()) {
            tiku_shell_cmd_ping_tick();
            if (!tiku_shell_cmd_ping_active()) {
                shell_print_prompt();
            }
        }
#endif
#if TIKU_SHELL_CMD_NTP
        /* Service an active NTP query: poll for the reply across ticks. */
        if (tiku_shell_cmd_ntp_active()) {
            tiku_shell_cmd_ntp_tick();
            if (!tiku_shell_cmd_ntp_active()) {
                shell_print_prompt();
            }
        }
#endif
#if TIKU_SHELL_CMD_DNS
        /* Service an active DNS query: poll for the reply across ticks. */
        if (tiku_shell_cmd_dns_active()) {
            tiku_shell_cmd_dns_tick();
            if (!tiku_shell_cmd_dns_active()) {
                shell_print_prompt();
            }
        }
#endif
#if TIKU_SHELL_CMD_MQTT
        /* Service an active MQTT op: pace mqtt_periodic + act on the event. */
        if (tiku_shell_cmd_mqtt_active()) {
            tiku_shell_cmd_mqtt_tick();
            if (!tiku_shell_cmd_mqtt_active()) {
                shell_print_prompt();
            }
        }
#endif

#if TIKU_SHELL_NET_TEST && TIKU_KITS_NET_TCP_ENABLE
        /* Drive TCP timers/retransmits for the net-test server (the
         * console's IPv4 channel delivers RX; this handles the time-based
         * side). */
        tiku_kits_net_tcp_periodic();
#endif
#if TIKU_SHELL_TCP_ENABLE
        tiku_shell_io_tcp_flush();
#endif
    }

    TIKU_PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/* SHELL INIT                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise and start the shell kernel service.
 *
 * Registers the shell process, plus the net process for a TCP-only shell, or
 * the net-test servers under TIKU_SHELL_NET_TEST.
 *
 * @note Call once from main(), before the scheduler loop starts.
 */
void tiku_shell_init(void)
{
    tiku_process_register("Shell", &tiku_shell_process);
#if TIKU_SHELL_TCP_ENABLE && !TIKU_SHELL_NET_TEST
    /* TCP-only shell: a dedicated net process owns the UART and SLIP.
     * Net-test leaves it unregistered: there the console, pumped by the
     * shell, feeds the IP stack (see the block below). */
    extern struct tiku_process tiku_kits_net_process;
    tiku_process_register("Net", &tiku_kits_net_process);
#endif
#if TIKU_SHELL_NET_TEST
    /* Net-test servers for the TikuBench net suite: UDP (echo on port 7),
     * TCP and the CoAP server.  The slip command's console channel feeds
     * tiku_kits_net_ipv4_input(), which dispatches to them, so the device
     * answers the suite's UDP/TCP/CoAP tests over SLIP.  No net process:
     * the console, pumped by the shell, owns the wire. */
    tiku_kits_net_udp_init();
#if TIKU_KITS_NET_TCP_ENABLE
    /* The shell process starts the telnet listener (port 23) in its setup
     * pass.  RX reaches it through the console's IPv4 channel ->
     * ipv4_input -> tcp_input. */
    tiku_kits_net_tcp_init();
#endif
#if defined(TIKU_KITS_NET_COAP)
    {
        extern struct tiku_process tiku_kits_net_coap_process;
        tiku_process_register("CoAP", &tiku_kits_net_coap_process);
    }
#endif
#endif
}
