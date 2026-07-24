/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell.c - Shell process, command table, and line editor
 *
 * Defines the command table, the "help" built-in, and the TikuOS
 * protothread that performs line editing and feeds completed lines
 * to the parser.  All I/O goes through the tiku_shell_io abstraction
 * so the same code works over UART, a network link, or an LLM pipe.
 *
 * The command table is the heart of this file.  Entries are grouped
 * into visual categories (System, Processes, Filesystem, Hardware,
 * Power, Boot) by inserting CMD_CATEGORY() sentinels whose handler is
 * NULL; the "help" built-in renders those as section titles and the
 * parser skips them during dispatch.  Every real entry is wrapped in
 * an #if TIKU_SHELL_CMD_* guard from tiku_shell_config.h, so the table
 * — and the code it pulls in — shrinks to exactly the commands a given
 * build enables.  This is how the same source spans everything from a
 * tight lower-FRAM MSP430 budget to the roomier FR5994/FR6989 parts
 * and the Cortex-M targets.
 *
 * The shell process itself is a single cooperative protothread driven
 * by a periodic poll timer.  On each TIKU_EVENT_TIMER it drains every
 * byte currently buffered by the active I/O backend, runs a small
 * line-editing state machine (printable echo, backspace, Ctrl+C, and
 * an ANSI CSI decoder for the up/down history arrows), and on CR or LF
 * hands the finished line to tiku_shell_parser_execute().  When the
 * optional jobs/rules subsystems are compiled in, their due timers and
 * reactive conditions are serviced once per poll after the input drain.
 *
 * Because protothread local variables do not survive a yield, all line
 * state that must persist between polls lives in the file-scope `cli`
 * struct rather than on the protothread stack.  The only stack local in
 * the thread body, `ch`, is re-read inside each drain loop iteration and
 * never relied upon across the wait.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell.h"
#include "tiku_shell_config.h"
#include "tiku_shell_parser.h"
#include "tiku_shell_cwd.h"          /* working directory for the path-aware prompt */
#include <kernel/timers/tiku_timer.h>
#include <kernel/timers/tiku_htimer.h>   /* htimer self-test command */
#include <kernel/timers/tiku_clock.h>
#include <kernel/cpu/tiku_watchdog.h>    /* liveness kick in net_getc waits */
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
#include <arch/arm-rp2350/tiku_usb_cdc_arch.h>  /* usbcdc backend + poll pump */
#endif
#if TIKU_SHELL_CMD_SLIP
#include <tikukits/net/slip/tiku_kits_net_slip.h>   /* SLIP framing constants */
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>   /* tiku_kits_net_ipv4_input */
#endif
#if TIKU_SHELL_NET_TEST
#include <tikukits/net/ipv4/tiku_kits_net_udp.h>     /* udp_init (+ echo port 7) */
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
#if TIKU_SHELL_CMD_TRNG
#include "commands/tiku_shell_cmd_trng.h"
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
#endif

/*---------------------------------------------------------------------------*/
/* FORWARD DECLARATIONS                                                      */
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

/*---------------------------------------------------------------------------*/
/* COMMAND TABLE                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Static command table (NULL-terminated sentinel)
 *
 * A flat, statically-allocated array of tiku_shell_cmd_t.  Two kinds
 * of entry appear here:
 *
 *   - Real commands: { "name", "help text", handler }.  The parser
 *     matches argv[0] against the name and calls the handler; the
 *     "help" built-in prints name + help text.
 *   - Category headers: produced by CMD_CATEGORY() with handler ==
 *     NULL.  They carry only a label and exist purely to group the
 *     listing that "help" prints; the parser skips them.
 *
 * The array is laid out in category order (System, Processes,
 * Filesystem, Hardware, Power, Boot) and terminated by a sentinel
 * { NULL, NULL, NULL } so callers can iterate until name == NULL
 * without needing an element count.
 *
 * Every real entry is individually gated by its TIKU_SHELL_CMD_*
 * flag from tiku_shell_config.h.  A flag set to 0 (whether by the
 * default in the config header or by -DTIKU_SHELL_CMD_X=0 via
 * EXTRA_CFLAGS) removes both the table row and, via the matching
 * #include guard above, the command's object code — so a trimmed
 * build costs nothing for the commands it omits.  Note that "cat"
 * is doubly gated (TIKU_SHELL_CMD_CAT && TIKU_SHELL_CMD_READ) because
 * it simply reuses the "read" handler, and "pwd" rides on the same
 * TIKU_SHELL_CMD_CD flag as "cd".  The "Boot" category banner is
 * itself inside the TIKU_SHELL_CMD_INIT guard so an empty category
 * never prints.
 *
 * To add a new command:
 *   1. Create the handler in kernel/shell/commands/tiku_shell_cmd_xxx.c
 *   2. Add a TIKU_SHELL_CMD_XXX flag to tiku_shell_config.h
 *   3. #include the header above and add an entry here
 *   4. Add the .c to the Makefile (TIKU_SHELL_ENABLE=1 / APP=cli section)
 */

/**
 * @brief Emit a category-header table entry.
 *
 * Expands to a tiku_shell_cmd_t with handler == NULL and the help
 * field unused, so @p label is the only meaningful field.  The
 * "help" built-in renders these as section titles; the parser's
 * dispatch loop skips any entry whose handler is NULL.
 *
 * @param label  Static string shown as the section heading.
 */
#define CMD_CATEGORY(label)  { label, NULL, NULL }

/*
 * Print the interactive prompt.  Single definition so every reprint
 * (banner, after-command, Ctrl+C, watch-cancel, TCP reconnect) stays
 * identical, and so the prompt can show the shell's current working
 * directory -- the user always sees where they are, e.g.
 * "tikuOS:/sys/device> ".  The cwd string (tiku_shell_cwd_get()) always
 * starts with '/' and is at most TIKU_SHELL_CWD_SIZE bytes.
 */
static void shell_print_prompt(void) {
    SHELL_PRINTF(SH_GREEN SH_BOLD "tikuOS:%s> " SH_RST, tiku_shell_cwd_get());
}

#if TIKU_SHELL_CMD_SLIP
/*
 * SLIP RX demultiplexer.  While SLIP mode is on the shell shares the UART
 * with the IP stack: a 0xC0-delimited frame is reassembled (with SLIP
 * un-escaping) and handed to tiku_kits_net_ipv4_input(); any byte outside a
 * frame is an ordinary keystroke.  Returns 1 if the byte was consumed as part
 * of a SLIP frame, 0 if it should fall through to the line editor.
 */
static uint8_t shell_net_demux(int ch) {
    /* END (0xC0) is a frame delimiter, never an open/close parity toggle, and
     * the byte *after* an END decides frame-vs-console: an IPv4 packet always
     * begins 0x4N, so a post-END byte that is not 0x4N is console text. This
     * makes the decoder self-synchronising -- a stray or duplicated END (line
     * garbage, or the multiple ENDs a port reopen emits) can neither strand the
     * parser mid-frame nor divert a typed command into the frame buffer.
     * Returns 1 if the byte was consumed as SLIP, 0 to pass to the line editor. */
    static uint8_t  armed;     /* saw an END; next byte decides frame vs console */
    static uint8_t  in_frame;  /* collecting a frame */
    static uint8_t  esc;
    static uint16_t flen;
    static uint8_t  fbuf[TIKU_KITS_NET_MTU];
    static tiku_clock_time_t frame_t0;  /* when the current frame opened */
    uint8_t b;

    if (ch == TIKU_KITS_NET_SLIP_END) {        /* 0xC0 frame delimiter */
        if (in_frame && flen > 0) {
            tiku_kits_net_ipv4_input(fbuf, flen);
        }
        in_frame = 0;
        armed    = 1;          /* a frame *may* follow; the next byte decides */
        flen     = 0;
        esc      = 0;
        return 1;
    }

    b = (uint8_t)ch;

    if (armed) {               /* first byte after an END */
        armed = 0;
        if ((b & 0xF0u) == 0x40u) {            /* IPv4 version nibble -> frame */
            in_frame = 1;
            frame_t0 = tiku_clock_time();
        } else {
            return 0;                          /* stray END -> keystroke */
        }
    }
    if (!in_frame) {
        return 0;                              /* keystroke -> line editor */
    }
    /* Phantom-frame guard: a frame whose closing END was lost (observed on
     * hardware after an RX outage mid-frame) would otherwise swallow every
     * subsequent keystroke as payload, forever.  The threshold must dwarf a
     * LEGITIMATE frame's lifetime: frame bytes drain from the RX ring through
     * this demux, and the draining pump legitimately pauses for seconds
     * mid-frame during TLS crypto (a 1 s guard dropped live frames and
     * sprayed their payload into the console).  Nothing legitimate keeps one
     * frame open for 30 s -- the cert fetch deadline itself is 20 s -- so
     * this only catches true debris, at the cost of a console that takes up
     * to 30 s to self-recover after an RX outage. */
    if ((tiku_clock_time_t)(tiku_clock_time() - frame_t0)
        > (tiku_clock_time_t)(30 * TIKU_CLOCK_SECOND)) {
        in_frame = 0;
        esc      = 0;
        flen     = 0;
        return 0;
    }
    if (esc) {
        esc = 0;
        if (b == TIKU_KITS_NET_SLIP_ESC_END)      b = TIKU_KITS_NET_SLIP_END;
        else if (b == TIKU_KITS_NET_SLIP_ESC_ESC) b = TIKU_KITS_NET_SLIP_ESC;
        else if (b == TIKU_KITS_NET_SLIP_ESC_NUL) b = 0x00u;
    } else if (b == TIKU_KITS_NET_SLIP_ESC) {  /* 0xDB */
        esc = 1;
        return 1;
    }
    if (flen < (uint16_t)sizeof fbuf) {
        fbuf[flen++] = b;
    }
    return 1;
}

/*
 * Drain the shared UART through the SLIP demux on behalf of a blocking
 * builtin (e.g. BASIC HTTPGET$) that has taken over the shell loop.  While
 * such a builtin busy-waits, the main loop's demux is not running, so without
 * this incoming SLIP frames (DNS reply, TCP/TLS data) are never delivered to
 * the IP stack.  Crucially it reuses shell_net_demux, whose frame buffer is
 * static: a frame that arrives across many calls (the bytes trickle in at the
 * line rate, far slower than this is polled) is reassembled correctly, rather
 * than being shredded the way a caller-local accumulator would.
 */
void
tiku_shell_net_pump(void)
{
    int ch;
    while (tiku_shell_io_rx_ready()) {
        ch = tiku_shell_io_getc();
        if (ch < 0) {
            break;
        }
        (void)shell_net_demux(ch);
    }
}

/*
 * SLIP-aware non-blocking getc -- see the header.  A blocking builtin that
 * reads the keyboard while a SLIP link is up (the BASIC REPL / INPUT after a
 * BROWSE) calls this instead of tiku_shell_io_getc(): it routes SLIP frame
 * bytes (a closed connection's lingering teardown / retransmits) into the IP
 * stack, where they are consumed and ACKed, rather than letting them land in
 * the line editor as garbage and wedge the console.  Only bytes the demux
 * classifies as console (return 0) are handed back; if SLIP is not currently
 * active every byte is a keystroke, so it behaves like a plain getc.
 */
int
tiku_shell_net_getc(void)
{
    int ch;
    /* A blocking builtin's input wait is liveness, not a hang: the BASIC
     * REPL prompt, INPUT, DELAY and the RUN loop's Ctrl-C poll all spin on
     * this call for unbounded time INSIDE one dispatch of the shell process,
     * so the scheduler heartbeat is frozen for the whole session.  Kick here
     * (which also feeds the check-in hang detector) exactly like the net
     * pumps do, or the detector blames the shell and warm-resets ~2 s into
     * any quiet BASIC prompt. */
    tiku_watchdog_kick();
    while (tiku_shell_io_rx_ready()) {
        ch = tiku_shell_io_getc();
        if (ch < 0) {
            break;
        }
        if (tiku_shell_cmd_slip_active() && shell_net_demux(ch)) {
            continue;          /* consumed as a SLIP frame byte, not input */
        }
        return ch;             /* genuine console keystroke */
    }
    return -1;
}
#endif

#if TIKU_SHELL_CMD_HTIMER
/*
 * Self-test the hardware one-shot timer (htimer): schedule a ~100 ms compare
 * and confirm it fires, timed against the system tick. Run it after boot so a
 * crystal-clocked htimer (e.g. Apollo510's 32 kHz STIMER) has settled. This
 * validates the arch htimer end-to-end (schedule -> compare -> ISR ->
 * callback) without a logic analyzer; nothing else in the shell exercises it.
 */
static volatile uint8_t s_htimer_selftest_fired;

static void htimer_selftest_cb(struct tiku_htimer *t, void *ptr) {
    (void)t;
    (void)ptr;
    s_htimer_selftest_fired = 1u;
}

static void tiku_shell_cmd_htimer(uint8_t argc, const char *argv[]) {
    static struct tiku_htimer ht;   /* static: the ISR references it after we return */
    tiku_htimer_clock_t now;
    tiku_clock_time_t   t0;
    unsigned long       elapsed;
    unsigned long       delay_ticks;
    unsigned long       target_ms;
    int                 rc_set;
    (void)argc;
    (void)argv;

    /* Ground truth: measure the raw STIMER count rate against the (validated)
     * 128 Hz system tick -- independent of TIKU_HTIMER_ARCH_SECOND. */
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

static const tiku_shell_cmd_t tiku_shell_commands[] = {
    /* ---- System ---- */
    CMD_CATEGORY("System"),
#if TIKU_SHELL_CMD_HELP
    {"help",    "Show available commands",     tiku_shell_cmd_help},
#endif
#if TIKU_SHELL_CMD_INFO
    {"info",    "Device, CPU, uptime, clock",  tiku_shell_cmd_info},
#endif
#if TIKU_SHELL_CMD_FREE
    {"free",    "Memory usage (SRAM/FRAM)",    tiku_shell_cmd_free},
#endif
#if TIKU_SHELL_CMD_REBOOT
    {"reboot",  "System reset",                tiku_shell_cmd_reboot},
#endif
#if TIKU_SHELL_CMD_TRNG
    {"trng",    "Dump hardware TRNG bytes",    tiku_shell_cmd_trng},
#endif
#if TIKU_SHELL_CMD_MRAMBENCH
    {"mrambench","Time the MRAM programmer",   tiku_shell_cmd_mrambench},
#endif
#if TIKU_SHELL_CMD_BLE
    {"ble",     "EM9305 BLE radio: probe | beacon [name] | stop", tiku_shell_cmd_ble},
#endif
#if TIKU_SHELL_CMD_HISTORY
    {"history", "Last N commands from FRAM",   tiku_shell_cmd_history},
#endif
#if TIKU_SHELL_CMD_WIFI
    {"wifi",    "CYW43 WiFi: scan/connect/up/status", tiku_shell_cmd_wifi},
#endif
#if TIKU_SHELL_CMD_BT
    {"bt",      "CYW43 BT: status",             tiku_shell_cmd_bt},
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

    /* ---- Processes ---- */
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

    /* ---- Filesystem ---- */
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
    {"alias",   "Define/list FRAM-backed aliases",
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

    /* ---- Networking ---- */
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

    /* ---- Hardware ---- */
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

    /* ---- Power ---- */
    CMD_CATEGORY("Power"),
#if TIKU_SHELL_CMD_SLEEP
    {"sleep",   "Set low-power idle mode",     tiku_shell_cmd_sleep},
#endif
#if TIKU_SHELL_CMD_WAKE
    {"wake",    "Show active wake sources",    tiku_shell_cmd_wake},
#endif
#if TIKU_SHELL_CMD_FREQ
    {"freq",    "Show/set CPU core frequency", tiku_shell_cmd_freq},
#endif

    /* ---- Boot ---- */
#if TIKU_SHELL_CMD_INIT
    CMD_CATEGORY("Boot"),
    {"init",    "Manage FRAM boot entries",    tiku_shell_cmd_init},
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
 * Walks tiku_shell_commands from the first entry to the NULL-name
 * sentinel.  For each entry whose handler is NULL (a CMD_CATEGORY()
 * marker) it prints the name field as a dimmed/cyan section title;
 * for every real entry it prints the command name left-justified in
 * a fixed column followed by its one-line help text.  The output is
 * therefore an exact, build-specific reflection of the table — only
 * the commands compiled into this image are listed.
 *
 * Takes no arguments; argc/argv are accepted to match
 * tiku_shell_handler_t and are deliberately ignored.  All output
 * goes through SHELL_PRINTF so it follows the active I/O backend.
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
 * Statically allocated (no dynamic allocation) and file-scope rather
 * than a protothread local because protothread locals do not survive
 * a yield: every field here must persist across the
 * TIKU_PROCESS_WAIT_EVENT_UNTIL() at the top of the poll loop.  There
 * is exactly one shell process, so a single shared instance suffices.
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

/* cli.pos is uint8_t, so the line buffer must index within 0..255.  Raising
 * TIKU_SHELL_LINE_SIZE past 256 would let a full line overflow pos (and the
 * uint8_t history head/count) -- widen those fields first. */
_Static_assert(TIKU_SHELL_LINE_SIZE <= 256,
               "cli.pos is uint8_t; widen it before TIKU_SHELL_LINE_SIZE > 256");

#if TIKU_SHELL_CMD_HISTORY
/**
 * @brief Replace the current input line with a recalled history entry.
 *
 * Implements the up/down-arrow behaviour of an interactive shell.
 * @p up != 0 walks towards older entries (incrementing cli.hist_age);
 * up == 0 walks towards newer ones (decrementing it).  Walking newer
 * past the newest entry sets hist_age back to -1 and clears the line;
 * walking older past the oldest stored command is a no-op (the
 * history lookup returns NULL and the function returns early without
 * disturbing the line).
 *
 * On a successful step it first erases whatever is currently on the
 * line by emitting "\b \b" for each held character, then writes the
 * recalled text into cli.buf, echoes it, and updates cli.pos and
 * cli.hist_age.  The recalled string is read straight from the
 * FRAM-backed history ring via tiku_shell_history_get() (age 0 =
 * most recent) and is copied in, bounded by TIKU_SHELL_LINE_SIZE - 1.
 *
 * Uses the raw tiku_shell_io_putc() primitive rather than
 * SHELL_PRINTF to keep the formatted-output path out of the recall
 * code — it costs nothing here and keeps the smallest MSP430 builds
 * from pulling in the formatter for history alone.
 *
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
 * The first token (no leading space) completes against the command table;
 * later tokens complete against the VFS namespace -- the token is split into
 * a directory part (resolved against the cwd) and a leaf prefix, and the
 * directory's children supply the candidates.  A unique match is filled in
 * (with a trailing '/' for a directory, ' ' otherwise); an ambiguous one is
 * extended to the longest common prefix, and a second Tab (no further
 * progress) lists the matches and redraws the line.
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
 * Declares the tiku_process struct backing the shell and ties it to
 * the protothread body below.  The string "CLI" is the name the
 * process exposes through the process table and /proc views (the
 * service is separately registered as "Shell" in tiku_shell_init()).
 */
TIKU_PROCESS(tiku_shell_process, "CLI");

/**
 * @brief Shell process protothread — line editor and command dispatcher.
 *
 * Runs as a single cooperative TikuOS process.  After a one-time
 * initialisation pass it spends its life in a poll loop: it waits for
 * the periodic poll timer, drains every byte currently available from
 * the active I/O backend through a small line-editing state machine,
 * and on a carriage-return or line-feed hands the completed line to
 * tiku_shell_parser_execute().  Control returns to the scheduler
 * between polls, so the shell consumes no CPU while idle.
 *
 * One-time initialisation (runs once, after TIKU_PROCESS_BEGIN):
 *   1. Register the command table with the parser.
 *   2. Initialise the optional alias / jobs / rules subsystems when
 *      their TIKU_SHELL_CMD_* flags are set.
 *   3. Reset the line-editor state (cli.pos, cli.esc_state,
 *      cli.hist_age).
 *   4. Choose the I/O backend.  Over UART this installs
 *      tiku_shell_io_uart and prints the banner + first prompt
 *      immediately; with the TCP backend the banner is deferred and
 *      printed later, when a telnet client actually connects.
 *   5. Arm the poll timer for TIKU_SHELL_POLL_TICKS.
 *
 * Poll loop (one pass per TIKU_EVENT_TIMER):
 *   - When TCP is enabled, manage the connection lifecycle first:
 *     drop the backend when the client disconnects, and install the
 *     backend + print the banner on a freshly accepted connection.
 *   - Re-arm the poll timer up front (via tiku_timer_reset(), which
 *     re-adds it drift-free) so a command that inspects
 *     /sys/timer/count sees the shell's own timer as active while it
 *     runs.
 *   - Input-byte path: while the backend reports bytes ready, read one
 *     byte and route it:
 *       * ESC (0x1B) starts a two-step ANSI CSI sequence; the next two
 *         bytes are consumed by the esc_state machine so that "ESC [ A"
 *         and "ESC [ B" map to up/down history recall (left/right are
 *         intentionally ignored — there is no in-line cursor).
 *       * CR or LF terminates the line: echo a newline, NUL-terminate
 *         the buffer, record it in history and dispatch it to the
 *         parser when non-empty, then reset the line and reprint the
 *         prompt.
 *       * Backspace (0x08) or DEL (0x7F) removes the last character and,
 *         when the backend wants local echo, erases it on screen.
 *       * Ctrl+C (0x03) is the escape hatch when an `every` job or rule
 *         is flooding output: it clears any auto-firing jobs/rules,
 *         abandons the current line, and reprints the prompt.
 *       * Any other printable byte is appended to the line (up to
 *         TIKU_SHELL_LINE_SIZE - 1) and echoed when echo is enabled;
 *         typing exits history-recall mode by resetting cli.hist_age.
 *   - After the drain, service the optional jobs and rules ticks (so
 *     user keystrokes are always processed before scheduled work), and
 *     flush the TCP backend if it is in use.
 *
 * Protothread caveat: TIKU_PROCESS_WAIT_EVENT_UNTIL expands to a
 * PT_YIELD_UNTIL, so the C stack is unwound at the wait point and no
 * protothread local survives it.  All editor state therefore lives in
 * the file-scope `cli` struct; the only local here, `ch`, is assigned
 * and consumed within a single drain-loop iteration and is never read
 * across the wait.
 *
 * @param ev    Event delivered to the process (TIKU_EVENT_TIMER drives
 *              each poll pass).
 * @param data  Event data pointer (unused).
 */
TIKU_PROCESS_THREAD(tiku_shell_process, ev, data)
{
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
    /* Net-test on a native-USB console build (RP2350): the USB CDC port is
     * the only wired console -- picking the UART here orphans the one port
     * the host tools connect to (the shell never reads USB, so the console
     * is dead and a macOS host freezes ~60 s opening it).  The SLIP
     * transport itself still rides the physical UART (tiku_kits_net_slip
     * writes via tiku_uart_putc), so net-test over a UART rig is intact. */
    tiku_shell_io_set_backend(&tiku_shell_io_usbcdc);
#else
    /* Net-test: the UART is BOTH the local console and the SLIP transport, so
     * keep it as the default backend now; the telnet backend is installed on
     * connect (loop below) and reverts to UART on disconnect. */
    tiku_shell_io_set_backend(&tiku_shell_io_uart);
#endif
#else
    /* APP=cli telnet-only: no local console; banner deferred until a TCP
     * client connects (see loop below). */
#endif
#else
#if defined(TIKU_CONSOLE_USB) && !defined(TIKU_CONSOLE_BOTH)
    /* usb: the interactive shell is the RP2350 USB CDC-ACM port. */
    tiku_shell_io_set_backend(&tiku_shell_io_usbcdc);
#else
    /* uart, or both (UART is the reliable full-duplex control channel; in
     * `both` mode USB just mirrors TIKU_PRINTF output). */
    tiku_shell_io_set_backend(&tiku_shell_io_uart);
#endif
#endif

    /* NVM technology label is per-device: MRAM on Apollo, Flash on RP2350,
     * FRAM on MSP430.  Fall back to the MSP430-era "FRAM" if a device header
     * has not declared one. */
#ifndef TIKU_DEVICE_NVM_LABEL
#define TIKU_DEVICE_NVM_LABEL "FRAM"
#endif

#if !TIKU_SHELL_TCP_ENABLE || TIKU_SHELL_NET_TEST
    /* Boot banner: shown whenever there is a local console at boot -- every
     * non-telnet build, plus net-test (which keeps the UART console). */
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
    shell_print_prompt();
#endif

    tiku_timer_set_event(&cli.timer, TIKU_SHELL_POLL_TICKS);

    /* ---- Main loop ---- */
    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER
                                      || ev == TIKU_EVENT_VFS);

#if defined(TIKU_CONSOLE_USB)
        /* Native-USB builds: pump the polled CDC stack every pass no matter
         * which backend owns the shell.  The stack has no IRQ, so EP0 class
         * requests (SET_LINE_CODING / SET_CONTROL_LINE_STATE) are answered
         * only when someone calls poll() -- leave it unserviced and a macOS
         * host blocks ~30 s PER REQUEST inside open()/tcsetattr() on
         * /dev/cu.usbmodem* (Linux's cdc_acm merely times out after 5 s and
         * carries on, which is why a dead port was only conspicuous on
         * Macs).  Also flushes mirrored TIKU_PRINTF output and drains host
         * writes when the backend is UART/TCP (net-test, telnet, `both`). */
        tiku_usb_cdc_poll();
#endif

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
            /* No telnet client connected. */
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
            /* APP=cli telnet-only: idle until a client connects (a dedicated
             * net process services the SLIP transport meanwhile). */
            tiku_timer_reset(&cli.timer);
            continue;
#endif
            /* Net-test falls through: the input drain below keeps pumping the
             * UART SLIP demux -- the transport for ping/udp/tcp/telnet. */
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
         * active -- so the per-backend drain below stops reading the wire,
         * which would starve telnet RX.  Pump the wire through the SLIP
         * demux here so the telnet transport keeps flowing; console
         * keystrokes are dropped while the remote client owns the line
         * editor.  The wire follows the console: the USB CDC port on an
         * RP2350 native-USB build, the UART everywhere else (it must match
         * SLIP_WIRE_* in tiku_kits_net_slip.c, where the TX side lives). */
#if defined(TIKU_CONSOLE_USB) && !defined(TIKU_CONSOLE_BOTH)
#define SHELL_SLIP_WIRE_IO tiku_shell_io_usbcdc
#else
#define SHELL_SLIP_WIRE_IO tiku_shell_io_uart
#endif
        if (tiku_shell_cmd_slip_active() &&
            tiku_shell_io_get_backend() == &tiku_shell_io_tcp) {
            while (SHELL_SLIP_WIRE_IO.rx_ready()) {
                int uch = SHELL_SLIP_WIRE_IO.getc();
                if (uch < 0) {
                    break;
                }
                (void)shell_net_demux(uch);
            }
        }
#endif

        /* Drain all available characters from the backend.  In SLIP mode the
         * shell shares the UART: complete 0xC0 frames are routed to the IP
         * stack, while ordinary keystrokes still reach the line editor below. */
        while (tiku_shell_io_rx_ready()) {
            ch = tiku_shell_io_getc();
            if (ch < 0) {
                break;
            }
#if TIKU_SHELL_CMD_SLIP
            if (tiku_shell_cmd_slip_active()
#if TIKU_SHELL_TCP_ENABLE
                /* When a telnet client owns the line editor, getc() returns
                 * its TCP bytes -- those are NOT SLIP frames, so must not go
                 * to the demux (which is mid-frame for the UART transport and
                 * would swallow them).  The UART SLIP transport is drained
                 * separately above. */
                && tiku_shell_io_get_backend() != &tiku_shell_io_tcp
#endif
                && shell_net_demux(ch)) {
                continue;
            }
#endif

#if TIKU_SHELL_CMD_WATCH
            /* A live watch is streaming: keystrokes are routed to
             * the mode — Ctrl+C cancels it, everything else is
             * discarded.  This preserves the modal feel of the
             * original blocking watch while the shell loop (and
             * the watch itself) stays event-driven underneath. */
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
             * mode exits.  Mirrors the modal feel of the old blocking REPL
             * while the shell loop stays event-driven underneath. */
            if (tiku_basic_mode_active()) {
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
                /* Async net commands (ping/ntp) stream output and restore the
                 * prompt when they finish -- don't print a stray one now. */
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
                    /* `basic` entered its own mode and printed the BASIC prompt;
                     * don't also print the shell prompt. */
                    if (tiku_basic_mode_active()) {
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
        /* Re-evaluate reactive rules.  Edge-triggered, so actions
         * fire only on a false->true transition. */
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
         * the run completes the mode clears and we restore the prompt. */
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
        /* Drive TCP timers/retransmits for the net-test server (the shell's
         * slip demux delivers RX; this handles the time-based side). */
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
 * Registers the CLI process (and optionally the network process
 * when TCP shell is enabled) with the TikuOS scheduler.  Call once
 * from main() after tiku_vfs_tree_init().
 *
 * The shell process prints the boot banner, starts the I/O poll
 * timer, and begins accepting commands on the next scheduler tick.
 */
void tiku_shell_init(void)
{
    tiku_process_register("Shell", &tiku_shell_process);
#if TIKU_SHELL_TCP_ENABLE && !TIKU_SHELL_NET_TEST
    /* APP=cli telnet model: a dedicated net process owns the UART RX + SLIP.
     * In net-test mode the shell owns RX via its demux (and the net process is
     * not even compiled), so skip this -- see the net-test block below. */
    extern struct tiku_process tiku_kits_net_process;
    tiku_process_register("Net", &tiku_kits_net_process);
#endif
#if TIKU_SHELL_NET_TEST
    /* Net test servers for the TikuBench net suite (Ambiq has no APP=net):
     * init UDP (built-in echo on port 7) + TCP, and register the CoAP server.
     * The shell's `slip` demux feeds tiku_kits_net_ipv4_input(), which then
     * dispatches to these -- so the device answers the suite's UDP/TCP/CoAP
     * tests over SLIP.  No net process (the shell owns UART RX). */
    tiku_kits_net_udp_init();
#if TIKU_KITS_NET_TCP_ENABLE
    /* The telnet listener (port 23, when TIKU_SHELL_TCP_ENABLE) is started by
     * the shell process itself once the stack is up -- see the process body
     * above.  RX reaches it through the slip demux -> ipv4_input -> tcp_input. */
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
