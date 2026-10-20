/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_lp.c - "lp" command (ESP32-P4 LP agent).
 *
 * The agent's jobs run on the LP core, awake or asleep; this command and
 * the `on ... wake` rule form write the table, and the job that ends a deep
 * sleep has its command line run once the shell is up.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tiku.h>
#include <stdint.h>
#include <string.h>
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_io.h>
#include <shell/tiku_shell_parser.h>
#include "tiku_shell_cmd_lp.h"

#if (TIKU_SHELL_CMD_LP + 0)

#include <arch/esp32p4/tiku_lpagent_arch.h>
#include "tiku_shell_cmd_util.h"

/** @brief How often a rule's LP IO is read, in milliseconds. */
#define LP_RULE_PERIOD_MS 20u

/** @brief The /dev/gpio path of LP IO n: GPIO port 1, pins 0..15. */
#define LP_RULE_PREFIX "/dev/gpio/1/"

static const char *const op_names[] = {
    "?", "==", "!=", ">", "<", "changed", "rise", "fall", "always"
};

static void lp_usage(void)
{
    SHELL_PRINTF("Usage: lp [add io <n> <op> <value> <ms> <act> [cmd...] |"
                 " add timer <ms> <act> [cmd...] | del <slot> | samples]\n"
                 "  op: == != > < changed rise fall   act: wake sample"
                 " wake+sample\n");
}

/** @brief An operator word; 0 when it is none. */
static uint8_t lp_parse_op(const char *s)
{
    uint8_t i;

    for (i = 1u; i < sizeof(op_names) / sizeof(op_names[0]); i++) {
        if (tiku_cmd_streq(s, op_names[i])) {
            return i;
        }
    }
    return 0u;
}

/** @brief An action word; 0 when it is none. */
static uint8_t lp_parse_action(const char *s)
{
    if (tiku_cmd_streq(s, "wake")) {
        return TIKU_LPAGENT_ACT_WAKE;
    }
    if (tiku_cmd_streq(s, "sample")) {
        return TIKU_LPAGENT_ACT_SAMPLE;
    }
    if (tiku_cmd_streq(s, "wake+sample")) {
        return TIKU_LPAGENT_ACT_WAKE | TIKU_LPAGENT_ACT_SAMPLE;
    }
    return 0u;
}

/** @brief A signed decimal; 0 when there are no digits. */
static int32_t lp_parse_i32(const char *s)
{
    return (*s == '-') ? -(int32_t)tiku_cmd_parse_u32(s + 1)
                       : (int32_t)tiku_cmd_parse_u32(s);
}

/** @brief Join argv[from..argc) into @p out with single spaces; 0 when it
 *         does not fit. */
static int lp_join(char *out, unsigned cap, uint8_t argc, const char *argv[],
                   uint8_t from)
{
    unsigned n = 0u, len;
    uint8_t i;

    out[0] = '\0';
    for (i = from; i < argc; i++) {
        len = (unsigned)strlen(argv[i]);
        if (n + len + (n ? 1u : 0u) >= cap) {
            return 0;
        }
        if (n) {
            out[n++] = ' ';
        }
        memcpy(out + n, argv[i], len + 1u);
        n += len;
    }
    return 1;
}

/** @brief Add @p spec and print the slot, or why it was refused. */
static void lp_add_report(const tiku_lpagent_spec_t *spec)
{
    int slot = tiku_lpagent_add(spec);

    if (slot >= 0) {
        SHELL_PRINTF("lp: job %d added\n", slot);
    } else {
        SHELL_PRINTF("lp: %s\n", slot == -1 ? "table full"
                               : slot == -3 ? "LP core does not start"
                               : "bad job");
    }
}

static void lp_add(uint8_t argc, const char *argv[])
{
    tiku_lpagent_spec_t spec;
    uint8_t cmd_at;

    memset(&spec, 0, sizeof(spec));
    if (argc >= 8u && tiku_cmd_streq(argv[2], "io")) {
        spec.kind = TIKU_LPAGENT_KIND_IO;
        spec.source = (uint8_t)tiku_cmd_parse_u32(argv[3]);
        spec.op = lp_parse_op(argv[4]);
        spec.threshold = lp_parse_i32(argv[5]);
        spec.period_ms = tiku_cmd_parse_u32(argv[6]);
        spec.action = lp_parse_action(argv[7]);
        cmd_at = 8u;
    } else if (argc >= 5u && tiku_cmd_streq(argv[2], "timer")) {
        spec.kind = TIKU_LPAGENT_KIND_TIMER;
        spec.op = TIKU_LPAGENT_OP_ALWAYS;
        spec.period_ms = tiku_cmd_parse_u32(argv[3]);
        spec.action = lp_parse_action(argv[4]);
        cmd_at = 5u;
    } else {
        lp_usage();
        return;
    }
    if (spec.op == 0u || spec.action == 0u ||
        !lp_join(spec.cmd, sizeof(spec.cmd), argc, argv, cmd_at)) {
        lp_usage();
        return;
    }
    lp_add_report(&spec);
}

static void lp_list(void)
{
    tiku_lpagent_spec_t spec;
    tiku_lpagent_wake_t w;
    uint32_t hits;
    unsigned slot, used = 0u;

    for (slot = 0u; slot < TIKU_LPAGENT_JOBS; slot++) {
        if (!tiku_lpagent_get(slot, &spec, &hits)) {
            continue;
        }
        used++;
        if (spec.kind == TIKU_LPAGENT_KIND_IO) {
            SHELL_PRINTF("  %u: io %u %s %ld every %lu ms",
                         slot, spec.source, op_names[spec.op],
                         (long)spec.threshold, (unsigned long)spec.period_ms);
        } else {
            SHELL_PRINTF("  %u: timer every %lu ms", slot,
                         (unsigned long)spec.period_ms);
        }
        SHELL_PRINTF(" ->%s%s  hits %lu%s%s\n",
                     (spec.action & TIKU_LPAGENT_ACT_WAKE) ? " wake" : "",
                     (spec.action & TIKU_LPAGENT_ACT_SAMPLE) ? " sample" : "",
                     (unsigned long)hits, spec.cmd[0] ? "  then: " : "",
                     spec.cmd);
    }
    SHELL_PRINTF("jobs %u/%u  samples %lu\n", used,
                 (unsigned)TIKU_LPAGENT_JOBS,
                 (unsigned long)tiku_lpagent_sample_count());
    if (tiku_lpagent_woken(&w)) {
        SHELL_PRINTF("this boot: woken by job %lu (value %ld)\n",
                     (unsigned long)w.job, (long)w.value);
    }
}

static void lp_samples(void)
{
    uint32_t count = tiku_lpagent_sample_count(), i, ticks, job;
    uint32_t first = count > TIKU_LPAGENT_SAMPLES
                         ? count - TIKU_LPAGENT_SAMPLES : 0u;
    int32_t value;

    SHELL_PRINTF("samples %lu\n", (unsigned long)count);
    for (i = first; i < count; i++) {
        if (tiku_lpagent_sample(i, &ticks, &job, &value)) {
            SHELL_PRINTF("  %lu: t %lu job %lu value %ld\n", (unsigned long)i,
                         (unsigned long)ticks, (unsigned long)job,
                         (long)value);
        }
    }
}

void tiku_shell_cmd_lp(uint8_t argc, const char *argv[])
{
    if (argc < 2u) {
        lp_list();
    } else if (tiku_cmd_streq(argv[1], "add")) {
        lp_add(argc, argv);
    } else if (tiku_cmd_streq(argv[1], "del") && argc >= 3u) {
        if (tiku_lpagent_del(tiku_cmd_parse_u32(argv[2])) != 0) {
            SHELL_PRINTF("lp: no job %s\n", argv[2]);
        }
    } else if (tiku_cmd_streq(argv[1], "samples")) {
        lp_samples();
    } else {
        lp_usage();
    }
}

int tiku_shell_lp_rule(uint8_t argc, const char *argv[])
{
    tiku_lpagent_spec_t spec;
    const char *pin;

    /* on <path> <op> <value> wake [command...] */
    if (argc < 5u || !tiku_cmd_streq(argv[4], "wake")) {
        return 0;
    }
    memset(&spec, 0, sizeof(spec));
    pin = argv[1] + (sizeof(LP_RULE_PREFIX) - 1u);
    spec.op = lp_parse_op(argv[2]);
    if (strncmp(argv[1], LP_RULE_PREFIX, sizeof(LP_RULE_PREFIX) - 1u) != 0 ||
        *pin < '0' || *pin > '9' || tiku_cmd_parse_u32(pin) > 15u ||
        spec.op == 0u || spec.op > TIKU_LPAGENT_OP_LT) {
        SHELL_PRINTF("on: a wake rule watches /dev/gpio/1/0..15 with"
                     " == != > <\n");
        return 1;
    }
    spec.kind = TIKU_LPAGENT_KIND_IO;
    spec.source = (uint8_t)tiku_cmd_parse_u32(pin);
    spec.threshold = lp_parse_i32(argv[3]);
    spec.period_ms = LP_RULE_PERIOD_MS;
    spec.action = TIKU_LPAGENT_ACT_WAKE;
    if (!lp_join(spec.cmd, sizeof(spec.cmd), argc, argv, 5u)) {
        SHELL_PRINTF("on: the command after wake is over %u characters\n",
                     (unsigned)TIKU_LPAGENT_CMD_MAX - 1u);
        return 1;
    }
    lp_add_report(&spec);
    return 1;
}

void tiku_shell_lp_wake_run(void)
{
    tiku_lpagent_wake_t w;
    char line[TIKU_LPAGENT_CMD_MAX];

    if (!tiku_lpagent_woken(&w)) {
        return;
    }
    SHELL_PRINTF("lp: job %lu woke the part (value %ld)\n",
                 (unsigned long)w.job, (long)w.value);
    if (tiku_lpagent_take_wake_cmd(line, sizeof(line))) {
        SHELL_PRINTF("lp: running \"%s\"\n", line);
        tiku_shell_parser_execute(line);
    }
}

#endif /* TIKU_SHELL_CMD_LP */
