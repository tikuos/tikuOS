/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_module.h - runtime-loadable native module ABI.
 *
 * A module is compiled separately at a fixed address and cannot link against
 * firmware symbols, so it reaches every service through a jump table passed to
 * its entry point.  Included by both the firmware and the module build.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BASIC_MODULE_H_
#define TIKU_BASIC_MODULE_H_

#include <stddef.h>
#include <stdint.h>
#include "tiku_basic_ext.h"      /* the handler typedefs the table exposes */

/** First word of a module image: 'TMOD' little-endian. */
#define TIKU_MODULE_MAGIC    0x444F4D54u
/** Version of the image header and the service table; others are refused. */
#define TIKU_MODULE_ABI      1u

#include <hal/tiku_module_layout.h>

#if (TIKU_BASIC_MODULE_ENABLE + 0) && \
    !defined(TIKU_MODULE_CARVE_ADDR) && !TIKU_MODULE_EXEC_IN_RAM
#error "no module slot defined for this platform"
#endif

/**
 * @brief The /data file holding the module image.
 *
 * Install takes the image from this file, first writing the embedded copy
 * there when the file is absent (TIKU_BASIC_MODULE_EMBED).  Parts with a RAM
 * window copy the image from this file at every activate.
 */
#define TIKU_MODULE_FILE  "mod.bin"

/**
 * @brief 1 to embed a module image in the firmware as the seed for
 *        TIKU_MODULE_FILE; with 0, install needs the file already in /data.
 */
#ifndef TIKU_BASIC_MODULE_EMBED
#define TIKU_BASIC_MODULE_EMBED  1
#endif

/**
 * @brief Encode a module entry offset for tiku_module_header_t.init_off.
 *
 * On ARM the Thumb bit (bit 0) is set so the loader can branch to
 * TIKU_MODULE_EXEC_ADDR + init_off directly; MSP430 and RISC-V use the plain
 * even offset.  One module source builds for every CPU through this macro.
 */
#if defined(__MSP430__) || defined(__riscv)
#define TIKU_MODULE_INIT_OFF(off)  (off)
#else
#define TIKU_MODULE_INIT_OFF(off)  ((off) | 1u)
#endif

/**
 * @brief Header at the start of every module image.
 *
 * The loader runs a module only when magic and abi_version match and init_off
 * lands past the header, inside the slot (or the copied image), with the
 * CPU's entry convention.
 */
typedef struct {
    uint32_t magic;          /**< TIKU_MODULE_MAGIC                        */
    uint32_t abi_version;    /**< TIKU_MODULE_ABI                          */
    uint32_t init_off;       /**< TIKU_MODULE_INIT_OFF(entry offset)       */
    uint32_t reserved;       /**< 0; the loader does not read it           */
} tiku_module_header_t;

/**
 * @brief The firmware services a module may call, passed to its entry point.
 *
 * Each entry is the tiku_basic_ext.h service of the same name: register_fn is
 * tiku_basic_register_fn(), parse_expr is tiku_basic_ext_parse_expr(), and so
 * on.
 */
typedef struct {
    uint32_t abi_version;    /**< TIKU_MODULE_ABI */
    int  (*register_fn)(const char *name, uint8_t arity,
                        tiku_basic_ext_nfn fn);
    int  (*register_strfn)(const char *name, tiku_basic_ext_strfn fn);
    int  (*register_stmt)(const char *name, tiku_basic_ext_stmt_fn fn);
    int  (*parse_expr)(const char **p, long *out);
    int  (*parse_strexpr)(const char **p, char *buf, size_t cap);
    void (*print)(const char *s);
    void (*error)(int cat, const char *msg);
    int  (*expect)(const char **p, char ch);
} tiku_basic_syscalls_t;

/** @brief Module entry point: the module defines it, the loader calls it. */
typedef void (*tiku_module_init_fn)(const tiku_basic_syscalls_t *sys);

/* --- Firmware-side loader (not seen by the module build) --- */
#ifndef TIKU_MODULE_BUILD

/**
 * @brief Install the module image into the part's slot and activate it.
 *
 * The image comes from TIKU_MODULE_FILE, which the embedded copy seeds when
 * the file is absent.  Parts that run modules from RAM have no slot to write,
 * so they go straight to activate.
 *
 * @return 0 installed and activated; -1 no image, bad header, a slot that
 *         disagrees with the link, a failed install, or the feature off.
 */
int tiku_basic_module_load(void);

/**
 * @brief Activate the installed module: validate its header and run its init,
 *        which registers its BASIC words.
 *
 * Parts with a RAM window first copy the image from TIKU_MODULE_FILE into it;
 * activate never seeds the file from the embedded copy.
 *
 * @note May be called at every boot: with no valid module it returns -1.
 * @return 0 activated, -1 no valid module or the feature off.
 */
int tiku_basic_module_activate(void);

/** @brief 1 once a module has been activated this boot. */
int tiku_basic_module_loaded(void);

#endif /* TIKU_MODULE_BUILD */

#endif /* TIKU_BASIC_MODULE_H_ */
