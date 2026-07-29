/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_model.h - read a model out of the file store instead of out of .rodata.
 *
 * Opens a model by name, maps it in place and dispatches on format: RAW is
 * opaque bytes; RELOC carries a relocation table whose sites are patched from
 * symbols resolved by name.  Engine-agnostic.  Packed by tools/axonpack.py.
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
 * Called once at init; re-registering a name replaces it.  Names beginning with
 * '@' are the model's own sections, resolved by the loader.  Register `&thing`,
 * never a number -- code symbols must carry the Thumb bit.
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

    /* The engine-facing description of the model: how big its input is, how it
     * is quantized, how much working buffer it needs.  Opaque here on purpose --
     * this layer knows it is a relocatable blob and nothing more; the caller
     * knows what struct it is.  Without it a model cannot be run at all by an
     * image that has no other model to borrow a descriptor from. */
    const uint8_t *desc;        /**< RELOC: descriptor bytes, UNRELOCATED    */
    size_t         desc_len;
    const uint8_t *labels;      /**< RELOC: pointer array, UNRELOCATED       */
    size_t         labels_len;
    uint32_t       nlabels;     /**< entries in @p labels                    */
    const char    *strings;     /**< RELOC: string pool, used as mapped      */
    size_t         strings_len;
    /** Bytes the model wants for its packed output, i.e. how big the buffer
     *  registered as \@packed_out must be.  0 if it does not use one. */
    uint32_t       packed_out_len;

    const uint8_t *sites;       /**< RELOC: site table (sect,sym,off)        */
    uint32_t       nsites;
    const char    *syms;        /**< RELOC: NUL-separated symbol names       */
    uint32_t       nsyms;
} tiku_model_t;

/*---------------------------------------------------------------------------*/
/* SECTIONS                                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief The sections a model can ask to have patched.
 *
 * Weights and strings are absent because they are used exactly as mapped -- a
 * section appears here only if something in it needs correcting.
 */
typedef enum {
    TIKU_MODEL_SECT_CMD    = 0,
    TIKU_MODEL_SECT_DESC   = 1,
    TIKU_MODEL_SECT_LABELS = 2,
    TIKU_MODEL_SECT_COUNT  = 3
} tiku_model_sect_t;

/** @brief Where one section should be built, and how much room is there. */
typedef struct {
    void  *dst;                 /**< NULL if the model has no such section   */
    size_t cap;
} tiku_model_dest_t;

/**
 * @brief Open a model by file name and validate it end to end.
 *
 * Checks every header field, section bound and CRC, and for RELOC proves each
 * site is 4-byte aligned inside the command buffer and names a symbol that
 * exists.  Nothing downstream re-checks, so a model that opens is safe to use.
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
 * RAW does nothing.  RELOC copies the command buffer into @p dst and applies
 * every relocation -- `word = resolved(symbol) + stored_addend`.  Weights are
 * not copied; they stay mapped in NVM and the engine reads them there.
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

/**
 * @brief Build EVERY relocatable section of a model, not just the commands.
 *
 * The general form of tiku_model_prepare(): @p dst is indexed by
 * tiku_model_sect_t and each non-NULL entry receives that section, relocated.
 * Sections cross-reference, so a model that cannot be fully built writes nothing.
 *
 * @param m        Model from tiku_model_open().
 * @param dst      Array of TIKU_MODEL_SECT_COUNT destinations.
 * @param bad_sym  Receives the unresolved symbol name on ERR_SYMBOL.  May be
 *                 NULL.
 * @return TIKU_MODEL_OK, or SPACE / SYMBOL / PARAM / FULL.
 */
int tiku_model_prepare_all(const tiku_model_t *m, tiku_model_dest_t *dst,
                           const char **bad_sym);

#endif /* TIKU_MODEL_H_ */
