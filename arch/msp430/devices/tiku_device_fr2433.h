/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_device_fr2433.h - MSP430FR2433 silicon-level constants.
 *
 * GPIO ports, clock-system type, memory sizes and peripheral availability.
 * The part has three ports, a CS module without a key and a DCO set by range
 * and FLL; this port starts no crystal on it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DEVICE_FR2433_H_
#define TIKU_DEVICE_FR2433_H_

/*---------------------------------------------------------------------------*/
/* DEVICE IDENTIFICATION                                                     */
/*---------------------------------------------------------------------------*/

/** Part name, reported by the shell and /sys. */
#define TIKU_DEVICE_NAME            "MSP430FR2433"
#define TIKU_DEVICE_NVM_LABEL       "FRAM"   /**< NVM technology (UI label). */

/*---------------------------------------------------------------------------*/
/* GPIO PORT AVAILABILITY                                                    */
/*---------------------------------------------------------------------------*/

/** @name 1 for each GPIO port the part has, 0 for each it lacks
 * @{ */
#define TIKU_DEVICE_HAS_PORT1       1
#define TIKU_DEVICE_HAS_PORT2       1
#define TIKU_DEVICE_HAS_PORT3       1
#define TIKU_DEVICE_HAS_PORT4       0
#define TIKU_DEVICE_HAS_PORT5       0
#define TIKU_DEVICE_HAS_PORT6       0
#define TIKU_DEVICE_HAS_PORT7       0
#define TIKU_DEVICE_HAS_PORT8       0
#define TIKU_DEVICE_HAS_PORT9       0
#define TIKU_DEVICE_HAS_PORTJ       0
/** @} */

/*---------------------------------------------------------------------------*/
/* CRYSTAL OSCILLATOR AVAILABILITY                                           */
/*---------------------------------------------------------------------------*/

/** @name 0: no LFXT or HFXT start-up code is built for this part
 * @{ */
#define TIKU_DEVICE_HAS_LFXT        0
#define TIKU_DEVICE_HAS_HFXT        0
/** @} */

/*---------------------------------------------------------------------------*/
/* CLOCK SYSTEM TYPE                                                         */
/*---------------------------------------------------------------------------*/

/*
 * The FR2433 clock system has no CS password, sets the DCO through DCORSEL
 * and the FLL, gives MCLK and SMCLK one source select (SELMS), has a one-bit
 * ACLK select, and keeps its dividers in CSCTL5.
 */
#define TIKU_DEVICE_CS_HAS_KEY      0   /**< CS registers have no key */
#define TIKU_DEVICE_CS_TYPE_FR2X33  1   /**< Selects the FR2xx CS code */

/*---------------------------------------------------------------------------*/
/* CLOCK CAPABILITIES                                                        */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_MAX_STABLE_MHZ  16  /**< Maximum stable MCLK, in MHz */

/*---------------------------------------------------------------------------*/
/* MEMORY SIZES                                                              */
/*---------------------------------------------------------------------------*/

/** 16 KB FRAM, 15.5 KB of it usable. */
#define TIKU_DEVICE_FRAM_SIZE       (16 * 1024UL)
#define TIKU_DEVICE_RAM_SIZE        (4 * 1024UL)    /**< 4 KB SRAM */
#define TIKU_DEVICE_RAM_START       0x2000U         /**< First byte of SRAM */

/*---------------------------------------------------------------------------*/
/* FRAM REGION SIZING (used by kernel/memory/tiku_nvm_map)                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bytes of the config region, which holds the init table.
 *
 * kernel/memory/tiku_nvm_map.c declares the region at this size and the
 * linker places it; services/init/tiku_init.c asserts the init table fits.
 */
#define TIKU_DEVICE_FRAM_CONFIG_SIZE      512U    /* Init table */

/** @name Loadable app slot geometry; no code in the tree allocates slots
 * @{ */
#define TIKU_DEVICE_FRAM_APP_SLOT_SIZE    2048U   /**< 2 KB per app slot */
#define TIKU_DEVICE_FRAM_APP_SLOT_COUNT   2       /**< 2 slots */
/** @} */

/*---------------------------------------------------------------------------*/
/* FRAM ADDRESS RANGE                                                        */
/*---------------------------------------------------------------------------*/

#define TIKU_DEVICE_FRAM_START      0xC400U  /**< First byte of main FRAM */
#define TIKU_DEVICE_FRAM_END        0xFFFFU  /**< Last byte of main FRAM */

/*---------------------------------------------------------------------------*/
/* MPU (MEMORY PROTECTION UNIT)                                              */
/*---------------------------------------------------------------------------*/

/** 0: no MPU; the tiku_mpu_arch_* calls do nothing on this part. */
#define TIKU_DEVICE_HAS_MPU         0

/*---------------------------------------------------------------------------*/
/* FR2433 DCO RANGE SELECT VALUES                                            */
/*---------------------------------------------------------------------------*/

/** @name DCORSEL_6 and DCORSEL_7, for toolchain headers that lack them
 * @{ */
#ifndef DCORSEL_6
#define DCORSEL_6               (0x000C)    /**< DCO range: 20 MHz (SLAU445) */
#endif

#ifndef DCORSEL_7
#define DCORSEL_7               (0x000E)    /**< DCO range: 24 MHz (SLAU445) */
#endif
/** @} */

#endif /* TIKU_DEVICE_FR2433_H_ */
