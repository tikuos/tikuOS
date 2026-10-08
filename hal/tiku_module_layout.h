/*
 * TikuOS native-module storage and execution windows.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MODULE_LAYOUT_H_
#define TIKU_MODULE_LAYOUT_H_

/*
 * Where each part keeps and runs a module.  A module is linked for one
 * absolute address, so the address below, the ORIGIN in the module's linker
 * script (basic/modules/mod_demo*.ld) and the device linker script must agree.
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
#endif
/** Bytes in the NVM slot, or in the RAM window where the module is copied. */
#ifndef TIKU_MODULE_CARVE_SIZE
#define TIKU_MODULE_CARVE_SIZE  0x8000u
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


#endif /* TIKU_MODULE_LAYOUT_H_ */
