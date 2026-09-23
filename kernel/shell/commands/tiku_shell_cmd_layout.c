/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_layout.c - "layout" command implementation.
 *
 * A thin front on the layout service: it builds the request text the service
 * parses and turns each refusal into a sentence with a value that would pass.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"
#include <kernel/shell/tiku_shell.h>

#if TIKU_SHELL_CMD_LAYOUT

#include "tiku_shell_cmd_layout.h"
#include <kernel/memory/tiku_layout.h>
#include <kernel/fs/tiku_tfs.h>
#include <kernel/vfs/tree/tiku_vfs_tree_data.h>
#include <stdint.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief The name of knob @p id on this board, or "?". */
static const char *
layout_knob_name(uint8_t id, tiku_layout_knob_t *k)
{
    unsigned i;

    for (i = 0u; i < TIKU_LAYOUT_KNOBS_MAX; i++) {
        if (tiku_layout_knob_env(tiku_layout_env(), i, k) && k->id == id) {
            return k->name;
        }
    }
    memset(k, 0, sizeof *k);
    return "?";
}

/** @brief Whether a valid record exists to read generation and values from. */
static int
layout_have_record(void)
{
    return tiku_layout_have_record();
}

/** @brief Append @p s to @p out, keeping room for the terminator. */
static int
layout_cat(char *out, size_t cap, size_t *at, const char *s)
{
    size_t n = strlen(s);

    if (*at + n + 1u >= cap) {
        return -1;
    }
    memcpy(out + *at, s, n);
    *at += n;
    out[(*at)++] = ' ';
    out[*at] = '\0';
    return 0;
}

/**
 * @brief Turn knob values and --identity/--expect/--op/--erase into service text.
 *
 * @return 0, or -1 on a malformed or overlong argument list.
 */
static int
layout_request_text(uint8_t argc, const char *argv[], char *out, size_t cap)
{
    size_t at = 0u;
    uint8_t i;

    out[0] = '\0';
    for (i = 2u; i < argc; i++) {
        int rc;

        if (strcmp(argv[i], "--erase") == 0) {
            rc = layout_cat(out, cap, &at, "method=erase");
        } else if (strcmp(argv[i], "--expect") == 0 && i + 1u < argc) {
            rc = layout_cat(out, cap, &at, "expect=");
            if (rc != 0) { return -1; }
            at--;                                /* no space after the '=' */
            rc |= layout_cat(out, cap, &at, argv[++i]);
        } else if (strcmp(argv[i], "--op") == 0 && i + 1u < argc) {
            rc = layout_cat(out, cap, &at, "op=");
            if (rc != 0) { return -1; }
            at--;
            rc |= layout_cat(out, cap, &at, argv[++i]);
        } else if (strcmp(argv[i], "--identity") == 0 && i + 1u < argc) {
            rc = layout_cat(out, cap, &at, "identity=");
            if (rc != 0) { return -1; }
            at--;
            rc = layout_cat(out, cap, &at, argv[++i]);
        } else if (argv[i][0] == '-') {
            return -1;
        } else {
            rc = layout_cat(out, cap, &at, argv[i]);
        }
        if (rc != 0) {
            return -1;
        }
    }
    return 0;
}

/** @brief Explain a refusal from plan or stage in one or two lines. */
static void
layout_refusal(const char *verb, int rc, const tiku_layout_plan_t *plan)
{
    tiku_layout_knob_t k;
    const tiku_layout_record_t *r = tiku_layout_record();
    const char *name = layout_knob_name(plan->bad_knob, &k);

    switch (rc) {
    case TIKU_LAYOUT_E_RANGE:
        SHELL_PRINTF(SH_RED "layout %s: %s must lie in %lu..%lu" SH_RST "\n",
                     verb, name, (unsigned long)k.floor,
                     (unsigned long)k.ceiling);
        SHELL_PRINTF("  nearest allowed value: %lu\n",
                     (unsigned long)plan->nearest);
        break;
    case TIKU_LAYOUT_E_STEP:
        SHELL_PRINTF(SH_RED "layout %s: %s moves in steps of %lu" SH_RST "\n",
                     verb, name, (unsigned long)k.step);
        SHELL_PRINTF("  nearest allowed value: %lu\n",
                     (unsigned long)plan->nearest);
        break;
    case TIKU_LAYOUT_E_KNOB:
        SHELL_PRINTF(SH_RED "layout %s: no such knob on this board" SH_RST
                     "\n", verb);
        break;
    case TIKU_LAYOUT_E_LOSS:
        SHELL_PRINTF(SH_RED "layout %s: /data holds %u files (%lu bytes) "
                     "that the change erases" SH_RST "\n", verb,
                     (unsigned)plan->files, (unsigned long)plan->bytes);
        SHELL_PRINTF("  add --erase to discard them\n");
        break;
    case TIKU_LAYOUT_E_STALE:
        SHELL_PRINTF(SH_RED "layout %s: the layout changed; it is now at "
                     "%lu:%lu" SH_RST "\n", verb,
                     layout_have_record() ? (unsigned long)r->generation : 0UL,
                     layout_have_record() ? (unsigned long)r->revision : 0UL);
        break;
    case TIKU_LAYOUT_E_BUSY:
        SHELL_PRINTF(SH_RED "layout %s: operation %lu is pending; cancel it "
                     "first" SH_RST "\n", verb, (unsigned long)r->op);
        break;
    case TIKU_LAYOUT_E_REUSED:
        SHELL_PRINTF(SH_RED "layout %s: operation id already names another "
                     "request" SH_RST "\n", verb);
        break;
    case TIKU_LAYOUT_E_RECOVERY:
        SHELL_PRINTF("layout %s: ownership is unavailable; inspect 'layout "
                     "inspect', then explicitly recover the known store\n", verb);
        break;
    default:
        SHELL_PRINTF(SH_RED "layout %s: %s" SH_RST "\n", verb,
                     tiku_layout_err_name(rc));
        break;
    }
}

/*---------------------------------------------------------------------------*/
/* SUBCOMMANDS                                                               */
/*---------------------------------------------------------------------------*/

/** @brief layout limits: every knob with its floor, ceiling and step. */
static void
layout_limits(void)
{
    tiku_layout_knob_t k;
    unsigned i;

    SHELL_PRINTF("%-10s %-6s %10s %10s %8s %10s\n",
                 "knob", "effect", "floor", "ceiling", "step", "default");
    for (i = 0u; i < TIKU_LAYOUT_KNOBS_MAX; i++) {
        if (tiku_layout_knob_env(tiku_layout_env(), i, &k)) {
            SHELL_PRINTF("%-10s %-6s %10lu %10lu %8lu %10lu\n", k.name,
                         (k.effect == TIKU_LAYOUT_EFFECT_STORE) ? "store"
                                                                : "boot",
                         (unsigned long)k.floor, (unsigned long)k.ceiling,
                         (unsigned long)k.step, (unsigned long)k.dflt);
        }
    }
    SHELL_PRINTF("A store change rewrites /data at the next boot.\n");
}

/** @brief layout status: the store's state and the last operation. */
static void
layout_status(void)
{
    const tiku_layout_state_t *st = tiku_layout_state();
    const tiku_layout_record_t *r = tiku_layout_record();
    int valid = layout_have_record();

    SHELL_PRINTF("store:   %s", tiku_layout_store_name(st->store));
    if (st->store == TIKU_LAYOUT_STORE_HELD) {
        SHELL_PRINTF(" (%s)", tiku_layout_held_name(st->held));
    }
    SHELL_PRINTF("\nrecord:  %s\n",
                 (st->record == TIKU_LAYOUT_RECORD_VALID) ? "valid"
                 : (st->record == TIKU_LAYOUT_RECORD_FOREIGN) ? "foreign"
                 : "absent");
    if (st->corrected) {
        SHELL_PRINTF("the store was found at %lu, not where the record "
                     "said\n", (unsigned long)st->tier);
    }
    if (st->store == TIKU_LAYOUT_STORE_HELD) {
        SHELL_PRINTF("NVM tier withheld; resolve the hold before using /data.\n");
    }
    if (st->outcome_op != 0u) {
        SHELL_PRINTF("at boot: operation %lu %s\n",
                     (unsigned long)st->outcome_op,
                     tiku_layout_err_name(st->outcome));
    }
    if (valid && r->phase == TIKU_LAYOUT_PHASE_REWRITING) {
        SHELL_PRINTF(SH_RED "operation %lu was interrupted while rewriting "
                     "/data" SH_RST "\n", (unsigned long)r->op);
        if (st->held == TIKU_LAYOUT_HELD_INTERRUPTED) {
            SHELL_PRINTF("  'layout resume %lu' finishes it; its files were "
                         "already given up\n", (unsigned long)r->op);
        } else {
            SHELL_PRINTF("  resume is refused until the image contract and "
                         "layout bounds match\n");
        }
    } else if (valid && r->receipt_op != 0u) {
        SHELL_PRINTF("last:    operation %lu %s\n",
                     (unsigned long)r->receipt_op,
                     tiku_layout_err_name(r->receipt));
    }
}

/** @brief layout show: the applied map, then any pending request. */
static void
layout_show(void)
{
    const tiku_layout_record_t *r = tiku_layout_record();
    tiku_layout_knob_t k;
    int valid = layout_have_record();
    unsigned i;
    char identity[33];

    SHELL_PRINTF("layout %lu:%lu (generation:revision)\n",
                 valid ? (unsigned long)r->generation : 0UL,
                 valid ? (unsigned long)r->revision : 0UL);
    tiku_layout_identity_text(r->identity, identity);
    SHELL_PRINTF("identity %s\n", valid ? identity : "none (recover first)");
    for (i = 0u; i < TIKU_LAYOUT_KNOBS_MAX; i++) {
        uint32_t now, next;

        if (!tiku_layout_knob_env(tiku_layout_env(), i, &k)) {
            continue;
        }
        now  = valid ? tiku_layout_kv_get(r->applied, r->n_applied, k.id,
                                          k.dflt)
                     : k.dflt;
        next = (valid && r->n_pending != 0u)
             ? tiku_layout_kv_get(r->pending, r->n_pending, k.id, now) : now;
        SHELL_PRINTF("  %-10s %10lu", k.name, (unsigned long)now);
        if (next != now) {
            SHELL_PRINTF("  -> %lu at next boot", (unsigned long)next);
        }
        SHELL_PRINTF("\n");
    }
    if (valid && r->n_pending != 0u) {
        SHELL_PRINTF("pending: operation %lu%s\n", (unsigned long)r->op,
                     (r->method == TIKU_LAYOUT_METHOD_ERASE)
                         ? ", erasing /data" : "");
    }
    layout_status();
}

/** @brief layout plan / stage: validate a request, and record it for stage. */
static void
layout_request(uint8_t argc, const char *argv[], int stage)
{
    char text[128];
    tiku_layout_request_t req;
    tiku_layout_plan_t plan;
    const char *verb = stage ? "stage" : "plan";
    int rc;

    if (argc < 3u || layout_request_text(argc, argv, text, sizeof text) != 0 ||
        tiku_layout_parse(text, &req) != 0 || req.n == 0u) {
        SHELL_PRINTF("Usage: layout %s <knob>=<value> ... [--erase]%s\n", verb,
                     stage ? " --expect G:R --op N --identity HEX32" : "");
        return;
    }
    if (stage && (req.op == 0u || !req.has_expect)) {
        const tiku_layout_record_t *r = tiku_layout_record();

        SHELL_PRINTF("layout stage: name the operation with --op N and the "
                     "layout you saw with --expect %lu:%lu and --identity HEX32 "
                     "from 'layout show'\n",
                     layout_have_record() ? (unsigned long)r->generation : 0UL,
                     layout_have_record() ? (unsigned long)r->revision : 0UL);
        return;
    }
    rc = stage ? tiku_layout_stage_env(tiku_layout_env(), &req, &plan)
               : tiku_layout_plan_env(tiku_layout_env(), &req, &plan);
    if (rc != TIKU_LAYOUT_OK) {
        layout_refusal(verb, rc, &plan);
        return;
    }
    if (stage && tiku_layout_record()->op != req.op) {
        if (tiku_layout_record()->receipt_op == req.op) {
            SHELL_PRINTF("layout stage: operation %lu already finished; nothing staged\n",
                         (unsigned long)req.op);
        } else {
            SHELL_PRINTF("layout stage: nothing changes; nothing staged\n");
        }
        return;
    }
    if (plan.effect == TIKU_LAYOUT_EFFECT_NONE && !stage) {
        SHELL_PRINTF("layout plan: nothing changes\n");
        return;
    }
    if (plan.effect == TIKU_LAYOUT_EFFECT_STORE) {
        if (plan.files != 0u) {
            SHELL_PRINTF("/data is rewritten at the next boot: %u files "
                         "(%lu bytes) are erased\n", (unsigned)plan.files,
                         (unsigned long)plan.bytes);
        } else {
            SHELL_PRINTF("/data is rewritten at the next boot; it holds no "
                         "files\n");
        }
    }
    if (stage) {
        SHELL_PRINTF("staged operation %lu; reboot to apply, or "
                     "'layout cancel %lu'\n", (unsigned long)req.op,
                     (unsigned long)req.op);
    }
}

/** @brief Parse a decimal operation id from @p s.  @return 0 or -1. */
static int
layout_op_arg(const char *s, uint32_t *op)
{
    uint32_t v = 0u;

    if (s == NULL || *s == '\0') {
        return -1;
    }
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9' ||
            v > (UINT32_MAX - (uint32_t)(*s - '0')) / 10u) {
            return -1;
        }
        v = v * 10u + (uint32_t)(*s - '0');
    }
    *op = v;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* COMMAND                                                                   */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_layout(uint8_t argc, const char *argv[])
{
    const char *sub = (argc >= 2u) ? argv[1] : "show";
    uint32_t op;
    int rc;

    if (tiku_layout_state()->store == TIKU_LAYOUT_STORE_NONE) {
        SHELL_PRINTF("layout: this part has no NVM region to divide\n");
        return;
    }
    if (strcmp(sub, "show") == 0) {
        layout_show();
    } else if (strcmp(sub, "limits") == 0) {
        layout_limits();
    } else if (strcmp(sub, "status") == 0) {
        layout_status();
    } else if (strcmp(sub, "inspect") == 0) {
        const tiku_layout_env_t *e = tiku_layout_env();
        tiku_tfs_cand_t candidates[8];
        int n = tiku_tfs_locate(&e->region, e->step, candidates, 8);
        SHELL_PRINTF("Candidates only, not proof of ownership; no bytes changed.\n");
        for (int i = 0; i < n && i < 8; i++) {
            SHELL_PRINTF("  %lu %s\n", (unsigned long)candidates[i].off,
                         tiku_tfs_probe_name(candidates[i].kind));
        }
        SHELL_PRINTF("%d candidate(s)%s\n", n, n > 8 ? " (first 8 shown)" : "");
    } else if (strcmp(sub, "recover") == 0) {
        tiku_layout_request_t q;
        if (argc != 4u || strcmp(argv[3], "--accept-layout") != 0 ||
            tiku_layout_parse(argv[2], &q) != 0 || q.n != 1u ||
            q.kv[0].id != TIKU_KNOB_NVM_TIER) {
            SHELL_PRINTF("Usage: layout recover nvm.tier=<known-store-offset> "
                         "--accept-layout\nNo files are formatted. Verify the "
                         "offset from your previous layout, not a scan alone.\n");
            return;
        }
        rc = tiku_layout_recover(q.kv[0].value);
        SHELL_PRINTF("layout recover: %s%s\n", tiku_layout_err_name(rc),
                     rc == TIKU_LAYOUT_OK ? "; reboot to activate" : "");
    } else if (strcmp(sub, "plan") == 0) {
        layout_request(argc, argv, 0);
    } else if (strcmp(sub, "stage") == 0) {
        layout_request(argc, argv, 1);
    } else if (strcmp(sub, "cancel") == 0 || strcmp(sub, "resume") == 0) {
            if (argc != 3u || layout_op_arg(argv[2], &op) != 0) {
            SHELL_PRINTF("Usage: layout %s <operation>\n", sub);
            return;
        }
        if (sub[0] == 'c') {
            rc = tiku_layout_cancel_env(tiku_layout_env(), op);
        } else {
            rc = tiku_layout_resume(op);
            if (rc == TIKU_LAYOUT_OK) {
                tiku_vfs_tree_data_retry();
            }
        }
        if (rc != TIKU_LAYOUT_OK) {
            SHELL_PRINTF(SH_RED "layout %s: %s" SH_RST "\n", sub,
                         tiku_layout_err_name(rc));
            return;
        }
        SHELL_PRINTF("layout %s: operation %lu %s\n", sub, (unsigned long)op,
                     (sub[0] == 'c') ? "cancelled"
                                     : "finished; reboot to activate /data and the NVM tier");
    } else {
        SHELL_PRINTF("Usage: layout [show|limits|status|plan|stage|cancel|"
                     "resume|inspect|recover]\n");
    }
}

#endif /* TIKU_SHELL_CMD_LAYOUT */
