/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_axonsprobe.c - Axon NPU (nRF54LM20B) bring-up probe.
 *
 * The nRF54LM20B carries the Axon NPU as peripheral NRF_AXONS @ 0x50056000
 * (IRQn 86, MCU power domain).  The PUBLIC register model is only a power
 * wrapper -- ENABLE.EN @ +0x400 and STATUS.READY @ +0x404 ("AXONS is
 * accessible") -- with the first 1 KB reserved and no nrfx HAL, no SVD block
 * and no documentation for the engine itself (the programming model lives in
 * Nordic's Neuton toolchain).  This probe is the on-die recon tool for that
 * unknown, the same role cryptoprobe played for CRACEN:
 *
 *   axonsprobe            ENABLE/STATUS + FICR identity
 *   axonsprobe en         enable, spin READY (bounded), report time-to-ready
 *   axonsprobe off        disable
 *   axonsprobe dump [o n] hex-dump n words of the 4 KB slot from offset o
 *                         (prints each address BEFORE reading: a bus fault
 *                         parks the core and the last line marks the edge)
 *   axonsprobe diff       snapshot the reserved window, enable, wait, then
 *                         print every word that CHANGED (finds live registers
 *                         without a single write)
 *   axonsprobe irq        NVIC-enable IRQ 86 + count what fires on enable
 *
 * READ-ONLY by design: no blind writes into an undocumented engine.  Findings
 * gate the Axon support plan's next phases.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_axonsprobe.h"

#if TIKU_SHELL_CMD_AXONSPROBE

#include <kernel/shell/tiku_shell_io.h>
#include <arch/nordic/tiku_device_select.h>
#include <arch/nordic/tiku_cpu_common.h>
#include <string.h>
#include <stdlib.h>

#if defined(TIKU_DEVICE_HAS_AXONS) && TIKU_DEVICE_HAS_AXONS

#if defined(TIKU_AXON_ENABLE) && TIKU_AXON_ENABLE
/* First-light path: Nordic's Axon driver core linked from the gitignored
 * temp/axon-models checkout, on the TikuOS platform layer
 * (arch/nordic/tiku_axon_platform.c). */
#include "drivers/axon/nrf_axon_driver.h"
#include "axon/nrf_axon_platform.h"
#include "drivers/axon/nrf_axon_dsp_intrinsics.h"
#if defined(TIKU_AXON_MODEL_TEST) && TIKU_AXON_MODEL_TEST
#include "drivers/axon/nrf_axon_nn_infer.h"
#include "drivers/axon/nrf_axon_nn_infer_test.h"
#include <kernel/fs/tiku_model.h>
#include <kernel/vfs/tree/tiku_vfs_tree_data.h>
#endif
#endif

/* Base + the two documented registers (offsets from the MDK struct). */
#define AXONS_BASE        0x50056000UL
#define AXONS_REG(off)    (*(volatile uint32_t *)(AXONS_BASE + (off)))
#define AXONS_ENABLE      AXONS_REG(0x400u)
#define AXONS_STATUS      AXONS_REG(0x404u)

/** Reserved engine window: 256 words (0x000..0x3FF). */
#define AXONS_WIN_WORDS   256u

/** Bounded READY spin (each iteration ~a few cycles at 128 MHz). */
#define AXONS_READY_SPIN  2000000ul

/** IRQ-storm brake for the probe ISR. */
#define AXONS_IRQ_LIMIT   16u

static volatile uint32_t axons_irq_count;

#if !defined(TIKU_AXON_ENABLE) || !TIKU_AXON_ENABLE
/** @brief Raw-probe ISR for IRQn 86 -- count, then self-disable on a storm.
 *  (With TIKU_AXON_ENABLE the platform layer owns the ISR and forwards to
 *  the vendor driver instead.) */
void tiku_nordic_axons_isr(void)
{
    axons_irq_count++;
    if (axons_irq_count >= AXONS_IRQ_LIMIT) {
        tiku_nordic_nvic_disable(86);
    }
}
#endif

/** @brief Enable the block and spin for READY; returns spins used or 0. */
static uint32_t axons_enable_wait(void)
{
    uint32_t spins;

    AXONS_ENABLE = 1u;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    for (spins = 1u; spins <= AXONS_READY_SPIN; spins++) {
        if ((AXONS_STATUS & 1u) != 0u) {
            return spins;
        }
    }
    return 0u;
}

static void axons_info(void)
{
    SHELL_PRINTF("AXONS @ 0x%x (nRF54LM20B Axon NPU wrapper)\n",
                 (unsigned)AXONS_BASE);
    SHELL_PRINTF("  ENABLE = 0x%x\n", (unsigned)AXONS_ENABLE);
    SHELL_PRINTF("  STATUS = 0x%x %s\n", (unsigned)AXONS_STATUS,
                 (AXONS_STATUS & 1u) ? "(READY)" : "(not ready)");
    SHELL_PRINTF("  FICR part=0x%x rev=0x%x\n",
                 (unsigned)*(volatile uint32_t *)0x00FFC340ul,
                 (unsigned)*(volatile uint32_t *)0x00FFC344ul);
}

static void axons_dump(uint32_t off, uint32_t words)
{
    uint32_t i;

    if (words == 0u || words > 1024u) {
        words = 16u;
    }
    off &= ~3ul;
    for (i = 0u; i < words; i++) {
        uint32_t o = off + 4u * i;
        if ((i & 3u) == 0u) {
            /* Address printed BEFORE the reads: a faulting window edge is
             * identified by the last line that appears. */
            SHELL_PRINTF("\n  +%x:", (unsigned)o);
        }
        SHELL_PRINTF(" %x", (unsigned)AXONS_REG(o));
    }
    SHELL_PRINTF("\n");
}

static void axons_diff(void)
{
    static uint32_t before[AXONS_WIN_WORDS];  /* 1 KB: static, not stack */
    uint32_t i;
    uint32_t spins;
    uint32_t changed = 0u;

    AXONS_ENABLE = 0u;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    for (i = 0u; i < AXONS_WIN_WORDS; i++) {
        before[i] = AXONS_REG(4u * i);
    }
    spins = axons_enable_wait();
    SHELL_PRINTF("enable: READY=%u (spins=%u)\n",
                 (unsigned)(AXONS_STATUS & 1u), (unsigned)spins);
    for (i = 0u; i < AXONS_WIN_WORDS; i++) {
        uint32_t now = AXONS_REG(4u * i);
        if (now != before[i]) {
            SHELL_PRINTF("  +%x: %x -> %x\n",
                         (unsigned)(4u * i), (unsigned)before[i],
                         (unsigned)now);
            changed++;
        }
    }
    SHELL_PRINTF("%u of %u words changed across enable\n",
                 (unsigned)changed, (unsigned)AXONS_WIN_WORDS);
}


#if defined(TIKU_AXON_MODEL_TEST) && TIKU_AXON_MODEL_TEST
/*---------------------------------------------------------------------------*/
/* A2b: RUN THE SAME MODEL FROM THE STORE, AND COMPARE                       */
/*---------------------------------------------------------------------------*/
/*
 * `axonsprobe model` runs Nordic's inference test against the model compiled
 * into .rodata.  `axonsprobe modelstore` runs the IDENTICAL vendor test with the
 * same model loaded from /data instead, so the two can be diffed.
 *
 * Everything before this proved the loader reproduces the linker's bytes, on the
 * host.  This is the first point where the NPU is in the loop, and it is the only
 * thing that can show the engine accepts a command buffer patched at runtime.
 *
 * HOW THE SUBSTITUTION WORKS.  The vendor's harness state is global rather than
 * static, so AxonnnModelPrepare() runs exactly as the baked path runs it --
 * populating the model pointer and the test vectors -- and only then is the model
 * pointer repointed at a RAM COPY of the descriptor whose cmd_buffer_ptr is the
 * patched buffer and whose model_const_ptr is the mapped weights.  No vendor
 * source is modified, and the verdict ("output bit exact!") is the vendor's own
 * rather than one written here.
 *
 * LAYER MODELS ARE EXCLUDED deliberately.  The packer extracts only the
 * full-model command buffer, so the layer-mode buffers still hold link-time
 * addresses; running them against relocated weights would mix a relocated and an
 * unrelocated path and make the comparison meaningless.
 */
extern nrf_axon_nn_compiled_model_s const *the_full_model_static_info[1];
extern nrf_axon_nn_model_test_info_s       the_test_vectors[];
extern int  AxonnnModelPrepare(void);

/* The weights stay MAPPED in RRAM; only the command buffer needs RAM.  Sized for
 * the largest model that fits the code window today (tinyml_ic, 38,680 B); vww's
 * 51,344 arrives with A3, when the arrays leave the image and free the room. */
#ifndef AXONS_STORE_CMD_MAX
#define AXONS_STORE_CMD_MAX  40960u
#endif
static uint8_t axons_store_cmd[AXONS_STORE_CMD_MAX] __attribute__((aligned(8)));
static nrf_axon_nn_compiled_model_s axons_store_desc;

/**
 * @brief Publish the firmware addresses a packed model's table may name.
 *
 * Registered as &thing, never as a number: a relocation against a code symbol
 * resolves Thumb-tagged, and taking the address in C is what supplies that bit
 * (see tiku_model.h).  A hand-written constant would be one short, and the
 * symptom is a single corrupt word in the command stream.
 */
static int axons_store_register_syms(void)
{
    extern int axonpro_int8_packing_filter(void);
    extern int nrf_axon_nn_op_extension_softmax(void);
    int rc;

    tiku_model_sym_reset();
    rc = tiku_model_sym_register("nrf_axon_interlayer_buffer",
                                 (uintptr_t)nrf_axon_interlayer_buffer);
    if (rc == TIKU_MODEL_OK) {
        rc = tiku_model_sym_register("axonpro_int8_packing_filter",
                                     (uintptr_t)&axonpro_int8_packing_filter);
    }
    if (rc == TIKU_MODEL_OK) {
        rc = tiku_model_sym_register("nrf_axon_nn_op_extension_softmax",
                                     (uintptr_t)&nrf_axon_nn_op_extension_softmax);
    }
    return rc;
}

static void axons_model_from_store(const char *name)
{
    tiku_tfs_t   *fs = tiku_vfs_tree_data_store();
    tiku_model_t  m;
    const char   *bad = NULL;
    size_t        n = 0u;
    uint32_t      t0;
    int           rc;

    if (fs == NULL) {
        SHELL_PRINTF("modelstore: no /data store on this build\n");
        return;
    }
    rc = tiku_model_open(fs, name, &m);
    if (rc != TIKU_MODEL_OK) {
        SHELL_PRINTF("modelstore: open %s: %s\n", name, tiku_model_strerror(rc));
        return;
    }
    if (m.fmt != (uint8_t)TIKU_MODEL_FMT_RELOC) {
        SHELL_PRINTF("modelstore: %s is not a relocatable model\n", name);
        return;
    }
    SHELL_PRINTF("modelstore: %s  weights %u  cmd %u  %u sites / %u syms\n",
                 name, (unsigned)m.weights_len, (unsigned)m.cmd_len,
                 (unsigned)m.nsites, (unsigned)m.nsyms);
    /* ALIGNMENT IS REPORTED FOR DIAGNOSIS, NOT BECAUSE IT IS A CONSTRAINT.  The
     * store does not guarantee it: a mapped file starts at slot_off + 4 (the
     * length word) and Nordic's slot stride is 4100 bytes -- neither a multiple
     * of 16 -- so a file's base alignment depends on which slot it landed in.
     *
     * That looked like a hazard, so it was measured rather than assumed, and it
     * is NOT one.  Three models ran bit-exact from the store at weights
     * alignment 0 (kws), 8 (ic) and 12 (ad); kws was then deliberately shifted
     * to alignment 4 by provisioning a padding file ahead of it and produced
     * byte-identical results again.  Four of the four possible 4-byte phases
     * work, so the NPU is reading these blobs without an alignment requirement
     * this layer has to satisfy.
     *
     * Kept because it costs one line and is the first thing worth reading if a
     * future model ever misbehaves only in some store layouts -- but on today's
     * evidence, "aligned 12" is an observation, not a suspect. */
    SHELL_PRINTF("modelstore: weights @%p (align %u)  cmd RAM @%p (align %u)\n",
                 (const void *)m.weights,
                 (unsigned)((uintptr_t)m.weights & 15u),
                 (const void *)axons_store_cmd,
                 (unsigned)((uintptr_t)axons_store_cmd & 15u));

    rc = axons_store_register_syms();
    if (rc != TIKU_MODEL_OK) {
        SHELL_PRINTF("modelstore: symbol registry: %s\n",
                     tiku_model_strerror(rc));
        return;
    }
    rc = tiku_model_prepare(&m, axons_store_cmd, sizeof axons_store_cmd,
                            &n, &bad);
    if (rc != TIKU_MODEL_OK) {
        SHELL_PRINTF("modelstore: prepare: %s%s%s\n", tiku_model_strerror(rc),
                     bad ? ": " : "", bad ? bad : "");
        return;
    }
    SHELL_PRINTF("modelstore: relocated %u sites into %u B of RAM\n",
                 (unsigned)m.nsites, (unsigned)n);

    if (nrf_axon_platform_init() != NRF_AXON_RESULT_SUCCESS) {
        SHELL_PRINTF("modelstore: axon platform init failed\n");
        return;
    }
    if (AxonnnModelPrepare() < 0) {
        SHELL_PRINTF("modelstore: AxonnnModelPrepare failed\n");
        nrf_axon_platform_close();
        return;
    }
    axons_store_desc = *the_full_model_static_info[0];
    axons_store_desc.cmd_buffer_ptr =
        (const NRF_AXON_PLATFORM_BITWIDTH_UNSIGNED_TYPE *)axons_store_cmd;
    /* model_const_ptr is read nowhere in the SDK -- the weights are reached
     * through the command buffer's patched addresses -- but it is repointed so
     * nothing left in the descriptor still names .rodata. */
    axons_store_desc.model_const_ptr  = m.weights;
    axons_store_desc.model_const_size = (uint32_t)m.weights_len;
    the_full_model_static_info[0] = &axons_store_desc;

    SHELL_PRINTF("modelstore: running vendor test vectors from the STORE\n");
    t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
    (void)nrf_axon_nn_run_test_vectors(the_full_model_static_info, NULL, 1,
                                       NULL, NULL, the_test_vectors);
    SHELL_PRINTF("modelstore: total %u us\n",
                 (unsigned)(NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0));
    nrf_axon_platform_close();
}
#endif  /* TIKU_AXON_MODEL_TEST */

void tiku_shell_cmd_axonsprobe(uint8_t argc, const char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "en") == 0) {
        uint32_t spins = axons_enable_wait();
        if (spins != 0u) {
            SHELL_PRINTF("READY after %u spins\n", (unsigned)spins);
        } else {
            SHELL_PRINTF("NOT ready after %u spins (ENABLE=%x STATUS=%x)\n",
                         (unsigned)AXONS_READY_SPIN,
                         (unsigned)AXONS_ENABLE, (unsigned)AXONS_STATUS);
        }
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "off") == 0) {
        AXONS_ENABLE = 0u;
        SHELL_PRINTF("disabled (ENABLE=%x STATUS=%x)\n",
                     (unsigned)AXONS_ENABLE, (unsigned)AXONS_STATUS);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "dump") == 0) {
        uint32_t off   = (argc >= 3)
                       ? (uint32_t)strtoul(argv[2], (char **)0, 16) : 0u;
        uint32_t words = (argc >= 4)
                       ? (uint32_t)strtoul(argv[3], (char **)0, 10) : 16u;
        axons_dump(off, words);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "diff") == 0) {
        axons_diff();
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "irq") == 0) {
        axons_irq_count = 0u;
        tiku_nordic_nvic_enable(86);
        SHELL_PRINTF("IRQ 86 armed; enabling block...\n");
        (void)axons_enable_wait();
        tiku_cpu_nordic_delay_ms(50u);
        SHELL_PRINTF("irq count = %u (STATUS=%x)\n",
                     (unsigned)axons_irq_count, (unsigned)AXONS_STATUS);
        return;
    }

#if defined(TIKU_AXON_ENABLE) && TIKU_AXON_ENABLE
    /* One-time vendor platform/driver init shared by every subcommand that
     * enters the blob.  Skipping it leaves the driver's engine base NULL and
     * the first intrinsic bus-faults at base+offset (BFAR 0x520, seen on HW
     * when `fir` ran before `hw`). */
    {
        static int axon_inited;
        if (!axon_inited && argc >= 2 &&
            (strcmp(argv[1], "hw") == 0 ||
             strcmp(argv[1], "acc") == 0 ||
             strcmp(argv[1], "fir") == 0)) {
            nrf_axon_result_e rc = nrf_axon_platform_init();
            SHELL_PRINTF("nrf_axon_platform_init -> %d\n", (int)rc);
            if (rc != NRF_AXON_RESULT_SUCCESS) {
                return;
            }
            axon_inited = 1;
        }
    }
    if (argc >= 2 && strcmp(argv[1], "hw") == 0) {
        if (!nrf_axon_platform_reserve_for_user()) {
            SHELL_PRINTF("reserve failed\n");
            return;
        }
        SHELL_PRINTF("powered on: ENABLE=%x STATUS=%x %s\n",
                     (unsigned)AXONS_ENABLE, (unsigned)AXONS_STATUS,
                     (AXONS_STATUS & 1u) ? "(READY!)" : "(still not ready)");
        axons_dump(0u, 16u);
        nrf_axon_platform_free_reservation_from_user();
        SHELL_PRINTF("released: ENABLE=%x STATUS=%x\n",
                     (unsigned)AXONS_ENABLE, (unsigned)AXONS_STATUS);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "acc") == 0) {
        /* KAT: sum of a 24-bit vector on the NPU vs the CPU loop.
         * axon_acc_24_32 handles reservation itself (keep_reservation=false)
         * -- but platform init must have run (axonsprobe hw first). */
        static int32_t x[16] __attribute__((aligned(4)));
        int32_t hw_out = 0;
        int32_t sw_out = 0;
        uint32_t i;
        uint32_t t0, t1;
        nrf_axon_result_e rc;

        for (i = 0u; i < 16u; i++) {
            x[i] = (int32_t)(i * 1000u + 7u) - 8000;   /* mixed signs */
            sw_out += x[i];
        }
        t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        rc = axon_acc_24_32(x, &hw_out, 16u, 0u,
                            NRF_AXON_SYNC_MODE_BLOCKING_POLLING, false);
        t1 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        SHELL_PRINTF("axon_acc_24_32 rc=%d  hw=%d sw=%d  %s  (%u us)\n",
                     (int)rc, (int)hw_out, (int)sw_out,
                     (rc == NRF_AXON_RESULT_SUCCESS && hw_out == sw_out)
                         ? "MATCH" : "MISMATCH",
                     (unsigned)(t1 - t0));
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "fir") == 0) {
        /* MAC-throughput benchmark with an EXACT CPU reference: a batch of
         * dot products (axon_mar_24_24_32 -- 32-bit output, no truncation).
         * REPS x LEN multiply-accumulates on the NPU vs a plain int32 CPU
         * loop, both timed on the 1 MHz GRTC.  Values are bounded so 24-bit
         * operands and the 32-bit accumulator never saturate, so hw==sw is a
         * true KAT rather than a convention guess (the FIR intrinsic's
         * fixed-point rounding makes a naive reference ambiguous). */
        enum { DOT_LEN = 512, DOT_REPS = 128 };  /* 65,536 MACs/rep */
        static int32_t x[DOT_LEN] __attribute__((aligned(4)));
        static int32_t y[DOT_LEN] __attribute__((aligned(4)));
        uint32_t i, r, t0, t_hw, t_sw;
        int32_t hw = 0, sw = 0;
        uint32_t mism = 0u;
        nrf_axon_result_e rc = NRF_AXON_RESULT_SUCCESS;

        for (i = 0u; i < DOT_LEN; i++) {
            x[i] = (int32_t)(((i * 2654435761u) >> 20) & 0x7Fu) - 64;  /* [-64,63] */
            y[i] = (int32_t)(((i * 40503u) >> 6) & 0x7Fu) - 64;
        }

        t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        for (r = 0u; r < DOT_REPS; r++) {
            int32_t out = 0;
            nrf_axon_result_e rr = axon_mar_24_24_32(
                x, y, &out, DOT_LEN, 0u,
                NRF_AXON_SYNC_MODE_BLOCKING_POLLING, false);
            if (rr != NRF_AXON_RESULT_SUCCESS) { rc = rr; }
            hw = out;
        }
        t_hw = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0;

        t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        for (r = 0u; r < DOT_REPS; r++) {
            int32_t acc = 0;
            for (i = 0u; i < DOT_LEN; i++) {
                acc += x[i] * y[i];
            }
            sw = acc;
        }
        t_sw = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0;

        if (hw != sw) {
            SHELL_PRINTF("  hw=%d sw=%d\n", (int)hw, (int)sw);
            mism = 1u;
        }
        SHELL_PRINTF("dot 128x512 (65K MAC) rc=%d  npu=%u us  cpu=%u us  "
                     "speedup=%u.%ux  %s\n",
                     (int)rc, (unsigned)t_hw, (unsigned)t_sw,
                     (unsigned)(t_sw / (t_hw ? t_hw : 1u)),
                     (unsigned)((10u * t_sw / (t_hw ? t_hw : 1u)) % 10u),
                     (rc == NRF_AXON_RESULT_SUCCESS && mism == 0u)
                         ? "MATCH" : "CHECK");
        return;
    }
#if defined(TIKU_AXON_MODEL_TEST) && TIKU_AXON_MODEL_TEST
    if (argc >= 2 && strcmp(argv[1], "model") == 0) {
        /* Nordic's portable inference test: runs the compiled model
         * (TIKU_AXON_MODEL=... on the make line) against its shipped test
         * vectors and prints per-vector results. */
        extern void base_inference_main(void);
        uint32_t t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        base_inference_main();
        SHELL_PRINTF("model run total: %u us\n",
                     (unsigned)(NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0));
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "modelstore") == 0) {
        axons_model_from_store(argc >= 3 ? argv[2] : "kws.axm");
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "modelbaked") == 0) {
        /* CONTROL for modelstore: the BAKED descriptor run through the SAME
         * restricted vendor call (full model only, no layer models).  If this
         * behaves like modelstore then the store is exonerated and the
         * difference is the harness restriction, not the relocation. */
        uint32_t t0;
        if (nrf_axon_platform_init() != NRF_AXON_RESULT_SUCCESS) {
            SHELL_PRINTF("modelbaked: axon platform init failed\n");
            return;
        }
        if (AxonnnModelPrepare() < 0) {
            SHELL_PRINTF("modelbaked: AxonnnModelPrepare failed\n");
            nrf_axon_platform_close();
            return;
        }
        SHELL_PRINTF("modelbaked: baked descriptor, full model only\n");
        t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        (void)nrf_axon_nn_run_test_vectors(the_full_model_static_info, NULL, 1,
                                           NULL, NULL, the_test_vectors);
        SHELL_PRINTF("modelbaked: total %u us\n",
                     (unsigned)(NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0));
        nrf_axon_platform_close();
        return;
    }
#endif
#endif /* TIKU_AXON_ENABLE */

    axons_info();
#if defined(TIKU_AXON_ENABLE) && TIKU_AXON_ENABLE
    SHELL_PRINTF("usage: axonsprobe [en|off|dump <off> <n>|diff|irq|hw|acc]\n");
#else
    SHELL_PRINTF("usage: axonsprobe [en|off|dump <off> <n>|diff|irq]\n");
#endif
}

#else /* !TIKU_DEVICE_HAS_AXONS */

void tiku_shell_cmd_axonsprobe(uint8_t argc, const char *argv[])
{
    (void)argc; (void)argv;
    SHELL_PRINTF("no AXONS block on this device (build MCU=nrf54lm20b)\n");
}

#endif /* TIKU_DEVICE_HAS_AXONS */

#endif /* TIKU_SHELL_CMD_AXONSPROBE */
