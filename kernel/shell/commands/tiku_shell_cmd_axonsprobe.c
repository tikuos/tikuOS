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

/* Nordic's inference sources are compiled in two configurations, and the store
 * path is wanted in both:
 *
 *   TIKU_AXON_MODEL_TEST       one model baked into .rodata.  The store path
 *                              runs beside it, so the two can be diffed --
 *                              this is the configuration that PROVES the store.
 *   TIKU_AXON_MODEL_FROM_STORE no model compiled at all.  The store path is the
 *                              only path -- this is the shipping shape.
 */
#if (defined(TIKU_AXON_MODEL_TEST) && TIKU_AXON_MODEL_TEST) || \
    (defined(TIKU_AXON_MODEL_FROM_STORE) && TIKU_AXON_MODEL_FROM_STORE)
#define AXONS_HAVE_NN 1
#endif

#if defined(AXONS_HAVE_NN)
#include "drivers/axon/nrf_axon_nn_infer.h"
#include "drivers/axon/nrf_axon_nn_infer_test.h"
#include <kernel/fs/tiku_model.h>
#include <kernel/memory/tiku_nvm_mirror.h>
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


#if defined(AXONS_HAVE_NN)
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
#if defined(TIKU_AXON_MODEL_FROM_STORE) && TIKU_AXON_MODEL_FROM_STORE
/* NO MODEL TRANSLATION UNIT IS COMPILED in this configuration, so the four
 * globals it used to define live here instead -- same names, same types, same
 * (non-static) linkage.  Nordic's inference sources are untouched and cannot
 * tell the difference; they were already written against these as externs.
 *
 * AxonnnModelPrepare() has nothing to prepare: there is no baked model to point
 * at and no baked vectors to populate.  Both are filled in from the store. */
nrf_axon_nn_compiled_model_s const *the_full_model_static_info[1];
nrf_axon_nn_compiled_model_layer_s const **the_model_layers_static_info[1]
                                                                    = { NULL };
uint16_t model_layers_count[1] = { 0 };
nrf_axon_nn_model_test_info_s the_test_vectors[1];

int AxonnnModelPrepare(void)
{
    return 0;
}
#else
extern nrf_axon_nn_compiled_model_s const *the_full_model_static_info[1];
extern nrf_axon_nn_model_test_info_s       the_test_vectors[];
extern int  AxonnnModelPrepare(void);
#endif

/* The weights stay MAPPED in RRAM; only the command buffer needs RAM.  Sized for
 * the largest command buffer in the shipped tinyml set (tinyml_vww, 51,344 B) --
 * reachable only from a model-free image, which is exactly the configuration
 * that has the RAM spare. */
#ifndef AXONS_STORE_CMD_MAX
#define AXONS_STORE_CMD_MAX  53248u
#endif
static uint8_t axons_store_cmd[AXONS_STORE_CMD_MAX] __attribute__((aligned(8)));

/* The descriptor is built INTO a real struct, so the compiler supplies the
 * alignment the engine expects rather than this code asserting it. */
static nrf_axon_nn_compiled_model_s axons_store_desc;

/* The label pointer array and the model's packed output.  Both are small and
 * both are per-model, so they are sized generously once rather than tuned:
 * the shipped models use 12 labels and 8 bytes of packed output. */
#define AXONS_STORE_LABEL_MAX  32u
/* Packed output.  64 bytes looked generous next to the classifiers' 8 -- and
 * then tinyml_ad asked for 2560, because an autoencoder's output is a whole
 * reconstructed frame rather than a handful of class scores.  The model states
 * its own requirement in the file and the load refuses rather than overflows,
 * so this only ever needs to be large enough; 4 KB clears the shipped set. */
#define AXONS_STORE_POUT_MAX   4096u
static const char *axons_store_labels[AXONS_STORE_LABEL_MAX];
static uint8_t axons_store_pout[AXONS_STORE_POUT_MAX]
                                            __attribute__((aligned(8)));

/**
 * @brief Publish the firmware addresses a packed model's table may name.
 *
 * Registered as &thing, never as a number: a relocation against a code symbol
 * resolves Thumb-tagged, and taking the address in C is what supplies that bit
 * (see tiku_model.h).  A hand-written constant would be one short, and the
 * symptom is a single corrupt word in the command stream.
 *
 * @packed_out is registered here rather than resolved by the loader because it
 * is neither in the file nor a fixed firmware address -- it is a buffer this
 * caller lends the model.  The loader owns the names for the model's own parts
 * and lets everything else fall through to the registry, which is exactly the
 * seam that makes that possible.
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
    if (rc == TIKU_MODEL_OK) {
        rc = tiku_model_sym_register("@packed_out",
                                     (uintptr_t)axons_store_pout);
    }
    return rc;
}

/*---------------------------------------------------------------------------*/
/* A3: THE KNOWN ANSWERS, ALSO FROM THE STORE                                */
/*---------------------------------------------------------------------------*/
/*
 * The vendor harness compares each inference against a shipped expected output,
 * and those vectors are C arrays too -- 315 KB of .rodata for tinyml_vww, more
 * than its weights.  A model-free image cannot carry them either, so they are
 * packed into a companion .kat file (tools/axonpack.py --kat) and read here.
 *
 * DELIBERATELY A SEPARATE FILE, NOT A SECTION OF THE .axm.  These are the test
 * harness's known answers, not part of the model: a product provisions the
 * model and never the vectors.  Keeping them apart is what lets the shipping
 * path be the small one.
 *
 * ONLY THE FULL-MODEL VECTORS ARE PACKED.  The other 232 KB is layer-by-layer
 * data, and the store path does not run layer models -- their command buffers
 * still hold link-time addresses (see the note above), so including their
 * vectors would only invite a comparison that cannot mean anything.
 */
#define AKT_MAGIC       0x31544B41u    /* 'AKT1' little-endian */
#define AKT_VERSION     1u
#define AKT_HDR_BYTES   48u
#define AXONS_KAT_MAX   8u             /* vector pairs; the shipped set has 3 */

static const int8_t *axons_kat_in[AXONS_KAT_MAX];
static const int8_t *axons_kat_exp[AXONS_KAT_MAX];

/**
 * @brief Map a .kat and point @p info at its vectors.
 *
 * The vectors are USED IN PLACE out of NVM -- only the two pointer arrays are
 * built in RAM, which is why an 83 KB KAT costs 64 bytes of SRAM.
 *
 * @return 0 on success, or -1 with a reason already printed.
 */
static int axons_kat_load(tiku_tfs_t *fs, const char *name,
                          nrf_axon_nn_model_test_info_s *info,
                          const char *test_name)
{
    const void *p = NULL;
    size_t      n = 0u;
    const uint8_t *b;
    uint32_t hdr[10];
    uint32_t i;

    if (tiku_tfs_map(fs, name, &p, &n) != TFS_OK) {
        SHELL_PRINTF("modelstore: no such KAT file: %s\n", name);
        return -1;
    }
    b = (const uint8_t *)p;
    if (n < AKT_HDR_BYTES) {
        SHELL_PRINTF("modelstore: %s is too short to be a KAT\n", name);
        return -1;
    }
    memcpy(hdr, b, sizeof hdr);
    if (hdr[0] != AKT_MAGIC || hdr[1] != AKT_VERSION ||
        hdr[2] != AKT_HDR_BYTES) {
        SHELL_PRINTF("modelstore: %s is not a v%u KAT\n", name,
                     (unsigned)AKT_VERSION);
        return -1;
    }
    {
        uint32_t nvec = hdr[3], in_off = hdr[4], in_str = hdr[5];
        uint32_t ex_off = hdr[6], ex_str = hdr[7];

        if (nvec == 0u || nvec > AXONS_KAT_MAX) {
            SHELL_PRINTF("modelstore: %s has %u vectors, room for %u\n",
                         name, (unsigned)nvec, (unsigned)AXONS_KAT_MAX);
            return -1;
        }
        /* Bounds as subtractions, never additions: two file-supplied u32s can
         * wrap, and a wrapped sum passes a naive comparison. */
        if (in_str == 0u || ex_str == 0u ||
            in_off > (uint32_t)n || ex_off > (uint32_t)n ||
            in_str > ((uint32_t)n - in_off) / nvec ||
            ex_str > ((uint32_t)n - ex_off) / nvec) {
            SHELL_PRINTF("modelstore: %s geometry does not fit the file\n",
                         name);
            return -1;
        }
        if (tiku_nvm_crc32(b + in_off, in_str * nvec) != hdr[8] ||
            tiku_nvm_crc32(b + ex_off, ex_str * nvec) != hdr[9]) {
            SHELL_PRINTF("modelstore: %s failed its checksum\n", name);
            return -1;
        }
        for (i = 0u; i < nvec; i++) {
            axons_kat_in[i]  = (const int8_t *)(b + in_off + i * in_str);
            axons_kat_exp[i] = (const int8_t *)(b + ex_off + i * ex_str);
        }
        nrf_axon_nn_populate_model_test_info_s(info, test_name,
                                               axons_kat_in, axons_kat_exp,
                                               (uint16_t)nvec, NULL, 0u);
        SHELL_PRINTF("modelstore: KAT %s: %u vectors, input %u B, "
                     "expected %u B (mapped, not copied)\n",
                     name, (unsigned)nvec, (unsigned)in_str, (unsigned)ex_str);
    }
    return 0;
}

#if defined(TIKU_AXON_MODEL_TEST) && TIKU_AXON_MODEL_TEST
/**
 * @brief Compare the descriptor built from the store against the linked one.
 *
 * Only possible in a baked build, and that is the point: this is the on-device
 * counterpart of the packer's host-side reconstruction gate.  The packer proves
 * the FILE reproduces the linker's bytes; this proves the DEVICE does, using
 * the same model, at the addresses it will actually run at.
 *
 * The pointer fields are expected to differ -- they are the whole reason the
 * model was relocated -- so they are reported rather than failed.  Everything
 * else is a scalar the store must reproduce exactly, and a mismatch there means
 * the packed descriptor does not describe the same model.
 *
 * @return the number of differing scalar fields (0 is the pass).
 */
static unsigned axons_store_desc_check(const nrf_axon_nn_compiled_model_s *got,
                                       const nrf_axon_nn_compiled_model_s *want)
{
    unsigned bad = 0u;

#define SCALAR(f, fmt)                                                        \
    do {                                                                      \
        if ((got)->f != (want)->f) {                                          \
            SHELL_PRINTF("  desc." #f " store=" fmt " baked=" fmt "\n",       \
                         (unsigned)(got)->f, (unsigned)(want)->f);            \
            bad++;                                                            \
        }                                                                     \
    } while (0)

    SCALAR(compiler_version, "%x");
    SCALAR(input_cnt, "%u");
    SCALAR(external_input_ndx, "%d");
    SCALAR(interlayer_buffer_needed, "%u");
    SCALAR(psum_buffer_needed, "%u");
    SCALAR(model_const_size, "%u");
    SCALAR(cmd_buffer_len, "%u");
    SCALAR(inputs[0].dimensions.height, "%u");
    SCALAR(inputs[0].dimensions.width, "%u");
    SCALAR(inputs[0].dimensions.channel_cnt, "%u");
    SCALAR(inputs[0].dimensions.byte_width, "%u");
    SCALAR(inputs[0].quant_mult, "%u");
    SCALAR(inputs[0].stride, "%u");
    SCALAR(inputs[0].quant_round, "%u");
    SCALAR(inputs[0].quant_zp, "%d");
    SCALAR(inputs[0].is_external, "%u");
    SCALAR(output_dimensions.height, "%u");
    SCALAR(output_dimensions.width, "%u");
    SCALAR(output_dimensions.channel_cnt, "%u");
    SCALAR(output_dimensions.byte_width, "%u");
    SCALAR(output_dequant_mult, "%u");
    SCALAR(output_dequant_round, "%u");
    SCALAR(output_dequant_zp, "%d");
    SCALAR(output_stride, "%u");
    SCALAR(is_layer_model, "%u");
    SCALAR(persistent_vars.count, "%u");
#undef SCALAR

    /* The interlayer-relative pointers must land at the SAME offsets from the
     * buffer base, even though the base itself is the same in both cases --
     * this catches a descriptor whose REL addends were mispatched. */
    if (got->output_ptr != want->output_ptr) {
        SHELL_PRINTF("  desc.output_ptr store=%p baked=%p\n",
                     (const void *)got->output_ptr,
                     (const void *)want->output_ptr);
        bad++;
    }
    if (got->inputs[0].ptr != want->inputs[0].ptr) {
        SHELL_PRINTF("  desc.inputs[0].ptr store=%p baked=%p\n",
                     (const void *)got->inputs[0].ptr,
                     (const void *)want->inputs[0].ptr);
        bad++;
    }
    return bad;
}
#endif  /* TIKU_AXON_MODEL_TEST */

static void axons_model_from_store(const char *name, const char *kat)
{
    tiku_tfs_t   *fs = tiku_vfs_tree_data_store();
    tiku_model_t  m;
    tiku_model_dest_t dst[TIKU_MODEL_SECT_COUNT];
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

    /* THE DESCRIPTOR'S SIZE IS THE ABI GATE.  The packed bytes are the vendor's
     * struct as the compiler that built the packer's input laid it out; this
     * image's struct must be the same shape or the fields land in the wrong
     * places -- and the engine would read a plausible, wrong model.  A vendor
     * header change is exactly what this catches. */
    if (m.desc_len != sizeof axons_store_desc) {
        SHELL_PRINTF("modelstore: descriptor is %u B, this build expects %u -- "
                     "repack against this SDK\n",
                     (unsigned)m.desc_len, (unsigned)sizeof axons_store_desc);
        return;
    }
    if (m.nlabels > AXONS_STORE_LABEL_MAX) {
        SHELL_PRINTF("modelstore: %u labels, room for %u\n",
                     (unsigned)m.nlabels, (unsigned)AXONS_STORE_LABEL_MAX);
        return;
    }
    if (m.packed_out_len > sizeof axons_store_pout) {
        SHELL_PRINTF("modelstore: wants %u B of packed output, room for %u\n",
                     (unsigned)m.packed_out_len,
                     (unsigned)sizeof axons_store_pout);
        return;
    }

    rc = axons_store_register_syms();
    if (rc != TIKU_MODEL_OK) {
        SHELL_PRINTF("modelstore: symbol registry: %s\n",
                     tiku_model_strerror(rc));
        return;
    }

    dst[TIKU_MODEL_SECT_CMD].dst    = axons_store_cmd;
    dst[TIKU_MODEL_SECT_CMD].cap    = sizeof axons_store_cmd;
    dst[TIKU_MODEL_SECT_DESC].dst   = &axons_store_desc;
    dst[TIKU_MODEL_SECT_DESC].cap   = sizeof axons_store_desc;
    dst[TIKU_MODEL_SECT_LABELS].dst = axons_store_labels;
    dst[TIKU_MODEL_SECT_LABELS].cap = sizeof axons_store_labels;

    rc = tiku_model_prepare_all(&m, dst, &bad);
    if (rc != TIKU_MODEL_OK) {
        SHELL_PRINTF("modelstore: prepare: %s%s%s\n", tiku_model_strerror(rc),
                     bad ? ": " : "", bad ? bad : "");
        return;
    }
    n = m.cmd_len;
    SHELL_PRINTF("modelstore: relocated %u sites -> cmd %u B, desc %u B, "
                 "%u labels\n", (unsigned)m.nsites, (unsigned)n,
                 (unsigned)m.desc_len, (unsigned)m.nlabels);

    if (nrf_axon_platform_init() != NRF_AXON_RESULT_SUCCESS) {
        SHELL_PRINTF("modelstore: axon platform init failed\n");
        return;
    }
    if (AxonnnModelPrepare() < 0) {
        SHELL_PRINTF("modelstore: AxonnnModelPrepare failed\n");
        nrf_axon_platform_close();
        return;
    }

    /* The three pointers the FILE cannot know, because they name things this
     * run chose: where the commands were built, where the labels were built,
     * and where the store mapped the weights.  Everything else in the
     * descriptor -- every dimension, every quantization constant, every buffer
     * size -- came out of the file. */
    axons_store_desc.cmd_buffer_ptr =
        (const NRF_AXON_PLATFORM_BITWIDTH_UNSIGNED_TYPE *)axons_store_cmd;
    axons_store_desc.labels = (m.nlabels != 0u) ? axons_store_labels : NULL;
    /* model_const_ptr is read nowhere in the SDK -- the weights are reached
     * through the command buffer's patched addresses -- but it is repointed so
     * nothing left in the descriptor still names .rodata. */
    axons_store_desc.model_const_ptr  = m.weights;
    axons_store_desc.model_const_size = (uint32_t)m.weights_len;

#if defined(TIKU_AXON_MODEL_TEST) && TIKU_AXON_MODEL_TEST
    /* THE ON-DEVICE RECONSTRUCTION GATE.  A baked build has the linker's own
     * descriptor sitting right there, so the one built from the store can be
     * compared against it field by field before either is used.  This is what
     * turns "the store path ran and the answers matched" into "the store path
     * built the same model" -- the second is the claim, and only this checks
     * it directly. */
    if (the_full_model_static_info[0] != NULL) {
        unsigned bad_fields =
            axons_store_desc_check(&axons_store_desc,
                                   the_full_model_static_info[0]);
        SHELL_PRINTF("modelstore: descriptor vs baked: %u field%s differ\n",
                     bad_fields, (bad_fields == 1u) ? "" : "s");
    }
#endif
    the_full_model_static_info[0] = &axons_store_desc;

    /* The known answers.  A model-free image has no baked vectors, so a KAT
     * file is the only way to have anything to compare against -- and without a
     * comparison "it ran" is not a result. */
    if (kat != NULL) {
        if (axons_kat_load(fs, kat, &the_test_vectors[0],
                           "test_nn_inference_from_store") != 0) {
            nrf_axon_platform_close();
            return;
        }
    } else if (the_test_vectors[0].full_model_vector_count == 0u) {
        SHELL_PRINTF("modelstore: no test vectors -- this image has none baked "
                     "in, so name a .kat file: modelstore %s <file.kat>\n",
                     name);
        nrf_axon_platform_close();
        return;
    }

    SHELL_PRINTF("modelstore: running vendor test vectors from the STORE\n");
    t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
    (void)nrf_axon_nn_run_test_vectors(the_full_model_static_info, NULL, 1,
                                       NULL, NULL, the_test_vectors);
    SHELL_PRINTF("modelstore: total %u us\n",
                 (unsigned)(NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0));
    nrf_axon_platform_close();
}
#endif  /* AXONS_HAVE_NN */

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
#if defined(TIKU_AXON_ENABLE) && TIKU_AXON_ENABLE
        /* Close the driver session first: writing ENABLE=0 while the platform
         * refcount is still non-zero leaves the two views disagreeing, and the
         * refcount is the one that wins the moment anything reserves again. */
        nrf_axon_platform_close();
#endif
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
             strcmp(argv[1], "fir") == 0 ||
             strcmp(argv[1], "hold") == 0 ||
             strcmp(argv[1], "busy") == 0)) {
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
    if (argc >= 2 && (strcmp(argv[1], "busy") == 0 ||
                      strcmp(argv[1], "hold") == 0) && argc >= 3) {
        /*
         * SUSTAINED STATES FOR A CONTROLLED POWER MEASUREMENT.
         *
         * A single inference is ~200 ms and a single intrinsic ~50 us, so
         * neither can be averaged by an external instrument without a marker
         * channel to bound the window.  These two hold the NPU in a KNOWN
         * state for a caller-chosen duration instead, so a plain average over
         * the window is the figure:
         *
         *   hold <ms>  block powered and reserved, doing NOTHING  (static)
         *   busy <ms>  the same, issuing MAC ops back to back     (dynamic)
         *
         * The pair is what makes the measurement controlled: subtracting them
         * isolates the NPU's dynamic cost from its static cost, and
         * subtracting `hold` from a run with the block DISABLED isolates what
         * merely powering it costs.  Every difference is immune to any fixed
         * offset on the supply rail, which matters because ~380 uA of what the
         * rail shows is not the SoC's at all.
         */
        enum { DOT_LEN = 512 };
        static int32_t bx[DOT_LEN] __attribute__((aligned(4)));
        static int32_t by[DOT_LEN] __attribute__((aligned(4)));
        int busy = (strcmp(argv[1], "busy") == 0);
        uint32_t ms = 0u, t0, i, ops = 0u;
        const char *p = argv[2];

        while (*p >= '0' && *p <= '9') { ms = ms * 10u + (uint32_t)(*p++ - '0'); }
        if (ms == 0u) {
            SHELL_PRINTF("Usage: axonsprobe %s <ms>\n", argv[1]);
            return;
        }
        for (i = 0u; i < DOT_LEN; i++) {
            bx[i] = (int32_t)(((i * 2654435761u) >> 20) & 0x7Fu) - 64;
            by[i] = (int32_t)(((i * 40503u) >> 6) & 0x7Fu) - 64;
        }
        if (!nrf_axon_platform_reserve_for_user()) {
            SHELL_PRINTF("reserve failed\n");
            return;
        }
        SHELL_PRINTF("%s %lu ms: ENABLE=%x -- starting\n", argv[1],
                     (unsigned long)ms, (unsigned)AXONS_ENABLE);
        t0 = NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL;
        do {
            if (busy) {
                int32_t out = 0;
                /* keep_reservation = FALSE.  The outer reserve_for_user()
                 * above already holds the block powered for the whole loop, so
                 * a per-op release never drops the refcount to zero and the
                 * engine does not power-cycle between ops.  Passing TRUE leaks
                 * one reference PER OP instead: measured, that left the block
                 * still drawing 2367 uA after the loop against an 834 uA
                 * baseline, and the experiment's own drift check caught it. */
                (void)axon_mar_24_24_32(bx, by, &out, DOT_LEN, 0u,
                                        NRF_AXON_SYNC_MODE_BLOCKING_POLLING,
                                        false);
                ops++;
            } else {
                /* Powered and reserved, core asleep: the block's STATIC cost
                 * with nothing issued to it. */
                __asm__ volatile ("wfi" ::: "memory");
            }
        } while ((uint32_t)(NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0)
                 < ms * 1000u);
        nrf_axon_platform_free_reservation_from_user();
        /* Then FORCE the session closed.  A measurement state is only useful
         * if it can be left, and "balanced" is not the same as "off": any
         * stray reservation anywhere keeps the engine powered and silently
         * contaminates the next reading.  close() zeroes the refcount and
         * disables the hardware, so the baseline is genuinely restorable. */
        nrf_axon_platform_close();
        SHELL_PRINTF("%s done: %lu ops, ENABLE=%x\n", argv[1],
                     (unsigned long)ops, (unsigned)AXONS_ENABLE);
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
#if defined(AXONS_HAVE_NN)
    /* The only model command that exists in BOTH configurations -- and the only
     * one at all in the shipping (model-free) image. */
    if (argc >= 2 && strcmp(argv[1], "modelstore") == 0) {
        axons_model_from_store(argc >= 3 ? argv[2] : "kws.axm",
                               argc >= 4 ? argv[3] : NULL);
        return;
    }
#endif
#if defined(TIKU_AXON_MODEL_TEST) && TIKU_AXON_MODEL_TEST
    /* Baked-model commands.  These are the REFERENCE the store path is measured
     * against, so they exist only where a model was compiled in. */
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
