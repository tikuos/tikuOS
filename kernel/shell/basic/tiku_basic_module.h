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

/*
 * Where each part keeps and runs a module.  A module is linked for one
 * absolute address, so the address below, the ORIGIN in the module's linker
 * script (modules/mod_demo*.ld) and the device linker script must agree.
 *
 *   nRF54L15, nRF54LM20  RRAM slot 0x58000, run in place; one address for
 *                        both parts, so one image serves both.  Installed by
 *                        CPU stores behind the WEN gate.  SRAM is
 *                        execute-never on these parts.
 *   Apollo4 Lite/Plus    MRAM slot 0x70000, run in place; installed through
 *                        the bootrom programmer.
 *   RP2350               flash slot 0x10058000 (eight 4 KB erase sectors),
 *                        run in place; installed through the boot-ROM path.
 *   MSP430FR5994/FR6989  0xFF0-byte FRAM slot at 0x43000 / 0x23000, the top
 *                        of HIFRAM, written and run in place (the HIFRAM MPU
 *                        segment is R+W+X).
 *   Apollo510/510B       no slot: copied from the store file into an ITCM
 *                        window at 0x1000 at every activate.  The ITCM is
 *                        powered by the reset default of PWRCTRL
 *                        MEMPWREN.PWRENTCM, which nothing programs.
 *   ESP32-C61            no slot: copied from the store file into a 32 KB
 *                        window at the PSRAM base at every activate.
 *
 * On the ARM parts with a slot it is the top 32 KB of the code window,
 * reserved only in builds with the loader (the Makefile passes
 * --defsym=__tiku_module_reserve and the link ASSERT keeps the firmware below
 * it).  MSP430 holds its slot back in the device linker script.
 *
 * Only Apollo510 and the ESP32-C61 run a module from RAM, from memory no other
 * code needs: the ITCM, and 32 KB the PSRAM tier leaves out.  SRAM is
 * execute-never on the Nordic parts and RP2350, and Apollo4's one TCM holds
 * .data, .bss and the stack, so the other parts run in place from NVM.
 */

/**
 * @brief Base of the NVM module slot; not defined where a module runs from a
 *        RAM window.
 *
 * Must equal the module script's ORIGIN and, on the ARM parts, the device
 * script's code cap less 32 KB.
 */
#if defined(AM_PART_APOLLO510)
/* Not defined on this part: the MRAM above the code window is the NVM region
 * (tier and /data), which a stale slot address would program over.  Leaving
 * the macro undefined makes any such use a compile error. */
#elif defined(AM_PART_APOLLO4L)
#define TIKU_MODULE_CARVE_ADDR  0x70000u
#elif defined(PLATFORM_RP2350)
#define TIKU_MODULE_CARVE_ADDR  0x10058000u
#elif defined(TIKU_DEVICE_MSP430FR5994) || defined(__MSP430FR5994__)
/* The slot ends at 0x43FF0, short of the stock region's odd 0x43FF7 end
 * (CPU47). */
#define TIKU_MODULE_CARVE_ADDR  0x43000u
#define TIKU_MODULE_CARVE_SIZE  0xFF0u
#elif defined(TIKU_DEVICE_MSP430FR6989) || defined(__MSP430FR6989__)
#define TIKU_MODULE_CARVE_ADDR  0x23000u
#define TIKU_MODULE_CARVE_SIZE  0xFF0u
#elif defined(PLATFORM_ESP32C61)
/* Not defined, as on Apollo510: the module runs from a PSRAM window. */
#elif defined(PLATFORM_NORDIC)
#define TIKU_MODULE_CARVE_ADDR  0x58000u
#else
/* Not defaulted: a part without its own branch must not inherit another
 * part's slot address.  A build with the loader stops here until the port
 * chooses one (its code cap less 32 KB). */
#if defined(TIKU_BASIC_MODULE_ENABLE) && TIKU_BASIC_MODULE_ENABLE
#error "no Tier-3 module slot defined for this platform"
#endif
#endif
/** Bytes in the NVM slot, or in the RAM window where the module is copied. */
#ifndef TIKU_MODULE_CARVE_SIZE
#define TIKU_MODULE_CARVE_SIZE  0x8000u
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
 * @brief Where a module runs: TIKU_MODULE_EXEC_IN_RAM is 1 where the loader
 *        copies it into a RAM window, 0 where it runs in place from its slot.
 *
 * TIKU_MODULE_EXEC_ADDR is the image base the module is linked for.  The ITCM
 * window starts 4 KB in from the ITCM base, so no module address is zero.
 */
#if defined(AM_PART_APOLLO510)
#define TIKU_MODULE_EXEC_IN_RAM  1
#define TIKU_MODULE_EXEC_ADDR    0x00001000u   /**< ITCM base + 4 KB */
#elif defined(PLATFORM_ESP32C61)
#define TIKU_MODULE_EXEC_IN_RAM  1
#define TIKU_MODULE_EXEC_ADDR    0x42800000u   /**< PSRAM base */
#else
#define TIKU_MODULE_EXEC_IN_RAM  0
#define TIKU_MODULE_EXEC_ADDR    TIKU_MODULE_CARVE_ADDR
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
 * @note Safe to call at every boot: without a valid module it returns -1.
 * @return 0 activated, -1 no valid module or the feature off.
 */
int tiku_basic_module_activate(void);

/** @brief 1 once a module has been activated this boot. */
int tiku_basic_module_loaded(void);

#endif /* TIKU_MODULE_BUILD */

#endif /* TIKU_BASIC_MODULE_H_ */
