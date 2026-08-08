/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_npu_arch.c - RA8P1 Ethos-U55 bring-up.
 *
 * The NPU sits behind a power domain and a module stop, both closed out of
 * reset, and the manual's release order between them is not interchangeable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_npu_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_common.h"
#include "tiku_cpu_freq_boot_arch.h"

/** @brief Bounded spins for a power-gating transition; the domain settles in
 *         microseconds, so this only has to stop a wedge from hanging boot. */
#define NPU_POWER_SPINS     100000UL

/* The cycle counter this part already uses for storage timings. */
#define NPU_DEMCR       0xE000EDFCUL
#define NPU_DEMCR_TRCENA (1UL << 24)
#define NPU_DWT_CTRL    0xE0001000UL
#define NPU_DWT_CYCCNT  0xE0001004UL

/**
 * @brief Cycles since the counter was enabled; wraps every 2^32.
 *
 * @note DEMCR.TRCENA gates the whole trace block, so enabling the counter
 *       alone leaves it reading zero on a board with no debugger attached.
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

/** @brief Set once the ID has been read back from a released block. */
static uint8_t npu_ready;

/**
 * @brief Unlock or relock the registers PRCR guards.
 *
 * @param unlock  Non-zero to allow writes, zero to protect again
 */
static void npu_protect(int unlock)
{
    TIKU_REG16(RA8P1_PRCR_S) = (uint16_t)(RA8P1_PRCR_KEY |
                                          (unlock ? RA8P1_PRCR_PRC1 : 0U));
}

/**
 * @brief Spin until the gating status bits settle on @p want.
 *
 * @note Polled rather than sampled once: the controller does not raise PDCSF
 *       in the same cycle as the PDDE write, so a single read straight after
 *       it sees the previous state and reports a failure that did not
 *       happen.
 *
 * @param mask  Bits to compare
 * @param want  Value those bits must reach
 * @return Non-zero when they did, inside the budget
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

/** @brief Link NPU_IRQ to its NVIC line and unmask it. */
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

void tiku_ra8p1_npu_handler(void)
{
    /* Acknowledge at the NPU first: clearing the ICU latch while the block
     * still asserts its output re-raises the line immediately. */
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
 * @note A fault leaves the NPU needing a reset before it will accept work, so
 *       this runs before every submission and not only at bring-up.
 * @return TIKU_RA8P1_NPU_OK when the reset completed and PROT agrees
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

    /* The reset clears these, so they belong here rather than at bring-up.
     * Their reset value encodes one outstanding read and one write. */
    TIKU_REG32(RA8P1_NPU_AXI_LIMIT0) = RA8P1_NPU_AXI_LIMIT;
    TIKU_REG32(RA8P1_NPU_AXI_LIMIT1) = RA8P1_NPU_AXI_LIMIT;
    TIKU_REG32(RA8P1_NPU_AXI_LIMIT2) = RA8P1_NPU_AXI_LIMIT;
    TIKU_REG32(RA8P1_NPU_AXI_LIMIT3) = RA8P1_NPU_AXI_LIMIT;
    TIKU_REG32(RA8P1_NPU_QREGIONCFG) = RA8P1_NPU_REGIONCFG_DEFAULT;
    TIKU_REG32(RA8P1_NPU_QCONFIG)    = RA8P1_NPU_QCONFIG_DEFAULT;
    __asm__ volatile ("dsb" ::: "memory");
    return TIKU_RA8P1_NPU_OK;
}

int tiku_ra8p1_npu_init(void)
{
    if (npu_ready != 0U) {
        return (TIKU_REG32(RA8P1_NPU_ID) == RA8P1_NPU_ID_EXPECT)
                   ? TIKU_RA8P1_NPU_OK : TIKU_RA8P1_NPU_ERR_ID;
    }

    /* UM 11.5.1 names the MOCO as a precondition of power gating, not of the
     * NPU, and nothing here starts it. */
    if ((TIKU_REG8(RA8P1_MOCOCR) & RA8P1_MOCOCR_MCSTP) != 0U) {
        return TIKU_RA8P1_NPU_ERR_MOCO;
    }

    if (!npu_wait_idle()) {
        return TIKU_RA8P1_NPU_ERR_POWER;
    }

    /* PDDE reads backwards: clearing it powers the domain ON.  Assigned, not
     * read-modify-written -- PDCSF and PDPGSF are read-only status in the same
     * byte, and feeding a set PDPGSF back in has the write refused, which
     * presents as the domain simply never leaving gating. */
    npu_protect(1);
    TIKU_REG8(RA8P1_PDCTRNPU) = 0U;
    npu_protect(0);

    if (!npu_wait((uint8_t)(RA8P1_PDCTRNPU_PDCSF | RA8P1_PDCTRNPU_PDPGSF),
                  0U)) {
        return TIKU_RA8P1_NPU_ERR_POWER;
    }

    /* Only now the module stop, and only as a read-modify-write: MSTPCRA bits
     * 21:17 read as one and must be written back as one, so a computed mask
     * that clears any of them has the whole write refused. */
    TIKU_REG32(RA8P1_MSTPCRA) &= ~RA8P1_MSTPA_NPU;
    (void)TIKU_REG32(RA8P1_MSTPCRA);
    tiku_cpu_ra8p1_delay_us(30U);

    if (TIKU_REG32(RA8P1_NPU_ID) != RA8P1_NPU_ID_EXPECT) {
        return TIKU_RA8P1_NPU_ERR_ID;
    }

    if (npu_reset_and_configure() != TIKU_RA8P1_NPU_OK) {
        return TIKU_RA8P1_NPU_ERR_POWER;
    }

    npu_irq_arm();
    npu_ready = 1U;
    return TIKU_RA8P1_NPU_OK;
}

void tiku_ra8p1_npu_stop(void)
{
    if (npu_ready == 0U) {
        return;
    }
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
    /* The field is a log2, so 8 means 256 rather than 8. */
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
/* Self-test: the loaded model, checked against this core                    */
/*---------------------------------------------------------------------------*/

/*
 * With the model embedded the image carries the command stream; with it off
 * the model must come from the store, and the image stops growing with the
 * network.
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
 * Buffer ceilings, not model sizes.  The geometry a run uses comes from the
 * loaded model, so these only have to admit the largest one this build accepts
 * A store file therefore cannot size SRAM at run time.
 */
/*
 * Sized to admit a 512x512 max-pool and a yolo-class network; the arena a
 * packed model asks for is its input plus its output.  It costs .bss on a
 * part with SRAM to spare.
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
 * The NPU fetches through its own AXI master, so anything the M85 wrote is
 * only visible once its dirty lines are cleaned out, and anything the NPU
 * wrote is only visible once the stale lines are dropped.  Both buffers are
 * line-aligned, and the arenas are a whole number of lines, so the invalidate
 * after a run drops only what the accelerator wrote.
 */
static uint8_t npu_arena[TIKU_NPU_ARENA_MAX] __attribute__((aligned(32)));
static uint8_t npu_cms[TIKU_NPU_CMS_MAX] __attribute__((aligned(32)));
/* The read-only blob: weights and scales, which the stream reaches
 * through region 0 rather than the arena's region 1. */
static uint8_t npu_wts[TIKU_NPU_WTS_MAX] __attribute__((aligned(32)));

/* Geometry in force.  Compiled-in by default; tiku_ra8p1_npu_load() replaces
 * it wholesale with whatever the store hands over. */
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

/** @brief The M85's answer, kept out of the stack: 128x128 at the largest
 *         geometry this build admits. */
static int8_t npu_expect[TIKU_NPU_ARENA_MAX / 4u];

/*
 * The packed header tools/npu/velapack.py writes: magic, version, the arena
 * geometry, the NPU config the stream was built for, and the stream length.
 */
#define NPU_ETH_MAGIC   0x504E4B54UL       /* "TKNP" little-endian */
#define NPU_ETH_HDR     40u
#define NPU_ETH_VER     2u

/** @brief Little-endian fetch; a mapped file carries no alignment promise. */
static uint32_t npu_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

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
    /* The config check below reads a register, and a gated NPU answers 0 for
     * every one of them -- so a load before bring-up would reject every file
     * it was handed rather than the wrong ones. */
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

    /* The stream was compiled for a particular NPU; refuse one this part
     * cannot execute rather than discover it as a parse error. */
    if (npu_rd32(h + 24) != TIKU_REG32(RA8P1_NPU_CONFIG)) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;
    }
    if (g.arena > sizeof npu_arena) {
        return TIKU_RA8P1_NPU_ERR_ARENA;
    }
    if (g.cms_len > sizeof npu_cms || g.wts_len > sizeof npu_wts ||
        m.len < NPU_ETH_HDR + g.cms_len + g.wts_len) {
        return TIKU_RA8P1_NPU_ERR_IMAGE;
    }

    for (i = 0U; i < g.cms_len; i++) {
        npu_cms[i] = h[NPU_ETH_HDR + i];
    }
    /* The weights follow the stream, and get their own aligned buffer for the
     * same reason it does: the store promises no alignment. */
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

/** @brief Whatever the loaded model describes, computed on this core. */
static void npu_reference(const int8_t *ifm, int8_t *ofm)
{
    unsigned in = npu_model.ifm_dim, out = npu_model.ofm_dim;
    unsigned r, c;

    if (npu_model.kind == TIKU_RA8P1_NPU_KIND_IDENTITY) {
        /* A 3x3 kernel whose only non-zero tap is the centre, one channel to
         * itself, at unit scale: the accelerator does the full MAC work and
         * the answer is the input unchanged. */
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
 * @param status_out  Out: the status word read at completion, or NULL
 * @return OK, ERR_TIMEOUT, or ERR_FAULT
 */
static int npu_run(uint32_t *status_out)
{
    unsigned long spins;
    uint32_t sta = 0UL;

    if (npu_reset_and_configure() != TIKU_RA8P1_NPU_OK) {
        return TIKU_RA8P1_NPU_ERR_FAULT;
    }

    /* Every region base, not just the ones this stream names.  A base left at
     * zero points its region at unmapped memory, and a stream that touches it
     * even once -- an empty weight fetch is enough -- faults there rather than
     * where the mistake is. */
    {
        unsigned r;

        /* Region 0 is where the stream looks for weights and scales, region 1
         * for the tensors.  The rest are given the arena so a stray reference
         * lands somewhere mapped rather than at address zero. */
        for (r = 0U; r < 8U; r++) {
            TIKU_REG32(RA8P1_NPU_BASEP(r))     = (uint32_t)npu_arena;
            TIKU_REG32(RA8P1_NPU_BASEP(r) + 4) = 0UL;
        }
        TIKU_REG32(RA8P1_NPU_BASEP(0)) = (uint32_t)npu_wts;
    }

    /* QBASE is an absolute address; QCONFIG selects which limit set the
     * queue's own fetches use, not a base to offset from. */
    TIKU_REG32(RA8P1_NPU_QBASE)    = (uint32_t)npu_cms;
    TIKU_REG32(RA8P1_NPU_QBASE_HI) = 0UL;
    TIKU_REG32(RA8P1_NPU_QSIZE)    = npu_model.cms_len;
    __asm__ volatile ("dsb" ::: "memory");

    /* Carry the clock and power bits over rather than writing a fresh word:
     * they are the block's own Q-channel state, not this driver's to change,
     * and a submission is only meant to add the run request. */
    npu_done = 0u;
    TIKU_REG32(RA8P1_NPU_CMD) =
        (TIKU_REG32(RA8P1_NPU_CMD) & (RA8P1_NPU_CMD_CLK_Q_EN |
                                      RA8P1_NPU_CMD_PWR_Q_EN)) |
        RA8P1_NPU_CMD_RUN;

    /*
     * Wait in WFI rather than on the status register.  An inference is
     * hundreds of microseconds, which is a long time to hold a core at its
     * rung doing nothing but re-reading a register; the completion interrupt
     * is already wired, so the core can be asleep for all of it.  Other
     * interrupts wake the sleep too, hence the loop rather than one WFI.
     *
     * The deadline is a cycle count because the kernel tick is 128 Hz -- far
     * too coarse to bound something this short.
     */
    {
        uint32_t t0 = npu_cycles();
        uint32_t budget = (uint32_t)(tiku_cpu_ra8p1_clock_get_hz() / 20UL);

        while (npu_done == 0u) {
            if ((npu_cycles() - t0) > budget) {
                break;
            }
            __asm__ volatile ("wfi");
        }
    }
    sta = TIKU_REG32(RA8P1_NPU_STATUS);
    spins = (npu_done != 0u) ? 1UL : 0UL;

    if (status_out != 0) {
        /* Status in the low half, bytes of stream consumed in the high half:
         * how far it got is what separates "never fetched" from "faulted
         * partway through". */
        *status_out = (sta & 0xFFFFUL) |
                      (TIKU_REG32(RA8P1_NPU_QREAD) << 16);
    }
    if (spins == 0UL) {
        return TIKU_RA8P1_NPU_ERR_TIMEOUT;
    }
    if ((sta & (RA8P1_NPU_STATUS_PARSE | RA8P1_NPU_STATUS_BUSERR)) != 0UL) {
        return TIKU_RA8P1_NPU_ERR_FAULT;
    }
    /* Stopping is not finishing: the block also stops on a fault it did not
     * flag here, so the end-of-stream bit is what says the work was done. */
    if ((sta & RA8P1_NPU_STATUS_END) == 0UL) {
        return TIKU_RA8P1_NPU_ERR_FAULT;
    }
    return TIKU_RA8P1_NPU_OK;
}

/** @brief Stage the stream and a seeded input, run, and compare. */
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
    /* The built-in stream was compiled against a specific NPU configuration
     * and the silicon states its own; a store model carries the same field and
     * is checked as it loads. */
    if (TIKU_REG32(RA8P1_NPU_CONFIG) != TIKU_NPU_MP_CFG_EXPECT) {
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
    /* Corrupt one byte for the duration of this run only.  A store-resident
     * stream is copied in once at load, so a tamper left in place would still
     * be there for every later run and turn honest checks into parse faults. */
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

    /* The stream is always cleaned: leaving it dirty makes the NPU fault on
     * garbage, which would mask what this is trying to show.  Only the TENSOR
     * maintenance is under test -- without it the input the M85 just wrote is
     * still sitting dirty in its cache. */
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

    /* And the output the NPU just wrote is invisible until the stale lines
     * covering it are dropped. */
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
        uint32_t n = (uint32_t)npu_model.ifm_dim * npu_model.ifm_dim;

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

    /* Microseconds, not cycles.  The two sides do not share a clock -- the
     * accelerator has its own -- so a cycle count taken on the core compares
     * nothing once the rung moves. */
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
    /* Flip a weight byte for one run.  The accelerator cannot detect this --
     * weights are data, not a parsed stream -- so a wrong ANSWER is the only
     * evidence that region 0 is being read at all. */
    npu_wts[npu_model.wts_len / 2U] ^= 0xFFU;
    rc = npu_selftest_run(seed, (uint32_t *)0, 0, 1);
    npu_wts[npu_model.wts_len / 2U] ^= 0xFFU;
    return rc;
}

int tiku_ra8p1_npu_selftest_noirq(uint32_t seed)
{
    int rc;

    /* Mask the line the completion arrives on, run, restore.  The NPU still
     * finishes its work; what is withdrawn is the driver's only way to learn
     * that it did. */
    TIKU_REG32(RA8P1_NVIC_ICER(RA8P1_ICU_SLOT_NPU / 32U)) =
        (1UL << (RA8P1_ICU_SLOT_NPU % 32U));
    __asm__ volatile ("dsb\n\tisb" ::: "memory");

    rc = npu_selftest_run(seed, (uint32_t *)0, 0, 1);

    TIKU_REG32(RA8P1_NVIC_ISER(RA8P1_ICU_SLOT_NPU / 32U)) =
        (1UL << (RA8P1_ICU_SLOT_NPU % 32U));
    __asm__ volatile ("dsb\n\tisb" ::: "memory");
    return rc;
}
