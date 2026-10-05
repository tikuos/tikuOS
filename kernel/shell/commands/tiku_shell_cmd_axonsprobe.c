/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_axonsprobe.c - Axon NPU probe and test commands.
 *
 * The MDK documents only the power wrapper, so the raw sub-commands enable
 * the block, dump and diff its engine window, and write nothing in it but
 * ENABLE.  With the vendor driver built in, the rest run the engine and models.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_axonsprobe.h"

#if TIKU_SHELL_CMD_AXONSPROBE

#include <kernel/shell/tiku_shell_io.h>
#include <kernel/cpu/tiku_hang.h>   /* check-in during hold/busy loops */
#include <arch/nordic/tiku_device_select.h>
#include <arch/nordic/tiku_cpu_common.h>
#include <string.h>
#include <stdlib.h>

#if defined(TIKU_DEVICE_HAS_AXONS) && TIKU_DEVICE_HAS_AXONS

#if defined(TIKU_AXON_ENABLE) && TIKU_AXON_ENABLE
/* Nordic's Axon driver core from the checkout at AXON_SDK (see the Makefile),
 * on the TikuOS platform layer (arch/nordic/tiku_axon_platform.c). */
#include "drivers/axon/nrf_axon_driver.h"
#include "axon/nrf_axon_platform.h"
#include "drivers/axon/nrf_axon_dsp_intrinsics.h"

/* Nordic's inference sources build in two configurations, and the store path
 * exists in both:
 *
 *   TIKU_AXON_MODEL_TEST       one model baked into .rodata; the store path
 *                              runs beside it, so the two can be compared.
 *   TIKU_AXON_MODEL_FROM_STORE no model compiled in; the store path is the
 *                              only path.
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

/** Upper bound on the READY poll, in loop iterations. */
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

/** @brief Print ENABLE, STATUS and the FICR part and revision. */
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

/**
 * @brief Print @p words words of the block from byte offset @p off.
 *
 * @p off is rounded down to a word; a count of 0 or above 1024 prints 16.
 */
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
            /* Each row's address is printed before its reads, so if a read
             * faults, the last line printed names the row. */
            SHELL_PRINTF("\n  +%x:", (unsigned)o);
        }
        SHELL_PRINTF(" %x", (unsigned)AXONS_REG(o));
    }
    SHELL_PRINTF("\n");
}

/** @brief Snapshot the window with the block off, enable it, print changes. */
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
/* RUNNING A MODEL FROM THE STORE                                            */
/*---------------------------------------------------------------------------*/
/*
 * `axonsprobe model` runs Nordic's inference test against the model compiled
 * into .rodata; `axonsprobe modelstore` runs the same vendor test with the
 * model loaded from /data, so the two results can be compared.
 *
 * The vendor harness state is global, so AxonnnModelPrepare() runs as it does
 * on the baked path, setting the model pointer and the test vectors.  The
 * model pointer is then repointed at a RAM copy of the descriptor whose
 * cmd_buffer_ptr is the relocated command buffer and whose model_const_ptr is
 * the mapped weights.  No vendor source is modified, and the pass or fail
 * verdict is the vendor test's own.
 *
 * Layer models are not run: the packer extracts only the full-model command
 * buffer, so the layer-mode buffers still hold link-time addresses.
 */
#if defined(TIKU_AXON_MODEL_FROM_STORE) && TIKU_AXON_MODEL_FROM_STORE
/* This configuration compiles no model, and so not the vendor test app that
 * defines these globals and AxonnnModelPrepare() in a baked build.  They are
 * defined here under the same names and types, so the store path below is
 * the same code in both configurations.  AxonnnModelPrepare() has nothing to
 * prepare: the descriptor and the vectors both come from the store. */
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

/* The weights stay mapped in RRAM; only the command buffer is built in RAM.
 * Sized for the largest command buffer in the shipped tinyml set (tinyml_vww);
 * a larger one is refused at prepare. */
#ifndef AXONS_STORE_CMD_MAX
#define AXONS_STORE_CMD_MAX  53248u
#endif
static uint8_t axons_store_cmd[AXONS_STORE_CMD_MAX] __attribute__((aligned(8)));

/* The descriptor is built into a real struct, so the compiler supplies the
 * alignment the engine expects. */
static nrf_axon_nn_compiled_model_s axons_store_desc;

/* Label pointers and packed output for the loaded model, sized for the
 * largest shipped one; a model needing more of either is refused at load. */
#define AXONS_STORE_LABEL_MAX  32u
/* An autoencoder's packed output is a whole reconstructed frame rather than a
 * few class scores; tinyml_ad sets this size. */
#define AXONS_STORE_POUT_MAX   4096u
static const char *axons_store_labels[AXONS_STORE_LABEL_MAX];
static uint8_t axons_store_pout[AXONS_STORE_POUT_MAX]
                                            __attribute__((aligned(8)));

/**
 * @brief Register the firmware addresses a packed model's table may name.
 *
 * Each address is taken in C, never written as a number: a code symbol's
 * address carries the Thumb bit, which a hand-written constant would miss.
 * The packed-output symbol names a buffer this caller lends the model.
 *
 * @return TIKU_MODEL_OK, or the first registration error
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
/* KNOWN-ANSWER VECTORS FROM THE STORE                                       */
/*---------------------------------------------------------------------------*/
/*
 * The vendor harness compares each inference with an expected output, and
 * those vectors are C arrays too, so a model-free image has none.  They are
 * packed into a companion .kat file (tools/axonpack.py --kat) and mapped from
 * the store here.
 *
 * The .kat is a separate file, not a section of the .axm: the vectors are the
 * test harness's, and a product provisions the model without them.  Only the
 * full-model vectors are packed, since the store path runs no layer models.
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
 * The vectors are used in place in NVM; only the two pointer arrays are built
 * in RAM.
 *
 * @param fs         Store holding the file
 * @param name       .kat file name
 * @param info       Vendor test info to populate
 * @param test_name  Name the vendor harness prints for the run
 * @return 0 on success, or -1 with a reason already printed
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
 * @brief Compare the descriptor built from the store with the linked one.
 *
 * The scalar fields listed below and the two interlayer-relative pointers
 * (output_ptr, inputs[0].ptr) must match; pointers into the model's own
 * sections and the lent output buffer differ between the two and are skipped.
 *
 * @param got   Descriptor built from the store
 * @param want  Descriptor the linker built
 * @return The number of differing fields (0 is the pass)
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

    /* Both descriptors point into the same interlayer buffer, so these must be
     * equal; a difference means a REL addend was patched wrong. */
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

/**
 * @brief Load model @p name from /data and run the vendor test vectors on it.
 *
 * @param name  Model file (.axm)
 * @param kat   Known-answer file (.kat), or NULL to use the baked vectors
 */
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
    /* Alignment is printed for diagnosis only.  The store does not align a
     * file to 16 bytes: a mapped file starts after its slot's 4-byte length
     * word, and slots are TIKU_TFS_SLOT_DATA + 4 bytes apart, so its
     * alignment depends on the slot it landed in.  The NPU reads these blobs
     * from any 4-byte-aligned address. */
    SHELL_PRINTF("modelstore: weights @%p (align %u)  cmd RAM @%p (align %u)\n",
                 (const void *)m.weights,
                 (unsigned)((uintptr_t)m.weights & 15u),
                 (const void *)axons_store_cmd,
                 (unsigned)((uintptr_t)axons_store_cmd & 15u));

    /* The packed descriptor holds the vendor struct as compiled into the
     * packer's input.  A different size here means this build's SDK lays it
     * out differently, the fields would land in the wrong places, and the
     * model must be repacked.  A layout change that keeps the size is not
     * caught. */
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

    /* The file cannot know three pointers, because this run chose them: where
     * the commands and the labels were built and where the store mapped the
     * weights.  Every other descriptor field came from the file, relocated by
     * the loader. */
    axons_store_desc.cmd_buffer_ptr =
        (const NRF_AXON_PLATFORM_BITWIDTH_UNSIGNED_TYPE *)axons_store_cmd;
    axons_store_desc.labels = (m.nlabels != 0u) ? axons_store_labels : NULL;
    /* The SDK does not read model_const_ptr (the command buffer's patched
     * addresses reach the weights); it is repointed so no descriptor field
     * still names .rodata. */
    axons_store_desc.model_const_ptr  = m.weights;
    axons_store_desc.model_const_size = (uint32_t)m.weights_len;

#if defined(TIKU_AXON_MODEL_TEST) && TIKU_AXON_MODEL_TEST
    /* A baked build has the linker's own descriptor, so the one built from
     * the store is compared with it field by field before either is used. */
    if (the_full_model_static_info[0] != NULL) {
        unsigned bad_fields =
            axons_store_desc_check(&axons_store_desc,
                                   the_full_model_static_info[0]);
        SHELL_PRINTF("modelstore: descriptor vs baked: %u field%s differ\n",
                     bad_fields, (bad_fields == 1u) ? "" : "s");
    }
#endif
    the_full_model_static_info[0] = &axons_store_desc;

    /* Known answers come from the named .kat, else from the baked vectors; a
     * model-free image has none baked, so it needs the .kat. */
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
        /* Close the driver session first: ENABLE=0 under a non-zero platform
         * refcount leaves the two disagreeing, and the next reservation then
         * trusts the refcount and skips the enable. */
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
    /* One-time vendor platform and driver init for hw, acc, fir, hold and
     * busy; the model commands run their own.  Skipping it leaves the
     * driver's engine base NULL, and the first intrinsic bus-faults. */
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
        /* Known-answer test: the sum of a 24-bit vector on the NPU against a
         * CPU loop.  axon_acc_24_32 takes and drops its own reservation
         * (keep_reservation=false); platform init ran above. */
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
         * Hold the NPU in one state for a caller-chosen time, so an external
         * meter can average over the window:
         *
         *   hold <ms>  block powered and reserved, nothing issued  (static)
         *   busy <ms>  the same, issuing MAC ops back to back      (dynamic)
         *
         * busy minus hold is the dynamic cost, and hold minus a run with the
         * block disabled is the cost of powering it; each difference cancels
         * any fixed offset on the supply rail.
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
                /* keep_reservation = false: the reservation taken above holds
                 * the block powered for the whole loop, so the per-op release
                 * never drops the refcount to zero and the engine does not
                 * power-cycle between ops.  Passing true would leak one
                 * reference per op. */
                (void)axon_mar_24_24_32(bx, by, &out, DOT_LEN, 0u,
                                        NRF_AXON_SYNC_MODE_BLOCKING_POLLING,
                                        false);
                ops++;
            } else {
                /* Powered and reserved with nothing issued, core asleep: the
                 * block's static cost. */
                __asm__ volatile ("wfi" ::: "memory");
            }
            /* Both arms block this process for the whole window, so check in:
             * the hang detector otherwise resets the board after
             * TIKU_HANG_THRESHOLD_TICKS without progress (tiku_hang.h). */
            tiku_hang_checkin();
        } while ((uint32_t)(NRF_GRTC_S->SYSCOUNTER[0].SYSCOUNTERL - t0)
                 < ms * 1000u);
        nrf_axon_platform_free_reservation_from_user();
        /* Then close the session: a stray reservation anywhere would keep the
         * engine powered into the next reading, and close() zeroes the
         * refcount and disables the block. */
        nrf_axon_platform_close();
        SHELL_PRINTF("%s done: %lu ops, ENABLE=%x\n", argv[1],
                     (unsigned long)ops, (unsigned)AXONS_ENABLE);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "fir") == 0) {
        /* MAC-throughput benchmark with an exact CPU reference: DOT_REPS dot
         * products of DOT_LEN elements on the NPU (axon_mar_24_24_32, 32-bit
         * output, no rounding) against a plain int32 CPU loop, both timed on
         * the 1 MHz GRTC.  Operands are bounded so that neither the 24-bit
         * inputs nor the 32-bit accumulator saturates, which makes hw == sw
         * an exact check.  The FIR intrinsic is not used: its fixed-point
         * rounding has no exact CPU reference. */
        enum { DOT_LEN = 512, DOT_REPS = 128 };  /* 65,536 MACs in all */
        static int32_t x[DOT_LEN] __attribute__((aligned(4)));
        static int32_t y[DOT_LEN] __attribute__((aligned(4)));
        uint32_t i, r, t0, t_hw, t_sw;
        int32_t hw = 0, sw = 0;
        uint32_t mism = 0u;
        nrf_axon_result_e rc = NRF_AXON_RESULT_SUCCESS;

        for (i = 0u; i < DOT_LEN; i++) {
            /* Both in [-64, 63]. */
            x[i] = (int32_t)(((i * 2654435761u) >> 20) & 0x7Fu) - 64;
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
    /* The one model command in both configurations, and the only one in a
     * model-free image. */
    if (argc >= 2 && strcmp(argv[1], "modelstore") == 0) {
        axons_model_from_store(argc >= 3 ? argv[2] : "kws.axm",
                               argc >= 4 ? argv[3] : NULL);
        return;
    }
#endif
#if defined(TIKU_AXON_MODEL_TEST) && TIKU_AXON_MODEL_TEST
    /* Baked-model commands, the reference the store path is compared with;
     * they exist only where a model is compiled in. */
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
        /* The baked descriptor through the same full-model-only vendor call
         * as modelstore (no layer models), so the two runs can be compared. */
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
