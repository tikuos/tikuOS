/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_layout.c - /sys/mem/layout VFS nodes.
 *
 * Reads render one fact a line as "name<TAB>value".  Writes take the same text
 * the shell's layout command builds, and need the system and store capabilities.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_layout.h"
#include "tiku_vfs_tree_data.h"
#include "tiku.h"
#include <kernel/memory/tiku_layout.h>
#include <stdio.h>
#include <string.h>

#if !defined(PLATFORM_MSP430)

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Append formatted text at @p *at, never past @p max. */
#define LAYOUT_PUT(buf, max, at, ...)                                        \
    do {                                                                     \
        if (*(at) < (max)) {                                                 \
            int n_ = snprintf((buf) + *(at), (max) - *(at), __VA_ARGS__);    \
            if (n_ > 0) {                                                    \
                *(at) += ((size_t)n_ < (max) - *(at))                        \
                         ? (size_t)n_ : (max) - *(at) - 1u;                  \
            }                                                                \
        }                                                                    \
    } while (0)

/** @brief A service result as the VFS error it corresponds to. */
static int
layout_vfs_err(int rc)
{
    switch (rc) {
    case TIKU_LAYOUT_OK:       return 0;
    case TIKU_LAYOUT_E_INVAL:
    case TIKU_LAYOUT_E_KNOB:   return TIKU_VFS_EINVAL;
    case TIKU_LAYOUT_E_RANGE:
    case TIKU_LAYOUT_E_STEP:   return TIKU_VFS_ERANGE;
    case TIKU_LAYOUT_E_STALE:  return TIKU_VFS_ESTALE;
    case TIKU_LAYOUT_E_BUSY:   return TIKU_VFS_EBUSY;
    case TIKU_LAYOUT_E_LOSS:
    case TIKU_LAYOUT_E_REUSED: return TIKU_VFS_ECONFLICT;
    case TIKU_LAYOUT_E_IO:     return TIKU_VFS_EIO;
    case TIKU_LAYOUT_E_PHASE:  return TIKU_VFS_ENOENT;
    default:                   return TIKU_VFS_ERR;
    }
}

/** @brief Copy a write's bytes into a terminated string.  @return 0 or -1. */
static int
layout_text(const char *buf, size_t len, char *out, size_t cap)
{
    if (len >= cap) {
        return -1;
    }
    memcpy(out, buf, len);
    out[len] = '\0';
    return 0;
}

/** @brief Parse a decimal operation id.  @return 0 or -1. */
static int
layout_op(const char *buf, size_t len, uint32_t *op)
{
    uint32_t v = 0u;
    size_t i = 0u;

    while (len > 0u && (buf[len - 1u] == '\n' || buf[len - 1u] == '\r' ||
                        buf[len - 1u] == ' ')) {
        len--;
    }
    if (len == 0u) {
        return -1;
    }
    for (; i < len; i++) {
        if (buf[i] < '0' || buf[i] > '9' || v > 429496728u) {
            return -1;
        }
        v = v * 10u + (uint32_t)(buf[i] - '0');
    }
    *op = v;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* READS                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief caps: one knob a line with its effect, floor, ceiling, step, default. */
static int
layout_caps_read(char *buf, size_t max)
{
    const tiku_layout_env_t *e = tiku_layout_env();
    tiku_layout_knob_t k;
    size_t at = 0u;
    unsigned i;

    if (max == 0u) {
        return 0;
    }
    buf[0] = '\0';
    for (i = 0u; i < TIKU_LAYOUT_KNOBS_MAX; i++) {
        if (tiku_layout_knob_env(e, i, &k)) {
            LAYOUT_PUT(buf, max, &at, "%s\t%s\t%lu\t%lu\t%lu\t%lu\n", k.name,
                       (k.effect == TIKU_LAYOUT_EFFECT_STORE) ? "store"
                                                              : "boot",
                       (unsigned long)k.floor, (unsigned long)k.ceiling,
                       (unsigned long)k.step, (unsigned long)k.dflt);
        }
    }
    LAYOUT_PUT(buf, max, &at, "methods\terase\n");
    return (int)at;
}

/** @brief current: generation, revision and the applied value of each knob. */
static int
layout_current_read(char *buf, size_t max)
{
    const tiku_layout_env_t *e = tiku_layout_env();
    const tiku_layout_record_t *r = tiku_layout_record();
    tiku_layout_knob_t k;
    size_t at = 0u;
    unsigned i;
    int valid = tiku_layout_have_record();

    if (max == 0u) {
        return 0;
    }
    buf[0] = '\0';
    LAYOUT_PUT(buf, max, &at, "generation\t%lu\nrevision\t%lu\n",
               valid ? (unsigned long)r->generation : 0UL,
               valid ? (unsigned long)r->revision : 0UL);
    for (i = 0u; i < TIKU_LAYOUT_KNOBS_MAX; i++) {
        if (tiku_layout_knob_env(e, i, &k)) {
            uint32_t v = valid
                ? tiku_layout_kv_get(r->applied, r->n_applied, k.id, k.dflt)
                : k.dflt;
            LAYOUT_PUT(buf, max, &at, "%s\t%lu\n", k.name, (unsigned long)v);
        }
    }
    return (int)at;
}

/** @brief pending: the staged request, or "none". */
static int
layout_pending_read(char *buf, size_t max)
{
    const tiku_layout_env_t *e = tiku_layout_env();
    const tiku_layout_record_t *r = tiku_layout_record();
    tiku_layout_knob_t k;
    size_t at = 0u;
    unsigned i;

    if (max == 0u) {
        return 0;
    }
    buf[0] = '\0';
    if (!tiku_layout_have_record() || r->n_pending == 0u) {
        LAYOUT_PUT(buf, max, &at, "none\n");
        return (int)at;
    }
    LAYOUT_PUT(buf, max, &at, "op\t%lu\nmethod\t%s\n", (unsigned long)r->op,
               (r->method == TIKU_LAYOUT_METHOD_ERASE) ? "erase" : "none");
    for (i = 0u; i < TIKU_LAYOUT_KNOBS_MAX; i++) {
        if (tiku_layout_knob_env(e, i, &k)) {
            uint32_t v = tiku_layout_kv_get(r->pending, r->n_pending, k.id,
                                            0xFFFFFFFFu);
            if (v != 0xFFFFFFFFu) {
                LAYOUT_PUT(buf, max, &at, "%s\t%lu\n", k.name,
                           (unsigned long)v);
            }
        }
    }
    return (int)at;
}

/** @brief status: the store's state, the phase and the last outcome. */
static int
layout_status_read(char *buf, size_t max)
{
    const tiku_layout_state_t *st = tiku_layout_state();
    const tiku_layout_record_t *r = tiku_layout_record();
    int valid = tiku_layout_have_record();
    static const char *const rec_names[] = { "absent", "valid", "foreign" };
    size_t at = 0u;

    if (max == 0u) {
        return 0;
    }
    buf[0] = '\0';
    LAYOUT_PUT(buf, max, &at, "store\t%s\nheld\t%s\nrecord\t%s\n",
               tiku_layout_store_name(st->store),
               tiku_layout_held_name(st->held),
               (st->record < 3u) ? rec_names[st->record] : "-");
    LAYOUT_PUT(buf, max, &at, "tier\t%lu\ncorrected\t%u\n",
               (unsigned long)tiku_layout_base(), (unsigned)st->corrected);
    LAYOUT_PUT(buf, max, &at, "phase\t%s\n",
               (valid && r->phase == TIKU_LAYOUT_PHASE_REWRITING)
                   ? "rewriting" : "none");
    if (valid && r->phase == TIKU_LAYOUT_PHASE_REWRITING) {
        LAYOUT_PUT(buf, max, &at, "op\t%lu\n", (unsigned long)r->op);
    }
    if (st->outcome_op != 0u) {
        LAYOUT_PUT(buf, max, &at, "boot\t%lu %s\n",
                   (unsigned long)st->outcome_op,
                   tiku_layout_err_name(st->outcome));
    }
    if (valid && r->receipt_op != 0u) {
        LAYOUT_PUT(buf, max, &at, "last\t%lu %s\n",
                   (unsigned long)r->receipt_op,
                   tiku_layout_err_name(r->receipt));
    }
    return (int)at;
}

/*---------------------------------------------------------------------------*/
/* WRITES                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief stage: "op=N expect=G:R method=erase nvm.tier=V", for next boot. */
static int
layout_stage_write(const char *buf, size_t len)
{
    char text[128];
    tiku_layout_request_t req;
    tiku_layout_plan_t plan;

    if (layout_text(buf, len, text, sizeof text) != 0 ||
        tiku_layout_parse(text, &req) != 0) {
        return TIKU_VFS_EINVAL;
    }
    return layout_vfs_err(tiku_layout_stage_env(tiku_layout_env(), &req,
                                                &plan));
}

/** @brief cancel: the pending request's operation id. */
static int
layout_cancel_write(const char *buf, size_t len)
{
    uint32_t op;

    if (layout_op(buf, len, &op) != 0) {
        return TIKU_VFS_EINVAL;
    }
    return layout_vfs_err(tiku_layout_cancel_env(tiku_layout_env(), op));
}

/** @brief resume: the interrupted operation's id; /data mounts after it. */
static int
layout_resume_write(const char *buf, size_t len)
{
    uint32_t op;
    int rc;

    if (layout_op(buf, len, &op) != 0) {
        return TIKU_VFS_EINVAL;
    }
    rc = tiku_layout_resume(op);
    if (rc == TIKU_LAYOUT_OK) {
        tiku_vfs_tree_data_retry();
    }
    return layout_vfs_err(rc);
}

/*---------------------------------------------------------------------------*/
/* NODE TABLE                                                                */
/*---------------------------------------------------------------------------*/

#define LAYOUT_CAP  (TIKU_VFS_CAP_SYS | TIKU_VFS_CAP_FS)

const tiku_vfs_node_t tiku_vfs_tree_layout_children[] = {
    { "caps",    TIKU_VFS_FILE, layout_caps_read,    NULL, NULL, 0 },
    { "current", TIKU_VFS_FILE, layout_current_read, NULL, NULL, 0 },
    { "pending", TIKU_VFS_FILE, layout_pending_read, NULL, NULL, 0 },
    { "status",  TIKU_VFS_FILE, layout_status_read,  NULL, NULL, 0 },
    { "stage",   TIKU_VFS_FILE, NULL, layout_stage_write,  NULL, 0, NULL, NULL,
      LAYOUT_CAP },
    { "cancel",  TIKU_VFS_FILE, NULL, layout_cancel_write, NULL, 0, NULL, NULL,
      LAYOUT_CAP },
    { "resume",  TIKU_VFS_FILE, NULL, layout_resume_write, NULL, 0, NULL, NULL,
      LAYOUT_CAP },
};

_Static_assert(sizeof(tiku_vfs_tree_layout_children) /
               sizeof(tiku_vfs_tree_layout_children[0])
               == TIKU_VFS_TREE_LAYOUT_NCHILD,
               "TIKU_VFS_TREE_LAYOUT_NCHILD out of sync");

#endif /* !PLATFORM_MSP430 */
