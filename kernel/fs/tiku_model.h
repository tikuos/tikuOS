/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_model.h - read a model out of the file store instead of out of .rodata.
 *
 * THE PROBLEM.  A compiled neural-network model is emitted as C arrays, so it
 * lands in .rodata -- inside the code window -- and a 208 KB model then sizes
 * the OS's permanent memory contract.  Models are DATA: they belong in /data as
 * named files, swappable over serial without reflashing.
 *
 * WHY THAT IS NOT JUST "PUT THE BYTES IN A FILE".  Weights alone would be: map
 * the file, hand over the pointer, done -- that is the RAW format below.  But
 * an Axon model also carries a COMMAND BUFFER, the instruction stream the NPU
 * executes, and that stream contains ABSOLUTE ADDRESSES of individual weight
 * members, baked in at link time.  Move the weights into a file and every one
 * of those addresses points at where the model used to be.
 *
 * They can be corrected, because the toolchain records every site.  ARM uses
 * REL, so each site's word already holds its own addend, and the fix is exactly
 *
 *     site_word = resolved_base + stored_addend
 *
 * which is what tools/axonpack.py packs the site list for, and what the RELOC
 * format below applies.  That rule is not argued: the packer proves it by
 * applying it on the host with the linker's real addresses and requiring the
 * result to equal the linked image byte for byte.
 *
 * DELIBERATELY NOT AXON-SHAPED.  This layer knows about formats, not engines: it
 * opens by name, maps in place, and dispatches to a per-format prepare().  The
 * RELOC machinery -- a site table plus symbols resolved BY NAME -- is generic,
 * and a pre-linked loadable module has the identical baked-address problem, so
 * one mechanism is meant to serve both rather than each inventing its own.
 *
 * WHY SYMBOLS RESOLVE BY NAME AND NOT BY POSITION.  The symbol COUNT differs
 * per model -- tinyml_ad needs 2 where the others need 4 -- so a positional
 * table would feed one model's slot another model's address.  The failure is
 * not a crash: the patched word is a plausible pointer, inference runs, and the
 * answers are confidently wrong.  Resolving by name makes that impossible, and
 * an unknown name fails immediately, naming the symbol it could not find.
 *
 * VALIDATION POSTURE.  A model file arrives over a serial cable and then sits in
 * NVM where it can rot.  A relocation table is uniquely dangerous input, because
 * every entry says "write four bytes at offset N" -- so structure is checked
 * before anything is used: magic, version, every section inside the file, every
 * site inside the command buffer and 4-byte aligned, every symbol index inside
 * the symbol table, and three CRCs.  A CRC proves the bytes are the ones that
 * were written; the field checks prove they mean what a valid packer would have
 * written.  Anything inconsistent is refused, never repaired.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MODEL_H_
#define TIKU_MODEL_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_tfs.h"

/*---------------------------------------------------------------------------*/
/* STATUS                                                                    */
/*---------------------------------------------------------------------------*/

typedef enum {
    TIKU_MODEL_OK          =  0,
    TIKU_MODEL_ERR_PARAM   = -1,  /**< NULL argument / not open              */
    TIKU_MODEL_ERR_NOENT   = -2,  /**< no such file in the store             */
    TIKU_MODEL_ERR_FORMAT  = -3,  /**< header or geometry inconsistent       */
    TIKU_MODEL_ERR_CRC     = -4,  /**< contents do not match their checksum  */
    TIKU_MODEL_ERR_SPACE   = -5,  /**< caller's buffer too small             */
    TIKU_MODEL_ERR_SYMBOL  = -6,  /**< a named symbol is not registered      */
    TIKU_MODEL_ERR_FULL    = -7   /**< symbol registry has no free slot      */
} tiku_model_err_t;

/** @brief Human-readable form of a tiku_model_* status. */
const char *tiku_model_strerror(int status);

/*---------------------------------------------------------------------------*/
/* FORMATS                                                                   */
/*---------------------------------------------------------------------------*/

typedef enum {
    /** Opaque bytes: quantized weights, a CMSIS-NN model, a palette -- anything
     *  addressed as base+offset, which needs no fixing up at all.  Any file
     *  without the RELOC magic is treated as RAW. */
    TIKU_MODEL_FMT_RAW   = 0,
    /** Carries a relocation table (tools/axonpack.py output, magic 'AXM1'). */
    TIKU_MODEL_FMT_RELOC = 1
} tiku_model_fmt_t;

/*---------------------------------------------------------------------------*/
/* SYMBOL REGISTRY                                                           */
/*---------------------------------------------------------------------------*/

/** @brief Registry capacity.  Real models name 2-4 symbols; the headroom costs
 *  a few pointers and removes a per-model tuning knob. */
#ifndef TIKU_MODEL_SYM_MAX
#define TIKU_MODEL_SYM_MAX  8
#endif

/** @brief Longest symbol name the registry stores, including NUL.
 *  `axon_model_const_tinyml_vww` is 28; 48 leaves room without being a table. */
#ifndef TIKU_MODEL_SYM_NAME_MAX
#define TIKU_MODEL_SYM_NAME_MAX  48
#endif

/**
 * @brief Publish an address a model's relocation table may name.
 *
 * Called by whatever owns the address -- the NPU driver for its interlayer
 * buffer and extension entry points -- typically once at init.  Re-registering
 * a name replaces its address, so a driver re-init is not an error.
 *
 * The model's own weights are NOT registered: symbol index 0 always means "the
 * weights, wherever the store mapped them", which the loader knows without
 * being told.
 *
 * THE THUMB BIT IS THE CALLER'S.  A relocation against a CODE symbol resolves
 * to a Thumb-tagged pointer -- bit 0 SET -- because that is what an ARM function
 * pointer is.  Registering `(uintptr_t)&some_function` gets this right for free,
 * which is why the loader does not (and cannot) add the bit itself: it has no way
 * to know which names are functions.  Registering an address taken from a linker
 * script, an `nm` listing, or a hard-coded constant will therefore be one short
 * on a function, and the symptom is a single wrong word in the command stream --
 * the packer's reconstruction gate caught exactly this as a one-word mismatch on
 * a softmax extension site.  Register `&thing`, never a number.
 *
 * @param name  NUL-terminated symbol name as the packer recorded it.
 * @param addr  Runtime address the name resolves to, Thumb-tagged for code.
 * @return TIKU_MODEL_OK, or ERR_PARAM / ERR_FULL.
 */
int tiku_model_sym_register(const char *name, uintptr_t addr);

/** @brief Forget every registered symbol (test harnesses; re-init). */
void tiku_model_sym_reset(void);

/** @brief Registered symbol count, for observability. */
unsigned tiku_model_sym_count(void);

/*---------------------------------------------------------------------------*/
/* AN OPEN MODEL                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief A model mapped in place, its structure already validated.
 *
 * Every pointer here aims INTO the memory-mapped store, so nothing was copied
 * to open it.  They share tiku_tfs_map()'s lifetime rule: valid until the next
 * write or delete of that file name.  Re-open after re-provisioning.
 */
typedef struct {
    const uint8_t *base;        /**< mapped file base                        */
    size_t         len;         /**< file length                             */
    uint8_t        fmt;         /**< tiku_model_fmt_t                        */

    /* RAW: payload is the whole file.  RELOC: the packed sections. */
    const uint8_t *weights;     /**< RAW: the file; RELOC: the weights blob  */
    size_t         weights_len;
    const uint8_t *cmd;         /**< RELOC: command buffer, UNRELOCATED      */
    size_t         cmd_len;
    const uint8_t *sites;       /**< RELOC: site table (off,sym) pairs       */
    uint32_t       nsites;
    const char    *syms;        /**< RELOC: NUL-separated symbol names       */
    uint32_t       nsyms;
} tiku_model_t;

/**
 * @brief Open a model by file name and validate it end to end.
 *
 * Sniffs the format from the magic, checks every header field and section
 * bound, verifies the CRCs, and -- for RELOC -- walks the whole site table
 * proving each site lies 4-byte aligned inside the command buffer and names a
 * symbol index that exists.  Nothing downstream re-checks those, so a model
 * that opens is structurally safe to use.
 *
 * @param fs    Mounted store (tiku_vfs_tree_data_store()).
 * @param name  File name, e.g. "vww.axm".
 * @param out   Receives the mapped, validated model.
 * @return TIKU_MODEL_OK, or NOENT / FORMAT / CRC / PARAM.
 */
int tiku_model_open(tiku_tfs_t *fs, const char *name, tiku_model_t *out);

/**
 * @brief Make a model ready to run, per its format.
 *
 * RAW: nothing to do; @p dst is untouched and *@p out_len is 0.
 *
 * RELOC: copies the command buffer into @p dst and applies every relocation --
 * `word = resolved(symbol) + stored_addend` -- with symbol 0 resolving to the
 * mapped weights and the rest resolved through the registry.  The WEIGHTS are
 * not copied: they stay mapped in NVM, which is the same memory the engine read
 * them from when they were baked into .rodata.
 *
 * On ERR_SYMBOL the name that could not be resolved is reported through
 * @p bad_sym, so the failure says which symbol rather than merely that one
 * failed.
 *
 * @param m        Model from tiku_model_open().
 * @param dst      Destination for the patched command buffer (RELOC only).
 * @param cap      Bytes available at @p dst.
 * @param out_len  Receives the bytes written.  May be NULL.
 * @param bad_sym  Receives the unresolved symbol name on ERR_SYMBOL.  May be
 *                 NULL.
 * @return TIKU_MODEL_OK, or SPACE / SYMBOL / PARAM.
 */
int tiku_model_prepare(const tiku_model_t *m, void *dst, size_t cap,
                       size_t *out_len, const char **bad_sym);

#endif /* TIKU_MODEL_H_ */
