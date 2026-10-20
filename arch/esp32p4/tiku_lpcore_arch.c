/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lpcore_arch.c - P4 LP core launch, park, relaunch and mailbox.
 *
 * The launch follows ESP-IDF ulp_lp_core_run() at the pin with the LP ROM
 * skipped: boot address, reset pulse, clock, wake source, HP trigger.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "tiku_lpcore_arch.h"
#include "tiku_esp32p4_regs.h"
#include "lpcore/tiku_lpcore_ipc.h"

#ifndef TIKU_P4_FENCE
#define TIKU_P4_FENCE() __asm__ volatile("fence iorw, iorw" ::: "memory")
#endif

/* The payload binary, wrapped as an object by the Makefile's sub-build and
 * linked into the XIP window; a host model supplies its own. */
#ifndef TIKU_P4_LPCORE_IMAGE
extern const uint8_t _binary_tiku_lpcore_bin_start[];
extern const uint8_t _binary_tiku_lpcore_bin_end[];
#define TIKU_P4_LPCORE_IMAGE _binary_tiku_lpcore_bin_start
#define TIKU_P4_LPCORE_IMAGE_SIZE \
    ((uint32_t)(_binary_tiku_lpcore_bin_end - _binary_tiku_lpcore_bin_start))
#endif

#ifndef TIKU_P4_LPCORE_WAIT
/** @brief Polls for a payload answer: tens of milliseconds at 360 MHz. */
#define TIKU_P4_LPCORE_WAIT 2000000u
#endif

/** @brief Set once the image is in LP SRAM, for the rest of the power-on. */
static uint8_t lpcore_loaded;
/** @brief Set by a launch, cleared when a stop holds the core in reset. */
static uint8_t lpcore_launched;
/** @brief The payload's l2h_seq at the last capture. */
static uint32_t lpcore_pulled_seq;

/** @brief Set or clear @p bits of the register at @p address. */
static void reg_bits(uintptr_t address, uint32_t bits, int set)
{
    uint32_t value = TIKU_P4_REG_READ(address);
    TIKU_P4_REG_WRITE(address, set ? (value | bits) : (value & ~bits));
}

/** @brief Hold the core in reset with its clock off, as boot leaves it. */
static void lpcore_hold(void)
{
    reg_bits(TIKU_P4_LP_RESET, TIKU_P4_LP_CORE_BIT, 1);
    reg_bits(TIKU_P4_LP_CLK, TIKU_P4_LP_CORE_BIT, 0);
    lpcore_launched = 0u;
}

/**
 * @brief Release the core at the reset entry and wait for the magic.
 * @return 0 once published, -1 otherwise (the core is held in reset)
 */
static int lpcore_launch(void)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    uint32_t spin;

    sh->magic = 0u;
    sh->cmd = 0u;
    sh->rsp = 0u;
    TIKU_P4_FENCE();
    TIKU_P4_REG_WRITE(TIKU_P4_PMU_HP_INT_CLR, TIKU_P4_LP_CPU_EXC_INT);
    TIKU_P4_REG_WRITE(TIKU_P4_LP_SYS_BOOT_ADDR, TIKU_LPCORE_ENTRY);
    reg_bits(TIKU_P4_LP_RESET, TIKU_P4_LP_CORE_BIT, 1);
    reg_bits(TIKU_P4_LP_RESET, TIKU_P4_LP_CORE_BIT, 0);
    reg_bits(TIKU_P4_LP_CLK, TIKU_P4_LP_CORE_BIT, 1);
    reg_bits(TIKU_P4_PMU_LP_CPU_PWR0,
             TIKU_P4_LP_SLP_STALL_EN | TIKU_P4_LP_SLP_RESET_EN, 1);
    reg_bits(TIKU_P4_LPPERI_CPU, TIKU_P4_LP_CORE_BIT, 0);
    TIKU_P4_REG_WRITE(TIKU_P4_PMU_LP_CPU_PWR2, TIKU_P4_LP_WAKE_BY_HP);
    TIKU_P4_REG_WRITE(TIKU_P4_PMU_HP_LP_COMM, TIKU_P4_HP_TRIGGER_LP);
    lpcore_launched = 1u;
    for (spin = 0u; spin < TIKU_P4_LPCORE_WAIT; spin++) {
        if (sh->magic == TIKU_LPCORE_MAGIC) {
            return 0;
        }
    }
    lpcore_hold();
    return -1;
}

void tiku_p4_lpcore_boot(void)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    uint32_t size = TIKU_P4_LPCORE_IMAGE_SIZE;

    if (!(TIKU_P4_REG_READ(TIKU_P4_LP_CLK) & TIKU_P4_LP_CORE_BIT) ||
        (TIKU_P4_REG_READ(TIKU_P4_LP_RESET) & TIKU_P4_LP_CORE_BIT)) {
        return;
    }
    if (size != 0u && size <= TIKU_LPCORE_IMAGE_MAX &&
        (sh->magic == TIKU_LPCORE_MAGIC ||
         sh->magic == TIKU_LPCORE_MAGIC_FAULT) &&
        memcmp(TIKU_LPCORE_PTR(TIKU_LPCORE_RAM_BASE), TIKU_P4_LPCORE_IMAGE,
               size) == 0) {
        lpcore_loaded = 1u;
        lpcore_launched = 1u;
        lpcore_pulled_seq = sh->l2h_seq;
        return;
    }
    lpcore_hold();
}

int tiku_p4_lpcore_start(void)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    uint32_t size = TIKU_P4_LPCORE_IMAGE_SIZE;

    if (!lpcore_loaded) {
        if (size == 0u || size > TIKU_LPCORE_IMAGE_MAX) {
            return -1;
        }
        lpcore_hold();
        memcpy(TIKU_LPCORE_PTR(TIKU_LPCORE_RAM_BASE), TIKU_P4_LPCORE_IMAGE,
               size);
        memset(TIKU_LPCORE_PTR(TIKU_LPCORE_SHARED_ADDR), 0,
               TIKU_LPCORE_SHARED_SIZE);
        TIKU_P4_FENCE();
        lpcore_pulled_seq = 0u;     /* the page's l2h_seq, just cleared */
        lpcore_loaded = 1u;
        return lpcore_launch();
    }
    if (!lpcore_launched || sh->magic != TIKU_LPCORE_MAGIC) {
        return lpcore_launch();
    }
    if (sh->rsp == TIKU_LPCORE_RSP_PARKED) {
        uint32_t spin;

        sh->cmd = TIKU_LPCORE_CMD_RESUME;
        TIKU_P4_FENCE();
        for (spin = 0u; spin < TIKU_P4_LPCORE_WAIT; spin++) {
            if (sh->rsp != TIKU_LPCORE_RSP_PARKED) {
                return 0;
            }
        }
        return -1;
    }
    return 0;
}

int tiku_p4_lpcore_stop(void)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    uint32_t spin;

    if (!lpcore_launched) {
        return 0;
    }
    /* A faulted payload, or one that never published, answers nothing. */
    if (sh->magic != TIKU_LPCORE_MAGIC) {
        lpcore_hold();
        return 0;
    }
    if (sh->rsp == TIKU_LPCORE_RSP_PARKED) {
        return 0;
    }
    sh->cmd = TIKU_LPCORE_CMD_PARK;
    TIKU_P4_FENCE();
    for (spin = 0u; spin < TIKU_P4_LPCORE_WAIT; spin++) {
        if (sh->rsp == TIKU_LPCORE_RSP_PARKED) {
            return 0;
        }
    }
    lpcore_hold();
    return -1;
}

int tiku_p4_lpcore_launched(void)
{
    return lpcore_launched;
}

uint32_t tiku_p4_lpcore_magic(void)
{
    return lpcore_launched ? TIKU_LPCORE_SHARED->magic : 0u;
}

int tiku_p4_lpcore_running(void)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;

    return (lpcore_launched && sh->magic == TIKU_LPCORE_MAGIC &&
            sh->rsp != TIKU_LPCORE_RSP_PARKED) ? 1 : 0;
}

uint32_t tiku_p4_lpcore_heartbeat(void)
{
    return lpcore_loaded ? TIKU_LPCORE_SHARED->heartbeat : 0u;
}

uint32_t tiku_p4_lpcore_image_size(void)
{
    return TIKU_P4_LPCORE_IMAGE_SIZE;
}

/*---------------------------------------------------------------------------*/
/* Mailbox (polled both ways)                                                */
/*---------------------------------------------------------------------------*/

static uint8_t lpcore_reply_buf[TIKU_LPCORE_MSG_CAP];
static uint32_t lpcore_reply_len;
static uint32_t lpcore_reply_count;

int tiku_p4_lpcore_send(const void *data, uint32_t len)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    uint32_t i;

    if (len > TIKU_LPCORE_MSG_CAP || !tiku_p4_lpcore_running()) {
        return -1;
    }
    for (i = 0u; i < len; i++) {
        sh->h2l_buf[i] = ((const uint8_t *)data)[i];
    }
    sh->h2l_len = len;
    TIKU_P4_FENCE();
    sh->h2l_seq = sh->h2l_seq + 1u;
    return 0;
}

void tiku_p4_lpcore_poll(void)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    uint32_t seq, len, i;

    if (!lpcore_loaded) {
        return;
    }
    seq = sh->l2h_seq;
    if (seq == lpcore_pulled_seq) {
        return;
    }
    lpcore_pulled_seq = seq;
    TIKU_P4_FENCE();
    len = sh->l2h_len;
    if (len > TIKU_LPCORE_MSG_CAP) {
        len = TIKU_LPCORE_MSG_CAP;
    }
    for (i = 0u; i < len; i++) {
        lpcore_reply_buf[i] = sh->l2h_buf[i];
    }
    lpcore_reply_len = len;
    lpcore_reply_count++;
}

uint32_t tiku_p4_lpcore_reply_seq(void)
{
    return lpcore_reply_count;
}

uint32_t tiku_p4_lpcore_reply(void *out, uint32_t cap)
{
    uint32_t len = lpcore_reply_len < cap ? lpcore_reply_len : cap;

    memcpy(out, lpcore_reply_buf, len);
    return len;
}

void tiku_p4_lpcore_info(tiku_p4_lpcore_info_t *out)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;

    out->reset_cause = TIKU_P4_REG_READ(TIKU_P4_RESET_CAUSE);
    out->dbg_pc = TIKU_P4_REG_READ(TIKU_P4_LP_SYS_DBG_PC);
    out->exc_pc = TIKU_P4_REG_READ(TIKU_P4_LP_SYS_EXC_PC);
    out->exc_int = (TIKU_P4_REG_READ(TIKU_P4_PMU_HP_INT_RAW) &
                    TIKU_P4_LP_CPU_EXC_INT) ? 1u : 0u;
    out->pwr0 = TIKU_P4_REG_READ(TIKU_P4_PMU_LP_CPU_PWR0);
    out->pwr2 = TIKU_P4_REG_READ(TIKU_P4_PMU_LP_CPU_PWR2);
    out->clk_on = (TIKU_P4_REG_READ(TIKU_P4_LP_CLK) &
                   TIKU_P4_LP_CORE_BIT) ? 1u : 0u;
    out->in_reset = (TIKU_P4_REG_READ(TIKU_P4_LP_RESET) &
                     TIKU_P4_LP_CORE_BIT) ? 1u : 0u;
    out->lp_io_in = TIKU_P4_REG_READ(TIKU_P4_LP_GPIO_IN) & 0xFFFFu;
    out->boots = lpcore_loaded ? sh->boots : 0u;
    out->fault_count = lpcore_loaded ? sh->fault_count : 0u;
    out->fault_cause = lpcore_loaded ? sh->fault_cause : 0u;
    out->fault_epc = lpcore_loaded ? sh->fault_epc : 0u;
    out->fault_tval = lpcore_loaded ? sh->fault_tval : 0u;
}

int tiku_p4_lpcore_command(uint32_t cmd, uint32_t a0, uint32_t a1,
                           uint32_t a2, uint32_t a3)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    uint32_t spin;

    if (!tiku_p4_lpcore_running()) {
        return -1;
    }
    sh->arg[0] = a0;
    sh->arg[1] = a1;
    sh->arg[2] = a2;
    sh->arg[3] = a3;
    sh->rsp = 0u;
    TIKU_P4_FENCE();
    sh->cmd = cmd;
    /* The payload writes .rsp, then a fence, then clears .cmd. */
    for (spin = 0u; spin < TIKU_P4_LPCORE_WAIT; spin++) {
        if (sh->cmd == 0u) {
            TIKU_P4_FENCE();
            return (int)sh->rsp;
        }
    }
    lpcore_hold();
    return -1;
}

void tiku_p4_lpcore_counters(tiku_p4_lpcore_counters_t *out)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    uint32_t hi;

    if (!lpcore_loaded) {
        memset(out, 0, sizeof(*out));
        return;
    }
    out->cycles = sh->cycles;
    do {
        hi = sh->lp_ticks_hi;
        out->lp_ticks = ((uint64_t)hi << 32) | sh->lp_ticks_lo;
    } while (sh->lp_ticks_hi != hi);
    out->led_level = sh->led_level;
    out->wake_mode = sh->wake_mode;
    out->wake_count = sh->wake_count;
}

int tiku_p4_lpcore_i2c_result(uint8_t found[16])
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    uint32_t i;

    for (i = 0u; i < 16u; i++) {
        found[i] = lpcore_loaded ? sh->i2c_found[i] : 0u;
    }
    if (!lpcore_loaded) {
        return 0;
    }
    return sh->i2c_scl_stuck ? -1 : (int)sh->i2c_count;
}
