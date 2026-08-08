/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_npu_iface.c - interfaces/npu backed by the RA8P1's Ethos-U55.
 *
 * The contract's shape is the accelerator's, not this part's; everything here
 * is a translation of one vocabulary into the other.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/npu/tiku_npu.h>

#include "tiku_npu_arch.h"

/** @brief Sticky: a fault is only cleared by loading a model again. */
static uint8_t npu_iface_faulted;

uint32_t tiku_npu_flags(void)
{
    /* Both are facts about the part: models come from the store because the
     * image must not grow with them, and the Ethos-U55 has no float path. */
    return TIKU_NPU_F_STORE_MODEL | TIKU_NPU_F_INT_ONLY;
}

tiku_npu_state_t tiku_npu_state(void)
{
    if (!tiku_ra8p1_npu_ready()) {
        return TIKU_NPU_GATED;
    }
    if (npu_iface_faulted) {
        return TIKU_NPU_FAULTED;
    }
    return (tiku_ra8p1_npu_model()->cms_len != 0u) ? TIKU_NPU_READY
                                                   : TIKU_NPU_IDLE;
}

int tiku_npu_start(void)
{
    return (tiku_ra8p1_npu_init() == TIKU_RA8P1_NPU_OK) ? TIKU_NPU_OK
                                                        : TIKU_NPU_ERR_STATE;
}

void tiku_npu_stop(void)
{
    tiku_ra8p1_npu_stop();
}

int tiku_npu_load(const char *name)
{
    int rc = tiku_ra8p1_npu_load(name);

    if (rc != TIKU_RA8P1_NPU_OK) {
        return TIKU_NPU_ERR_MODEL;
    }
    npu_iface_faulted = 0u;      /* a fresh model is the way out of a fault */
    return TIKU_NPU_OK;
}

int tiku_npu_info(tiku_npu_info_t *out)
{
    const tiku_ra8p1_npu_model_t *m = tiku_ra8p1_npu_model();

    if (out == 0 || !tiku_ra8p1_npu_ready()) {
        return TIKU_NPU_ERR_STATE;
    }
    out->macs      = tiku_ra8p1_npu_macs();
    out->shram_kb  = tiku_ra8p1_npu_shram_kb();
    out->arena     = m->arena;
    out->in_bytes  = (uint32_t)m->ifm_dim * m->ifm_dim;
    out->out_bytes = (uint32_t)m->ofm_dim * m->ofm_dim;
    return TIKU_NPU_OK;
}

void *tiku_npu_input(uint32_t *len)
{
    const tiku_ra8p1_npu_model_t *m = tiku_ra8p1_npu_model();

    if (len != 0) {
        *len = (uint32_t)m->ifm_dim * m->ifm_dim;
    }
    return tiku_ra8p1_npu_ifm();
}

const void *tiku_npu_output(uint32_t *len)
{
    const tiku_ra8p1_npu_model_t *m = tiku_ra8p1_npu_model();

    if (len != 0) {
        *len = (uint32_t)m->ofm_dim * m->ofm_dim;
    }
    return tiku_ra8p1_npu_ofm();
}

int tiku_npu_run(void)
{
    int rc = tiku_ra8p1_npu_run((uint32_t *)0);

    switch (rc) {
    case TIKU_RA8P1_NPU_OK:
        return TIKU_NPU_OK;
    case TIKU_RA8P1_NPU_ERR_IMAGE:
        return TIKU_NPU_ERR_STATE;
    case TIKU_RA8P1_NPU_ERR_TIMEOUT:
        npu_iface_faulted = 1u;
        return TIKU_NPU_ERR_TIMEOUT;
    default:
        npu_iface_faulted = 1u;
        return TIKU_NPU_ERR_FAULT;
    }
}

uint32_t tiku_npu_runs(void)
{
    return tiku_ra8p1_npu_runs();
}
