/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_npu_arch.c - RA8P1 Ethos-U55 bring-up, model loading and runs.
 *
 * The NPU sits behind a power domain and a module stop, both closed out of
 * reset; UM 11.5.1 powers the domain before the module stop is released.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_npu_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"

/** @brief Iteration bound on each power-gating and soft-reset wait. */
#define NPU_POWER_SPINS     100000UL

/* DWT cycle counter. */
#define NPU_DEMCR       0xE000EDFCUL
#define NPU_DEMCR_TRCENA (1UL << 24)
#define NPU_DWT_CTRL    0xE0001000UL
#define NPU_DWT_CYCCNT  0xE0001004UL

/**
 * @brief Read the DWT cycle counter, enabling it first; wraps every 2^32.
 *
 * DEMCR.TRCENA gates the whole trace block: with no debugger attached the
 * counter reads zero until TRCENA is set.
 */
static uint32_t npu_cycles(void)
{
    TIKU_REG32(NPU_DEMCR)    |= NPU_DEMCR_TRCENA;
    TIKU_REG32(NPU_DWT_CTRL) |= 1UL;
    return TIKU_REG32(NPU_DWT_CYCCNT);
}

volatile uint32_t tiku_ra8p1_npu_irq_count;

/** @brief Set by the completion interrupt; cleared before each submit. */
static volatile uint8_t npu_done;

/** @brief Set when tiku_ra8p1_npu_init() succeeds; cleared by the stop. */
static uint8_t npu_ready;

/**
 * @brief Unlock the PRC1-guarded registers, or lock every PRCR_S group.
 *
 * @param unlock  Non-zero to allow PRC1 writes, zero to protect again
 */
static void npu_protect(int unlock)
{
    TIKU_REG16(RA8P1_PRCR_S) = (uint16_t)(RA8P1_PRCR_KEY |
                                          (unlock ? RA8P1_PRCR_PRC1 : 0U));
}

/**
 * @brief Spin until the PDCTRNPU bits in @p mask read as @p want.
 *
 * PDCSF rises some cycles after the PDDE write, so a read straight after the
 * write still shows the previous state.
 *
 * @param mask  Bits to compare
 * @param want  Value those bits must reach
 * @return 1 when they did within NPU_POWER_SPINS reads, 0 otherwise
 */
static int npu_wait(uint8_t mask, uint8_t want)
{
    unsigned long spins;

    for (spins = NPU_POWER_SPINS; spins != 0UL; spins--) {
        if ((TIKU_REG8(RA8P1_PDCTRNPU) & mask) == want) {
            return 1;
        }
    }
    return 0;
}

/** @brief Wait for the domain's gating control to go idle. */
static int npu_wait_idle(void)
{
    return npu_wait((uint8_t)RA8P1_PDCTRNPU_PDCSF, 0U);
}

/** @brief Link NPU_IRQ to its NVIC line, clear it pending and unmask it. */
static void npu_irq_arm(void)
{
    TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_NPU)) = RA8P1_ICU_EVENT_NPU_IRQ;
    (void)TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_NPU));
    TIKU_REG32(RA8P1_NVIC_ICPR(RA8P1_ICU_SLOT_NPU / 32U)) =
        (1UL << (RA8P1_ICU_SLOT_NPU % 32U));
    TIKU_REG32(RA8P1_NVIC_ISER(RA8P1_ICU_SLOT_NPU / 32U)) =
        (1UL << (RA8P1_ICU_SLOT_NPU % 32U));
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
}

/** @brief NPU interrupt on ICU slot RA8P1_ICU_SLOT_NPU; sets npu_done. */
void tiku_ra8p1_npu_handler(void)
{
    /* Acknowledge at the NPU first: an ICU latch cleared while the block
     * still asserts its output is raised again at once. */
    TIKU_REG32(RA8P1_NPU_CMD) = RA8P1_NPU_CMD_CLEAR_IRQ;
    (void)TIKU_REG32(RA8P1_NPU_CMD);

    TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_NPU)) &= ~RA8P1_ICU_IELSR_IR;
    (void)TIKU_REG32(RA8P1_ICU_IELSR(RA8P1_ICU_SLOT_NPU));
    TIKU_REG32(RA8P1_NVIC_ICPR(RA8P1_ICU_SLOT_NPU / 32U)) =
        (1UL << (RA8P1_ICU_SLOT_NPU % 32U));
    __asm__ volatile ("dsb" ::: "memory");

    tiku_ra8p1_npu_irq_count++;
    npu_done = 1u;
}

/**
 * @brief Soft-reset the block, then restore the configuration reset clears.
 *
 * A fault leaves the NPU refusing work until it is reset, so npu_run() calls
 * this before every submission.
 *
 * @return TIKU_RA8P1_NPU_OK, or TIKU_RA8P1_NPU_ERR_POWER when the reset does
 *         not finish or PROT does not read back as RA8P1_NPU_RESET_CPL
 */
static int npu_reset_and_configure(void)
{
    unsigned long spins;

    TIKU_REG32(RA8P1_NPU_RESET) = RA8P1_NPU_RESET_CPL;
    (void)TIKU_REG32(RA8P1_NPU_RESET);
    for (spins = NPU_POWER_SPINS; spins != 0UL; spins--) {
        if ((TIKU_REG32(RA8P1_NPU_STATUS) & RA8P1_NPU_STATUS_RESET) == 0UL) {
            break;
        }
    }
    if (spins == 0UL) {
        return TIKU_RA8P1_NPU_ERR_POWER;
    }
    if (TIKU_REG32(RA8P1_NPU_PROT) != RA8P1_NPU_RESET_CPL) {
        return TIKU_RA8P1_NPU_ERR_POWER;
    }

    /* The soft reset clears these.  At zero the block allows one outstanding
     * read and one write, and a stream faults on its first data access. */
    TIKU_REG32(RA8P1_NPU_AXI_LIMIT0) = RA8P1_NPU_AXI_LIMIT;
    TIKU_REG32(RA8P1_NPU_AXI_LIMIT1) = RA8P1_NPU_AXI_LIMIT;
    TIKU_REG32(RA8P1_NPU_AXI_LIMIT2) = RA8P1_NPU_AXI_LIMIT;
    TIKU_REG32(RA8P1_NPU_AXI_LIMIT3) = RA8P1_NPU_AXI_LIMIT;
    TIKU_REG32(RA8P1_NPU_QREGIONCFG) = RA8P1_NPU_REGIONCFG_DEFAULT;
    TIKU_REG32(RA8P1_NPU_QCONFIG)    = RA8P1_NPU_QCONFIG_DEFAULT;
    __asm__ volatile ("dsb" ::: "memory");
    return TIKU_RA8P1_NPU_OK;
}

static void npu_power_down(void)
{
    npu_ready = 0U;

    TIKU_REG32(RA8P1_NVIC_ICER(RA8P1_ICU_SLOT_NPU / 32U)) =
        (1UL << (RA8P1_ICU_SLOT_NPU % 32U));

    TIKU_REG32(RA8P1_MSTPCRA) |= RA8P1_MSTPA_NPU;
    (void)TIKU_REG32(RA8P1_MSTPCRA);
    tiku_cpu_ra8p1_delay_us(30U);

    if (npu_wait_idle()) {
        npu_protect(1);
        TIKU_REG8(RA8P1_PDCTRNPU) = (uint8_t)RA8P1_PDCTRNPU_PDDE;
        npu_protect(0);
    }
}

int tiku_ra8p1_npu_init(void)
{
    if (npu_ready != 0U) {
        return (TIKU_REG32(RA8P1_NPU_ID) == RA8P1_NPU_ID_EXPECT)
                   ? TIKU_RA8P1_NPU_OK : TIKU_RA8P1_NPU_ERR_ID;
    }

    /* UM 11.5.1 requires the MOCO running for power gating; this function
     * does not start it. */
    if ((TIKU_REG8(RA8P1_MOCOCR) & RA8P1_MOCOCR_MCSTP) != 0U) {
        return TIKU_RA8P1_NPU_ERR_MOCO;
    }

    if (!npu_wait_idle()) {
        return TIKU_RA8P1_NPU_ERR_POWER;
    }

    /* PDDE is inverted: 0 powers the domain on.  The byte is assigned:
     * PDCSF and PDPGSF are read-only status bits in it, and a write that
     * carries a set PDPGSF is refused, which leaves the domain gated. */
    npu_protect(1);
    TIKU_REG8(RA8P1_PDCTRNPU) = 0U;
    npu_protect(0);

    if (!npu_wait((uint8_t)(RA8P1_PDCTRNPU_PDCSF | RA8P1_PDCTRNPU_PDPGSF),
                  0U)) {
        npu_power_down();
        return TIKU_RA8P1_NPU_ERR_POWER;
    }

    /* The module stop is released after the domain is powered, by a
     * read-modify-write: MSTPCRA bits 21:17 read as 1 and must be written
     * as 1, or the whole write is refused. */
    TIKU_REG32(RA8P1_MSTPCRA) &= ~RA8P1_MSTPA_NPU;
    (void)TIKU_REG32(RA8P1_MSTPCRA);
    tiku_cpu_ra8p1_delay_us(30U);

    if (TIKU_REG32(RA8P1_NPU_ID) != RA8P1_NPU_ID_EXPECT) {
        npu_power_down();
        return TIKU_RA8P1_NPU_ERR_ID;
    }

    if (npu_reset_and_configure() != TIKU_RA8P1_NPU_OK) {
        npu_power_down();
        return TIKU_RA8P1_NPU_ERR_POWER;
    }

    npu_irq_arm();
    npu_ready = 1U;
    return TIKU_RA8P1_NPU_OK;
}

void tiku_ra8p1_npu_stop(void)
{
    if (npu_ready != 0U) {
        npu_power_down();
    }
}

int tiku_ra8p1_npu_ready(void)
{
    return (npu_ready != 0U);
}

uint32_t tiku_ra8p1_npu_id(void)
{
    return (npu_ready != 0U) ? TIKU_REG32(RA8P1_NPU_ID) : 0UL;
}

uint16_t tiku_ra8p1_npu_macs(void)
{
    uint32_t cfg;

    if (npu_ready == 0U) {
        return 0U;
    }
    /* The field holds log2 of the MAC count: 8 means 256. */
    cfg = (TIKU_REG32(RA8P1_NPU_CONFIG) >> RA8P1_NPU_CONFIG_MACS_SHIFT) &
          RA8P1_NPU_CONFIG_MACS_MASK;
    return (uint16_t)(1UL << cfg);
}

uint16_t tiku_ra8p1_npu_shram_kb(void)
{
    if (npu_ready == 0U) {
        return 0U;
    }
    return (uint16_t)((TIKU_REG32(RA8P1_NPU_CONFIG) >>
                       RA8P1_NPU_CONFIG_SHRAM_SHIFT) &
                      RA8P1_NPU_CONFIG_SHRAM_MASK);
}

/*---------------------------------------------------------------------------*/
/* MODELS, RUNS AND SELF-TESTS                                               */
/*---------------------------------------------------------------------------*/

/*
 * TIKU_NPU_EMBED_MODEL=1 compiles the max-pool command stream into the image.
 * With 0 there is no model until tiku_ra8p1_npu_load() loads one from the
 * store.
 */
#ifndef TIKU_NPU_EMBED_MODEL
#define TIKU_NPU_EMBED_MODEL 1
#endif
#if (TIKU_NPU_EMBED_MODEL + 0)
#include "tiku_npu_maxpool.h"
#endif
#include "tiku_cache_arch.h"
#include <kernel/fs/tiku_model.h>
#include <kernel/vfs/tree/tiku_vfs_tree_data.h>

/*
 * Static buffer ceilings.  A run uses the geometry of the model in force, and
 * tiku_ra8p1_npu_load() refuses a model that does not fit.  The arena default
 * admits a 512x512 max-pool, whose arena is its input plus its output.
 */
#ifndef TIKU_NPU_ARENA_MAX
#define TIKU_NPU_ARENA_MAX  393216u
#endif
#ifndef TIKU_NPU_CMS_MAX
#define TIKU_NPU_CMS_MAX    4096u
#endif
#ifndef TIKU_NPU_WTS_MAX
#define TIKU_NPU_WTS_MAX    65536u
#endif

/*
 * The NPU reads and writes memory through its own AXI master: the M85's
 * writes reach it only after their dirty lines are cleaned, and the NPU's
 * writes reach the M85 only after the stale lines are invalidated.  The
 * buffers are 32-byte aligned and whole lines long, so the invalidate after
 * a run touches no other data.
 */
static uint8_t npu_arena[TIKU_NPU_ARENA_MAX] __attribute__((aligned(32)));
static uint8_t npu_cms[TIKU_NPU_CMS_MAX] __attribute__((aligned(32)));
/* Weights and scales, which the stream reaches through region 0; the arena
 * is region 1. */
static uint8_t npu_wts[TIKU_NPU_WTS_MAX] __attribute__((aligned(32)));

/* Geometry in force: the built-in model's when it is embedded, until
 * tiku_ra8p1_npu_load() replaces it. */
#if (TIKU_NPU_EMBED_MODEL + 0)
static tiku_ra8p1_npu_model_t npu_model = {
    TIKU_NPU_MP_ARENA_BYTES, TIKU_NPU_MP_IFM_OFFSET, TIKU_NPU_MP_OFM_OFFSET,
    TIKU_NPU_MP_IFM_DIM, TIKU_NPU_MP_OFM_DIM, TIKU_NPU_MP_CMS_BYTES,
    0u, TIKU_RA8P1_NPU_KIND_MAXPOOL, 1u
};
#else
static tiku_ra8p1_npu_model_t npu_model;    /* nothing to run until loaded */
#endif
static uint8_t npu_model_from_store;
static uint32_t npu_run_count;

/** @brief The M85's reference output, kept off the stack; it must hold the
 *         largest output a model in force can produce. */
static int8_t npu_expect[TIKU_NPU_ARENA_MAX / 4u];

/*
 * Header written by tools/npu/velapack.py, little-endian: magic u32 @0,
 * version u16 @4, kind u8 @6, channels u8 @7, arena u32 @8, ifm_off u32 @12,
 * ofm_off u32 @16, ifm_dim u16 @20, ofm_dim u16 @22, NPU CONFIG u32 @24,
 * cms_len u32 @28, wts_len u32 @32.  The command stream starts at byte 40
 * and the weights follow it.
 */
#define NPU_ETH_MAGIC   0x504E4B54UL       /* "TKNP" little-endian */
#define NPU_ETH_HDR     40u
#define NPU_ETH_VER     2u

/** @brief Read a little-endian u32 byte by byte; a mapped file may be
 *         unaligned. */
static uint32_t npu_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** @brief Read a little-endian u16 byte by byte. */
static uint16_t npu_rd16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

int tiku_ra8p1_npu_load(const char *name)
{
    tiku_tfs_t   *fs = tiku_vfs_tree_data_store();
    tiku_model_t  m;
    const uint8_t *h;
    tiku_ra8p1_npu_model_t g;
    unsigned i;

    if (fs == 0 || name == 0) { return TIKU_RA8P1_NPU_ERR_IMAGE; }
    /* The CONFIG check below reads an NPU register, and every register of a
     * gated NPU reads 0, so the NPU is brought up first. */
    if (tiku_ra8p1_npu_init() != TIKU_RA8P1_NPU_OK) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;
    }
    if (tiku_model_open(fs, name, &m) != TIKU_MODEL_OK) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;
    }
    h = m.base;
    if (m.len < NPU_ETH_HDR || npu_rd32(h) != NPU_ETH_MAGIC) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;
    }

    if (npu_rd16(h + 4) != NPU_ETH_VER) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;
    }
    g.kind     = h[6];
    g.channels = h[7];
    g.arena    = npu_rd32(h + 8);
    g.ifm_off  = npu_rd32(h + 12);
    g.ofm_off  = npu_rd32(h + 16);
    g.ifm_dim  = npu_rd16(h + 20);
    g.ofm_dim  = npu_rd16(h + 22);
    g.cms_len  = npu_rd32(h + 28);
    g.wts_len  = npu_rd32(h + 32);

    /* The stream was compiled for one NPU CONFIG; a file built for another
     * is refused. */
    if (npu_rd32(h + 24) != TIKU_REG32(RA8P1_NPU_CONFIG)) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;
    }
    if (g.arena > sizeof npu_arena) {
        return TIKU_RA8P1_NPU_ERR_ARENA;
    }
    /* Runs, self-tests and the bench address the input and output at their
     * offsets in the arena, and the self-test writes the M85's reference
     * output to npu_expect and compares the output against it.  Both tensors
     * must lie inside the model's arena, and the reference output and the
     * output must fit npu_expect.  The identity reference copies the whole
     * input; the max-pool one reads two input rows and columns per output
     * one. */
    {
        uint64_t in_n  = (uint64_t)g.ifm_dim * g.ifm_dim * g.channels;
        uint64_t out_n = (uint64_t)g.ofm_dim * g.ofm_dim * g.channels;

        if (g.channels == 0U ||
            g.ifm_off > g.arena || in_n > g.arena - g.ifm_off ||
            g.ofm_off > g.arena || out_n > g.arena - g.ofm_off ||
            out_n > sizeof npu_expect) {
            return TIKU_RA8P1_NPU_ERR_IMAGE;
        }
        if (g.kind == TIKU_RA8P1_NPU_KIND_IDENTITY
                ? in_n > sizeof npu_expect
                : 2UL * g.ofm_dim > g.ifm_dim) {
            return TIKU_RA8P1_NPU_ERR_IMAGE;
        }
    }
    if (g.cms_len > sizeof npu_cms || g.wts_len > sizeof npu_wts ||
        m.len < NPU_ETH_HDR + g.cms_len + g.wts_len) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;
    }

    for (i = 0U; i < g.cms_len; i++) {
        npu_cms[i] = h[NPU_ETH_HDR + i];
    }
    /* The weights follow the stream and are copied too: the store does not
     * guarantee alignment. */
    for (i = 0U; i < g.wts_len; i++) {
        npu_wts[i] = h[NPU_ETH_HDR + g.cms_len + i];
    }
    npu_model = g;
    npu_model_from_store = 1U;
    return TIKU_RA8P1_NPU_OK;
}

const tiku_ra8p1_npu_model_t *tiku_ra8p1_npu_model(void)
{
    return &npu_model;
}

int tiku_ra8p1_npu_from_store(void)
{
    return (npu_model_from_store != 0U);
}

/** @brief Compute the expected output of the model in force on the M85. */
static void npu_reference(const int8_t *ifm, int8_t *ofm)
{
    unsigned in = npu_model.ifm_dim, out = npu_model.ofm_dim;
    unsigned r, c;

    if (npu_model.kind == TIKU_RA8P1_NPU_KIND_IDENTITY) {
        /* The identity model is a 3x3 kernel whose only non-zero tap is the
         * centre, one channel to itself, at unit scale: the accelerator does
         * the full MAC work and the output equals the input. */
        unsigned n = in * in * npu_model.channels;

        for (r = 0U; r < n; r++) {
            ofm[r] = ifm[r];
        }
        return;
    }

    for (r = 0U; r < out; r++) {
        for (c = 0U; c < out; c++) {
            const int8_t *p = &ifm[(2U * r) * in + (2U * c)];
            const int8_t *q = &p[in];
            int8_t m = p[0];

            if (p[1] > m) { m = p[1]; }
            if (q[0] > m) { m = q[0]; }
            if (q[1] > m) { m = q[1]; }
            ofm[r * out + c] = m;
        }
    }
}

/**
 * @brief Submit the staged command stream and wait for it to finish.
 *
 * @param status_out  Out: STATUS in bits 15:0 and QREAD in bits 31:16, or
 *                    NULL
 * @return TIKU_RA8P1_NPU_OK, ERR_TIMEOUT, or ERR_FAULT
 */
static int npu_run(uint32_t *status_out)
{
    unsigned long spins;
    uint32_t sta = 0UL;

    if (npu_reset_and_configure() != TIKU_RA8P1_NPU_OK) {
        return TIKU_RA8P1_NPU_ERR_FAULT;
    }

    /* Region 0 holds the weights and scales, region 1 the tensors, and the
     * other six bases point at the arena.  A base left at zero points its
     * region at unmapped memory, and a stream that touches it, even with an
     * empty weight fetch, faults. */
    {
        unsigned r;

        for (r = 0U; r < 8U; r++) {
            TIKU_REG32(RA8P1_NPU_BASEP(r))     = (uint32_t)npu_arena;
            TIKU_REG32(RA8P1_NPU_BASEP(r) + 4) = 0UL;
        }
        TIKU_REG32(RA8P1_NPU_BASEP(0)) = (uint32_t)npu_wts;
    }

    /* QBASE is an absolute address.  QCONFIG selects the AXI limit set the
     * queue's own fetches use. */
    TIKU_REG32(RA8P1_NPU_QBASE)    = (uint32_t)npu_cms;
    TIKU_REG32(RA8P1_NPU_QBASE_HI) = 0UL;
    TIKU_REG32(RA8P1_NPU_QSIZE)    = npu_model.cms_len;
    __asm__ volatile ("dsb" ::: "memory");

    /* CLK_Q_EN and PWR_Q_EN are written back as read: they are the block's
     * Q-channel state, and a submission only adds the run request. */
    npu_done = 0u;
    TIKU_REG32(RA8P1_NPU_CMD) =
        (TIKU_REG32(RA8P1_NPU_CMD) & (RA8P1_NPU_CMD_CLK_Q_EN |
                                      RA8P1_NPU_CMD_PWR_Q_EN)) |
        RA8P1_NPU_CMD_RUN;

    /*
     * The core sleeps in WFI until the completion interrupt sets npu_done.
     * Any other interrupt also ends a WFI, so the loop re-checks the flag and
     * a deadline of 1/20 s, counted in DWT cycles at the current clock.
     */
    {
        uint32_t t0 = npu_cycles();
        uint32_t budget = (uint32_t)(tiku_cpu_ra8p1_clock_get_hz() / 20UL);

        while (npu_done == 0u) {
            uint32_t irq;
            if ((npu_cycles() - t0) > budget) {
                break;
            }
            __asm__ volatile ("mrs %0, primask" : "=r" (irq));
            __asm__ volatile ("cpsid i" ::: "memory");
            if (npu_done == 0u) {
                __asm__ volatile ("dsb" ::: "memory");
                __asm__ volatile ("wfi");
            }
            __asm__ volatile ("msr primask, %0" :: "r" (irq) : "memory");
        }
    }
    sta = TIKU_REG32(RA8P1_NPU_STATUS);
    spins = (npu_done != 0u) ? 1UL : 0UL;

    if (status_out != 0) {
        /* STATUS in the low half, QREAD in the high half: the bytes of
         * stream consumed show how far the run got. */
        *status_out = (sta & 0xFFFFUL) |
                      (TIKU_REG32(RA8P1_NPU_QREAD) << 16);
    }
    if (spins == 0UL) {
        return TIKU_RA8P1_NPU_ERR_TIMEOUT;
    }
    if ((sta & (RA8P1_NPU_STATUS_PARSE | RA8P1_NPU_STATUS_BUSERR)) != 0UL) {
        return TIKU_RA8P1_NPU_ERR_FAULT;
    }
    /* The block also stops on faults that set neither PARSE nor BUSERR; only
     * END marks a stream that ran to its end. */
    if ((sta & RA8P1_NPU_STATUS_END) == 0UL) {
        return TIKU_RA8P1_NPU_ERR_FAULT;
    }
    return TIKU_RA8P1_NPU_OK;
}

/**
 * @brief Stage the stream and a seeded input, run, and compare with the M85.
 *
 * @param seed        Varies the input pattern
 * @param status_out  Out: as for npu_run(), or NULL
 * @param tamper      Non-zero inverts one stream byte for this run
 * @param maint       Zero skips the arena's cache maintenance
 * @return TIKU_RA8P1_NPU_OK, or the first error met
 */
static int npu_selftest_run(uint32_t seed, uint32_t *status_out,
                            int tamper, int maint)
{
    int8_t  *ifm = (int8_t *)&npu_arena[npu_model.ifm_off];
    int8_t  *ofm = (int8_t *)&npu_arena[npu_model.ofm_off];
    unsigned ifm_n = (unsigned)npu_model.ifm_dim * npu_model.ifm_dim *
                     npu_model.channels;
    unsigned ofm_n = (unsigned)npu_model.ofm_dim * npu_model.ofm_dim *
                     npu_model.channels;
    unsigned i;
    int      rc;

    rc = tiku_ra8p1_npu_init();
    if (rc != TIKU_RA8P1_NPU_OK) {
        return rc;
    }
#if (TIKU_NPU_EMBED_MODEL + 0)
    /* The built-in stream was compiled for CONFIG TIKU_NPU_MP_CFG_EXPECT.
     * A store model's CONFIG is checked against the silicon at load. */
    if (!npu_model_from_store &&
        TIKU_REG32(RA8P1_NPU_CONFIG) != TIKU_NPU_MP_CFG_EXPECT) {
        return TIKU_RA8P1_NPU_ERR_ID;
    }
#endif

#if (TIKU_NPU_EMBED_MODEL + 0)
    if (!npu_model_from_store) {
        for (i = 0U; i < npu_model.cms_len; i++) {
            npu_cms[i] = tiku_npu_mp_cms[i];
        }
    }
#endif
    if (npu_model.cms_len == 0U) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;
    }
    /* The byte is inverted for this run only.  A store stream is copied in
     * once at load, so a corruption left in place would fault every later
     * run. */
    if (tamper) {
        npu_cms[npu_model.cms_len / 2U] ^= 0xFFU;
    }

    for (i = 0U; i < npu_model.arena; i++) {
        npu_arena[i] = 0U;
    }
    for (i = 0U; i < ifm_n; i++) {
        /* Spread so every 2x2 window has a distinct maximum. */
        ifm[i] = (int8_t)(((seed + (i * 37U)) % 251U) - 125U);
    }
    npu_reference(ifm, npu_expect);

    /* The stream and the weights are always cleaned; a stream left dirty
     * makes the NPU fault.  maint covers the arena only: without its
     * clean, the input the M85 wrote stays dirty in the D-cache. */
    tiku_ra8p1_dcache_clean(npu_cms, npu_model.cms_len);
    if (npu_model.wts_len != 0U) {
        tiku_ra8p1_dcache_clean(npu_wts, npu_model.wts_len);
    }
    if (maint) {
        tiku_ra8p1_dcache_clean(npu_arena, npu_model.arena);
    }
    __asm__ volatile ("dsb" ::: "memory");

    rc = npu_run(status_out);
    if (tamper) {
        npu_cms[npu_model.cms_len / 2U] ^= 0xFFU;   /* put it back */
    }
    if (rc != TIKU_RA8P1_NPU_OK) {
        return rc;
    }

    /* The output the NPU wrote reaches the M85 only after the stale lines
     * covering it are invalidated. */
    if (maint) {
        tiku_ra8p1_dcache_invalidate(npu_arena, npu_model.arena);
    }
    __asm__ volatile ("dsb" ::: "memory");

    for (i = 0U; i < ofm_n; i++) {
        if (ofm[i] != npu_expect[i]) {
            return TIKU_RA8P1_NPU_ERR_MISMATCH;
        }
    }
    return TIKU_RA8P1_NPU_OK;
}

void *tiku_ra8p1_npu_ifm(void)
{
    return (npu_model.cms_len != 0U) ? &npu_arena[npu_model.ifm_off]
                                     : (void *)0;
}

const void *tiku_ra8p1_npu_ofm(void)
{
    return (npu_model.cms_len != 0U) ? &npu_arena[npu_model.ofm_off]
                                     : (const void *)0;
}

uint32_t tiku_ra8p1_npu_runs(void)
{
    return npu_run_count;
}

int tiku_ra8p1_npu_run(uint32_t *status_out)
{
    int rc = tiku_ra8p1_npu_init();

    if (rc != TIKU_RA8P1_NPU_OK) {
        return rc;
    }
    if (npu_model.cms_len == 0U) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;
    }
#if (TIKU_NPU_EMBED_MODEL + 0)
    if (!npu_model_from_store) {
        unsigned i;

        for (i = 0U; i < npu_model.cms_len; i++) {
            npu_cms[i] = tiku_npu_mp_cms[i];
        }
    }
#endif
    tiku_ra8p1_dcache_clean(npu_cms, npu_model.cms_len);
    if (npu_model.wts_len != 0U) {
        tiku_ra8p1_dcache_clean(npu_wts, npu_model.wts_len);
    }
    tiku_ra8p1_dcache_clean(npu_arena, npu_model.arena);
    __asm__ volatile ("dsb" ::: "memory");

    rc = npu_run(status_out);

    tiku_ra8p1_dcache_invalidate(npu_arena, npu_model.arena);
    __asm__ volatile ("dsb" ::: "memory");
    if (rc == TIKU_RA8P1_NPU_OK) {
        npu_run_count++;
    }
    return rc;
}

int tiku_ra8p1_npu_bench(uint32_t rounds, uint32_t *npu_us, uint32_t *cpu_us)
{
    unsigned long mhz = tiku_cpu_ra8p1_clock_get_hz() / 1000000UL;
    int8_t  *ifm;
    uint32_t t0, npu_c, cpu_c;
    uint32_t i;
    int      rc;

    if (rounds == 0U || mhz == 0UL) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;
    }
    rc = tiku_ra8p1_npu_init();
    if (rc != TIKU_RA8P1_NPU_OK) {
        return rc;
    }
    if (npu_model.cms_len == 0U) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;
    }
    ifm = (int8_t *)&npu_arena[npu_model.ifm_off];
    {
        uint32_t n = (uint32_t)npu_model.ifm_dim * npu_model.ifm_dim *
                     npu_model.channels;

    for (i = 0U; i < n; i++) {
        ifm[i] = (int8_t)((i * 37U) % 251U) - 125;
    }
    }

    t0 = npu_cycles();
    for (i = 0U; i < rounds; i++) {
        rc = tiku_ra8p1_npu_run((uint32_t *)0);
        if (rc != TIKU_RA8P1_NPU_OK) {
            return rc;
        }
    }
    npu_c = npu_cycles() - t0;

    t0 = npu_cycles();
    for (i = 0U; i < rounds; i++) {
        npu_reference(ifm, npu_expect);
    }
    cpu_c = npu_cycles() - t0;

    /* Reported in microseconds: the NPU runs on its own clock, so a core
     * cycle count is not an NPU cycle count. */
    if (npu_us != 0) { *npu_us = (uint32_t)(npu_c / rounds / mhz); }
    if (cpu_us != 0) { *cpu_us = (uint32_t)(cpu_c / rounds / mhz); }
    return TIKU_RA8P1_NPU_OK;
}

int tiku_ra8p1_npu_selftest(uint32_t seed, uint32_t *status_out)
{
    return npu_selftest_run(seed, status_out, 0, 1);
}

int tiku_ra8p1_npu_selftest_tampered(uint32_t seed)
{
    return npu_selftest_run(seed, (uint32_t *)0, 1, 1);
}

int tiku_ra8p1_npu_selftest_nomaint(uint32_t seed)
{
    return npu_selftest_run(seed, (uint32_t *)0, 0, 0);
}

int tiku_ra8p1_npu_selftest_badwts(uint32_t seed)
{
    int rc;

    if (npu_model.wts_len == 0U) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;   /* nothing to corrupt */
    }
    /* One weight byte is inverted for this run.  Weights are data, not a
     * parsed stream, so the NPU raises no fault; only the output changes. */
    npu_wts[npu_model.wts_len / 2U] ^= 0xFFU;
    rc = npu_selftest_run(seed, (uint32_t *)0, 0, 1);
    npu_wts[npu_model.wts_len / 2U] ^= 0xFFU;
    return rc;
}

int tiku_ra8p1_npu_selftest_noirq(uint32_t seed)
{
    int rc;

    /* The completion line is masked for the run and unmasked after.  The NPU
     * still finishes, but npu_done stays 0 and the run ends at its deadline
     * when it waits on the interrupt. */
    TIKU_REG32(RA8P1_NVIC_ICER(RA8P1_ICU_SLOT_NPU / 32U)) =
        (1UL << (RA8P1_ICU_SLOT_NPU % 32U));
    __asm__ volatile ("dsb\n\tisb" ::: "memory");

    rc = npu_selftest_run(seed, (uint32_t *)0, 0, 1);

    TIKU_REG32(RA8P1_NVIC_ISER(RA8P1_ICU_SLOT_NPU / 32U)) =
        (1UL << (RA8P1_ICU_SLOT_NPU % 32U));
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
    return rc;
}
