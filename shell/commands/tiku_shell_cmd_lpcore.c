/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_lpcore.c - "lpcore" command (ESP32-P4 LP core).
 *
 * Launch and park go through /sys/coproc/run; this prints what the contract
 * does not carry (the core's registers and the payload's fault record) and
 * runs the payload's LED, I2C, clock and HP-wake commands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tiku.h>
#include <stdint.h>
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#include "tiku_shell_cmd_lpcore.h"

#if (TIKU_SHELL_CMD_LPCORE + 0)

#include <interfaces/coproc/tiku_coproc.h>
#include <kernel/timers/tiku_clock.h>
#include <arch/esp32p4/tiku_lpcore_arch.h>
#include <arch/esp32p4/tiku_sleep_arch.h>
#include <arch/esp32p4/lpcore/tiku_lpcore_ipc.h>
#include "tiku_shell_cmd_util.h"

/** @brief FireBeetle 2 I2C pins, both LP IOs. */
#define LPCORE_I2C_SDA 7u
#define LPCORE_I2C_SCL 8u

/** @brief Clock ticks the "clock" verb measures over: a quarter second. */
#define LPCORE_CLOCK_WINDOW (TIKU_CLOCK_SECOND / 4u)

static void lpcore_usage(void)
{
    SHELL_PRINTF("Usage: lpcore [led on|off|blink <passes> | i2c [sda scl]"
                 " | clock | wake ms <n> | wake io <n> 0|1]\n");
}

/** @brief Print the answer of a payload command that did not succeed. */
static int lpcore_answer(int rsp)
{
    if (rsp == (int)TIKU_LPCORE_RSP_DONE) {
        return 0;
    }
    if (rsp == (int)TIKU_LPCORE_RSP_BAD_ARG) {
        SHELL_PRINTF("lpcore: bad argument\n");
    } else {
        SHELL_PRINTF("lpcore: not running, or no answer\n");
    }
    return -1;
}

static void lpcore_led(uint8_t argc, const char *argv[])
{
    uint32_t mode, passes = 0u;
    tiku_p4_lpcore_counters_t c;

    if (argc < 3u) {
        lpcore_usage();
        return;
    }
    if (tiku_cmd_streq(argv[2], "blink")) {
        mode = 2u;
        passes = argc >= 4u ? tiku_cmd_parse_u32(argv[3]) : 50u;
    } else {
        int on = tiku_cmd_parse_on_off(argv[2]);
        if (on < 0) {
            lpcore_usage();
            return;
        }
        mode = (uint32_t)on;
    }
    if (lpcore_answer(tiku_p4_lpcore_command(TIKU_LPCORE_CMD_LED, mode,
                                             passes, 0u, 0u)) == 0) {
        tiku_p4_lpcore_counters(&c);
        SHELL_PRINTF("led %s  level %lu\n", argv[2],
                     (unsigned long)c.led_level);
    }
}

static void lpcore_i2c(uint8_t argc, const char *argv[])
{
    uint32_t sda = LPCORE_I2C_SDA, scl = LPCORE_I2C_SCL, a;
    uint8_t found[16];
    int n;

    if (argc >= 4u) {
        sda = tiku_cmd_parse_u32(argv[2]);
        scl = tiku_cmd_parse_u32(argv[3]);
    }
    if (lpcore_answer(tiku_p4_lpcore_command(TIKU_LPCORE_CMD_I2C_SCAN, sda,
                                             scl, 0u, 0u)) != 0) {
        return;
    }
    n = tiku_p4_lpcore_i2c_result(found);
    if (n < 0) {
        SHELL_PRINTF("i2c sda %lu scl %lu: SCL held low\n",
                     (unsigned long)sda, (unsigned long)scl);
        return;
    }
    SHELL_PRINTF("i2c sda %lu scl %lu: %d found", (unsigned long)sda,
                 (unsigned long)scl, n);
    for (a = 0u; a < 128u; a++) {
        if (found[a >> 3] & (1u << (a & 7u))) {
            SHELL_PRINTF(" 0x%02lx", (unsigned long)a);
        }
    }
    SHELL_PRINTF("\n");
}

/** @brief Arm the payload's one-shot HP wake: after @c ms on the LP timer,
 *         or when an LP IO reads a level. */
static void lpcore_wake(uint8_t argc, const char *argv[])
{
    uint32_t mode, a1, a2 = 0u;

    if (argc >= 4u && tiku_cmd_streq(argv[2], "ms")) {
        mode = TIKU_LPCORE_WAKE_TICKS;
        a1 = (uint32_t)(((uint64_t)tiku_cmd_parse_u32(argv[3]) *
                         tiku_p4_lp_hz()) / 1000ULL);
    } else if (argc >= 5u && tiku_cmd_streq(argv[2], "io")) {
        mode = TIKU_LPCORE_WAKE_IO;
        a1 = tiku_cmd_parse_u32(argv[3]);
        a2 = tiku_cmd_parse_u32(argv[4]);
    } else {
        lpcore_usage();
        return;
    }
    if (lpcore_answer(tiku_p4_lpcore_command(TIKU_LPCORE_CMD_WAKE, mode, a1,
                                             a2, 0u)) == 0) {
        SHELL_PRINTF("wake armed: %s %lu%s\n",
                     mode == TIKU_LPCORE_WAKE_TICKS ? "ticks" : "io",
                     (unsigned long)a1,
                     mode == TIKU_LPCORE_WAKE_IO
                         ? (a2 ? " high" : " low") : "");
    }
}

/** @brief Measure the LP core's cycle rate and the LP timer's tick rate over
 *         LPCORE_CLOCK_WINDOW, starting on a clock tick edge. */
static void lpcore_clock(void)
{
    tiku_p4_lpcore_counters_t c0, c1;
    tiku_clock_time_t t0, t1;
    uint32_t cycles;
    uint64_t ticks;

    if (!tiku_p4_lpcore_running()) {
        SHELL_PRINTF("lpcore: not running\n");
        return;
    }
    t0 = tiku_clock_time();
    while (tiku_clock_time() == t0) {
    }
    t0 = tiku_clock_time();
    tiku_p4_lpcore_counters(&c0);
    do {
        t1 = tiku_clock_time();
    } while ((tiku_clock_time_t)(t1 - t0) < LPCORE_CLOCK_WINDOW);
    tiku_p4_lpcore_counters(&c1);
    cycles = c1.cycles - c0.cycles;
    ticks = c1.lp_ticks - c0.lp_ticks;
    SHELL_PRINTF("cpu %lu Hz  lp timer %lu Hz  lp count 0x%04lx%08lx\n",
                 (unsigned long)(cycles * (TIKU_CLOCK_SECOND /
                                           LPCORE_CLOCK_WINDOW)),
                 (unsigned long)(ticks * (TIKU_CLOCK_SECOND /
                                          LPCORE_CLOCK_WINDOW)),
                 (unsigned long)(c1.lp_ticks >> 32),
                 (unsigned long)(c1.lp_ticks & 0xFFFFFFFFu));
}

static void lpcore_status(void)
{
    static const char *const names[] = {
        "absent", "stopped", "started", "running", "faulted"
    };
    tiku_coproc_state_t st = tiku_coproc_state();
    tiku_p4_lpcore_info_t in;

    if ((unsigned)st > (unsigned)TIKU_COPROC_FAULTED) {
        st = TIKU_COPROC_ABSENT;
    }
    tiku_p4_lpcore_info(&in);
    SHELL_PRINTF("state %s  image %lu B  boots %lu  heartbeat %lu\n",
                 names[st], (unsigned long)tiku_coproc_image_size(),
                 (unsigned long)in.boots,
                 (unsigned long)tiku_coproc_heartbeat());
    SHELL_PRINTF("clock %s  reset %s  wake 0x%08lx  pwr0 0x%08lx\n",
                 in.clk_on ? "on" : "off", in.in_reset ? "held" : "off",
                 (unsigned long)in.pwr2, (unsigned long)in.pwr0);
    SHELL_PRINTF("reset cause 0x%08lx  pc 0x%08lx  exc pc 0x%08lx  exc %lu\n",
                 (unsigned long)in.reset_cause, (unsigned long)in.dbg_pc,
                 (unsigned long)in.exc_pc, (unsigned long)in.exc_int);
    SHELL_PRINTF("faults %lu  mcause 0x%08lx  mepc 0x%08lx  mtval 0x%08lx\n",
                 (unsigned long)in.fault_count, (unsigned long)in.fault_cause,
                 (unsigned long)in.fault_epc, (unsigned long)in.fault_tval);
    {
        tiku_p4_lpcore_counters_t c;

        tiku_p4_lpcore_counters(&c);
        SHELL_PRINTF("lp io in 0x%04lx  wake armed %lu  wakes sent %lu\n",
                     (unsigned long)in.lp_io_in, (unsigned long)c.wake_mode,
                     (unsigned long)c.wake_count);
    }
}

void tiku_shell_cmd_lpcore(uint8_t argc, const char *argv[])
{
    if (argc < 2u) {
        lpcore_status();
    } else if (tiku_cmd_streq(argv[1], "led")) {
        lpcore_led(argc, argv);
    } else if (tiku_cmd_streq(argv[1], "i2c")) {
        lpcore_i2c(argc, argv);
    } else if (tiku_cmd_streq(argv[1], "clock")) {
        lpcore_clock();
    } else if (tiku_cmd_streq(argv[1], "wake")) {
        lpcore_wake(argc, argv);
    } else {
        lpcore_usage();
    }
}

#endif /* TIKU_SHELL_CMD_LPCORE */
