/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_model.h - load a model from the file store.
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

/** @brief Status of the tiku_model_* calls (0 = success, negative = error). */
typedef enum {
    TIKU_MODEL_OK          =  0,
    TIKU_MODEL_ERR_PARAM   = -1,  /**< NULL argument, not open, or no @c dst */
    TIKU_MODEL_ERR_NOENT   = -2,  /**< no such file in the store             */
    TIKU_MODEL_ERR_FORMAT  = -3,  /**< header or geometry inconsistent       */
    TIKU_MODEL_ERR_CRC     = -4,  /**< contents do not match their checksum  */
    TIKU_MODEL_ERR_SPACE   = -5,  /**< caller's buffer too small             */
    TIKU_MODEL_ERR_SYMBOL  = -6,  /**< a named symbol is not registered      */
    TIKU_MODEL_ERR_FULL    = -7   /**< registry full, or too many symbols    */
} tiku_model_err_t;

/** @brief Human-readable form of a tiku_model_* status. */
const char *tiku_model_strerror(int status);

/*---------------------------------------------------------------------------*/
/* FORMATS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Model file formats, told apart by the RELOC magic. */
typedef enum {
    /** Opaque bytes addressed as base+offset, such as quantized weights, a
     *  CMSIS-NN model or a palette; nothing is patched.  Any non-empty file
     *  without the RELOC magic is RAW. */
    TIKU_MODEL_FMT_RAW   = 0,
    /** Carries a relocation table (tools/axonpack.py output, magic 'AXM1'). */
    TIKU_MODEL_FMT_RELOC = 1
} tiku_model_fmt_t;

/*---------------------------------------------------------------------------*/
/* SYMBOL REGISTRY                                                           */
/*---------------------------------------------------------------------------*/

/** @brief Registry slots, and the most symbols one model may name.  Each slot
 *  holds a TIKU_MODEL_SYM_NAME_MAX-byte name and an address. */
#ifndef TIKU_MODEL_SYM_MAX
#define TIKU_MODEL_SYM_MAX  8
#endif

/** @brief Longest symbol name the registry stores, including the NUL. */
#ifndef TIKU_MODEL_SYM_NAME_MAX
#define TIKU_MODEL_SYM_NAME_MAX  48
#endif

/**
 * @brief Publish an address a model's relocation table may name.
 *
 * Re-registering a name replaces it.  The loader resolves the model's own
 * sections (`@weights`, `@cmd`, `@desc`, `@labels`, `@strings`) before it
 * looks in the registry.
 *
 * @param name  NUL-terminated symbol name as the packer recorded it.
 * @param addr  Runtime address the name resolves to, Thumb-tagged for code.
 * @return TIKU_MODEL_OK, or ERR_PARAM / ERR_FULL.
 * @note Pass an address taken with & (`&thing`): a code symbol's address
 *       carries the Thumb bit, which a hand-written number may lack.
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
 * @brief A model mapped in place, its structure validated by
 *        tiku_model_open().
 *
 * Every pointer here aims into the memory-mapped store; nothing is copied.
 *
 * @note The pointers follow tiku_tfs_map()'s lifetime rule: valid until the
 *       next write or delete of that file name.  Re-open after re-provisioning.
 */
typedef struct {
    const uint8_t *base;        /**< mapped file base                        */
    size_t         len;         /**< file length                             */
    uint8_t        fmt;         /**< tiku_model_fmt_t                        */

    /* RAW: payload is the whole file.  RELOC: the packed sections. */
    const uint8_t *weights;     /**< RAW: the file; RELOC: the weights blob  */
    size_t         weights_len;
    const uint8_t *cmd;         /**< RELOC: command buffer, unrelocated      */
    size_t         cmd_len;

    /* The engine-facing description of the model: input size, quantization,
     * working-buffer size.  Opaque to this layer, which only relocates it;
     * the engine that runs the model defines its struct. */
    const uint8_t *desc;        /**< RELOC: descriptor bytes, unrelocated    */
    size_t         desc_len;
    const uint8_t *labels;      /**< RELOC: pointer array, unrelocated       */
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
 * Weights and strings are used as mapped and never patched, so they have no
 * entry here.
 */
typedef enum {
    TIKU_MODEL_SECT_CMD    = 0,
    TIKU_MODEL_SECT_DESC   = 1,
    TIKU_MODEL_SECT_LABELS = 2,
    TIKU_MODEL_SECT_COUNT  = 3
} tiku_model_sect_t;

/** @brief Where one section is built, and the room there. */
typedef struct {
    void  *dst;                 /**< NULL if the model has no such section   */
    size_t cap;                 /**< bytes available at @c dst               */
} tiku_model_dest_t;

/**
 * @brief Open a model by file name and validate it end to end.
 *
 * For RELOC, checks every header field, section bound and CRC, and that each
 * site is a 4-byte aligned word inside the section it names (commands,
 * descriptor or labels) and names an entry of the symbol table.
 *
 * @param fs    Mounted store (tiku_vfs_tree_data_store()).
 * @param name  File name, e.g. "vww.axm".
 * @param out   Receives the mapped, validated model.
 * @return TIKU_MODEL_OK, or NOENT / FORMAT / CRC / PARAM.
 * @note tiku_model_prepare_all() relies on these checks and does not repeat
 *       them.
 */
int tiku_model_open(tiku_tfs_t *fs, const char *name, tiku_model_t *out);

/**
 * @brief Single-section form of tiku_model_prepare_all(): the commands only.
 *
 * A RAW model needs nothing and succeeds.  Every RELOC model carries a
 * descriptor, which this form gives no destination, so a RELOC model fails;
 * build it with tiku_model_prepare_all().
 *
 * @param m        Model from tiku_model_open().
 * @param dst      Destination for the patched command buffer.
 * @param cap      Bytes available at @p dst.
 * @param out_len  Receives the bytes written (0 for RAW).  May be NULL.
 * @param bad_sym  Passed to tiku_model_prepare_all().  May be NULL.
 * @return TIKU_MODEL_OK for RAW; PARAM, SPACE or FULL for RELOC.
 */
int tiku_model_prepare(const tiku_model_t *m, void *dst, size_t cap,
                       size_t *out_len, const char **bad_sym);

/**
 * @brief Build every relocatable section of a model.
 *
 * @p dst is indexed by tiku_model_sect_t; each section the model has needs an
 * entry, which receives a copy with every site patched to resolved(symbol) +
 * stored addend.  Weights and strings stay mapped in NVM; RAW needs nothing.
 *
 * @param m        Model from tiku_model_open().
 * @param dst      Array of TIKU_MODEL_SECT_COUNT destinations.
 * @param bad_sym  Receives the unresolved symbol name on ERR_SYMBOL.  May be
 *                 NULL.
 * @return TIKU_MODEL_OK, or SPACE / SYMBOL / PARAM / FULL / FORMAT.  On an
 *         error nothing is written: sections cross-reference, so it is all or
 *         none.
 */
int tiku_model_prepare_all(const tiku_model_t *m, tiku_model_dest_t *dst,
                           const char **bad_sym);

#endif /* TIKU_MODEL_H_ */
