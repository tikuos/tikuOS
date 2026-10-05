/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region.h - the board's carved, memory-mapped NVM region.
 *
 * A carved span read through be->base and written through be->write in an
 * unlock window.  The NVM tier and /data use it on every port but MSP430,
 * which keeps separate arrays.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NVM_REGION_H_
#define TIKU_NVM_REGION_H_

#include "kernel/fs/tiku_nvm_backend.h"

/**
 * @brief Default NVM tier size, taken from the front of the carved region.
 *
 * The layout service's nvm.tier knob overrides it; /data takes the rest.  On
 * MSP430 the tier and /data are separate FRAM arrays.  Tier memory has no
 * recovery identity, so named durable data belongs in a file or a cell.
 */
#define TIKU_NVM_TIER_BYTES  (32u * 1024u)

/*
 * Expected size of the carved region, per part, as the device scripts carve
 * it through arch/common/tiku_nvm_layout.ld:
 *
 *     region = layout_top - layout_code_cap - layout_persist_size
 *
 * No code reads these figures: the NVM tier and /data split the size the
 * backend reports (tiku_nvm_backend_get()->size), which `df` prints.  Keep
 * them in step with the device scripts.
 *
 * On these parts the code window is 0x60000 (384 KB) and bounds the image, so
 * model weights and radio firmware go in store files, not .rodata.  Where a
 * part has a loadable-module slot, it is the window's top 32 KB in loader
 * builds, so it appears in none of the arithmetic here.
 *
 *   apollo510  0x800000 - 0x470000 - 0x4000 = 0x38C000  3632 KB
 *   apollo4l/p 0x200000 - 0x078000 - 0x4000 = 0x184000  1552 KB
 *   rp2350     0x400000 - 0x060000 - 0x01000 = 0x39F000  3708 KB
 *   lm20       0x1FD000 - 0x060000 - 0x04000 = 0x199000  1636 KB
 *   l15        0x17D000 - 0x060000 - 0x04000 = 0x119000  1124 KB
 *   ra8p1      0x100000 - 0x060000 - 0x04000 = 0x09C000   624 KB
 */
/** @brief Expected carved-region size (documentation); 0 where not listed. */
#if defined(AM_PART_APOLLO510)
#define TIKU_NVM_REGION_BYTES  (3632u * 1024u)
#elif defined(PLATFORM_AMBIQ)
#define TIKU_NVM_REGION_BYTES  (1552u * 1024u)
#elif defined(PLATFORM_RP2350)
#define TIKU_NVM_REGION_BYTES  (3708u * 1024u)
#elif defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
#define TIKU_NVM_REGION_BYTES  (1636u * 1024u)
#elif defined(PLATFORM_NORDIC)
#define TIKU_NVM_REGION_BYTES  (1124u * 1024u)
#elif defined(PLATFORM_RA8P1)
#define TIKU_NVM_REGION_BYTES  (624u * 1024u)
#elif defined(PLATFORM_STM32N6)
/* Not linker-carved: the region is a span of the external NOR, so this
 * follows TIKU_XSPI_REGION_BYTES in arch/stm32n6/tiku_xspi_arch.h.  8 MB is
 * what TIKU_TFS_MAX_SLOTS can address at a 4 KB slot. */
#define TIKU_NVM_REGION_BYTES  (8192u * 1024u)
#elif defined(PLATFORM_ESP32C61)
/* Not linker-carved: the region is a span of the external flash, so this
 * follows TIKU_FLASH_REGION_BYTES in arch/esp32c61/tiku_flash_arch.h. */
#define TIKU_NVM_REGION_BYTES  (5120u * 1024u)
#else
#define TIKU_NVM_REGION_BYTES  0u
#endif

/*
 * Every target carves a region except MSP430, whose FRAM is unified with the
 * code estate, and host builds, which have no NVM; a new port gets 1.
 * Consumers also call tiku_nvm_backend_get() at run time: with no backend the
 * NVM tier is absent and /data does not mount.
 */
/**
 * @brief 1 on parts with a carved NVM region, 0 otherwise.
 *
 * Defined on every target once this header is included.  TIKU_NVM_REGION_BYTES
 * is 0 on a port its table does not list, so it does not say whether a region
 * exists.
 *
 * @note An undefined name reads as 0 in #if, so a unit that could miss the
 *       include checks #ifndef first (as tiku_basic_config.h does).
 */
#if defined(PLATFORM_MSP430) || defined(TIKU_TEST_HOST)
#define TIKU_NVM_HAS_REGION  0
#else
#define TIKU_NVM_HAS_REGION  1
#endif

/*
 * Expected file-store extent: the region less the default tier.
 *
 *   apollo510  3632 - 32 = 3600 KB      lm20     1636 - 32 = 1604 KB
 *   apollo4l/p 1552 - 32 = 1520 KB      l15      1124 - 32 = 1092 KB
 *   rp2350     3708 - 32 = 3676 KB      ra8p1     624 - 32 =  592 KB
 *   stm32n6    8192 - 32 = 8160 KB      esp32c61 5120 - 32 = 5088 KB
 *
 * No code reads it: /data takes the carved size less the layout service's
 * tier, and the store derives its slot count from that at mount.
 */
/** @brief Expected file-store extent (documentation); 0 without a region. */
#if TIKU_NVM_HAS_REGION
#define TIKU_NVMFS_FS_BYTES  (TIKU_NVM_REGION_BYTES - TIKU_NVM_TIER_BYTES)
#else
#define TIKU_NVMFS_FS_BYTES  0u
#endif

/**
 * @brief Return the board's carved NVM region backend, or NULL if none.
 *
 * The backend is a static object of the arch layer.  Reads use be->base
 * directly; writes go through be->write inside an NVM unlock window.
 *
 * @return Pointer to the region backend, or NULL on parts without one.
 */
const tiku_nvm_backend_t *tiku_nvm_backend_get(void);

#endif /* TIKU_NVM_REGION_H_ */
