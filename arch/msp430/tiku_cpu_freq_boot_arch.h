/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_arch.h - MSP430FR5969 CPU frequency configuration
 *
 * This file provides the core CPU frequency configuration functions
 * for the Tiku Operating System on the MSP430FR5969 microcontroller.
 * It includes clock system configuration, frequency setting, and
 * frequency getting functions.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_CPU_FREQ_BOOT_ARCH_H_

#include <tiku.h>
#include <stdint.h>
#include <stdbool.h>

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clock and CPU system status structure
 */
typedef struct {
    unsigned long fram_size;    /* FRAM size */
    unsigned long ram_size;     /* RAM size */
    unsigned long mclk_hz;      /* MCLK frequency */
    unsigned int smclk_div;     /* SMCLK divider */
    unsigned long aclk_hz;      /* ACLK frequency */
    bool lfxt_fault;            /* LFXT fault */
    bool hfxt_fault;            /* HFXT fault */
} clock_cpu_status_t;

/**
 * @brief ACLK source options
 */
typedef enum {
    TIKU_ACLK_VLO = 0,    /* Internal VLO (~10kHz, varies 4-20kHz) */
    TIKU_ACLK_REFO,       /* Internal REFO (32.768kHz) */
    TIKU_ACLK_LFXT,       /* External crystal on XT1 */
    TIKU_ACLK_DCO         /* DCO (same as MCLK) */
} tiku_aclk_source_t;

/**
 * @brief SMCLK source options
 */
typedef enum {
    TIKU_SMCLK_HFXT = 0,   /* External crystal */
    TIKU_SMCLK_REFO,       /* Internal REFO */
    TIKU_SMCLK_DCO,        /* DCO */
    TIKU_SMCLK_VLO         /* VLO */
} tiku_smclk_source_t;

/**
 * @brief Crystal oscillator modes
 */
typedef enum {
    TIKU_XT1_BYPASS = 0,  /* External clock input */
    TIKU_XT1_LF_XTAL,     /* Low frequency crystal (32.768kHz) */
    TIKU_XT1_HF_XTAL      /* High frequency crystal (4-24MHz) */
} tiku_crystal_mode_t;

/**
 * @brief DCO frequency range
 */
typedef enum {
    TIKU_DCO_RANGE_LOW = 0,   /* 1-8 MHz */
    TIKU_DCO_RANGE_HIGH = 1   /* 8-24 MHz */
} tiku_dco_range_t;

/**
 * @brief Clock validation result
 */
typedef enum {
    TIKU_CLOCK_OK = 0,
    TIKU_CLOCK_FAULT_XT1,
    TIKU_CLOCK_FAULT_LFXT,
    TIKU_CLOCK_FAULT_HFXT,
    TIKU_CLOCK_FAULT_TIMEOUT,
    TIKU_CLOCK_INVALID_FREQ
} tiku_clock_result_t;

/**
 * @brief Clock divider options
 */
typedef enum {
    TIKU_CLK_DIV_1 = 1,
    TIKU_CLK_DIV_2 = 2,
    TIKU_CLK_DIV_4 = 4,
    TIKU_CLK_DIV_8 = 8,
    TIKU_CLK_DIV_16 = 16,
    TIKU_CLK_DIV_32 = 32
} tiku_clk_div_t;

/*
* @brief Common clock frequencies
*/
typedef enum {  
    TIKU_CLK_FREQ_1MHZ = 1,
    TIKU_CLK_FREQ_2_677MHZ = 2,
    TIKU_CLK_FREQ_3_5MHZ = 3,
    TIKU_CLK_FREQ_4MHZ = 4,
    TIKU_CLK_FREQ_5_33MHZ = 5,
    TIKU_CLK_FREQ_7MHZ = 6,
    TIKU_CLK_FREQ_8MHZ = 7,
    TIKU_CLK_FREQ_16MHZ = 8
} tiku_clk_freq_t;


/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                */
/*---------------------------------------------------------------------------*/

/* Common clock frequencies */
#define CPU_FREQ_1MHZ        1
#define CPU_FREQ_2_677MHZ    2
#define CPU_FREQ_3_5MHZ      3
#define CPU_FREQ_4MHZ        4
#define CPU_FREQ_5_33MHZ     5
#define CPU_FREQ_7MHZ        6
#define CPU_FREQ_8MHZ        7
#define CPU_FREQ_16MHZ       8

/* Clock frequency limits */
#define CPU_FREQ_MIN_MHZ    1
#define CPU_FREQ_MAX_MHZ    16
#define CPU_FRAM_THRESH_MHZ 8    /* FRAM wait state threshold */

/* Crystal frequencies */
#define XT1_FREQ_32KHZ       32768UL
#define LFXT_FREQ_32KHZ      32768UL
#define LFXT_FREQ_4MHZ       4000000UL
#define HFXT_FREQ_8MHZ       8000000UL
#define HFXT_FREQ_12MHZ      12000000UL
#define HFXT_FREQ_16MHZ      16000000UL
#define HFXT_FREQ_24MHZ      24000000UL

/* Reference frequencies */
#define REFO_FREQ_HZ        32768UL
#define VLO_FREQ_NOMINAL_HZ 10000UL
#define VLO_FREQ_MIN_HZ     4000UL
#define VLO_FREQ_MAX_HZ     20000UL

/* Clock fault timeout (iterations) */
#define CLOCK_FAULT_TIMEOUT 50000UL

/*---------------------------------------------------------------------------*/
/* MSP430 HARDWARE REGISTER DEFINITIONS                                     */
/*---------------------------------------------------------------------------*/

#ifndef MSP430_XT1OFF
#define MSP430_XT1OFF        (0x0001)    /* XT1 oscillator off */
#endif

#ifndef MSP430_XT1BYPASS
#define MSP430_XT1BYPASS     (0x1000)    /* XT1 bypass select */
#endif

#ifndef MSP430_XT1DRIVE_0
#define MSP430_XT1DRIVE_0    (0x0000)    /* XT1 drive strength: lowest */
#endif

#ifndef MSP430_XT1DRIVE_1
#define MSP430_XT1DRIVE_1    (0x0040)    /* XT1 drive strength: low */
#endif

#ifndef MSP430_XT1DRIVE_2
#define MSP430_XT1DRIVE_2    (0x0080)    /* XT1 drive strength: high */
#endif

#ifndef MSP430_XT1DRIVE_3
#define MSP430_XT1DRIVE_3    (0x00C0)    /* XT1 drive strength: highest */
#endif

#ifndef MSP430_XT1HFFREQ_1
#define MSP430_XT1HFFREQ_1   (0x0004)    /* XT1 HF freq range: 1-4MHz */
#endif

#ifndef MSP430_XT1HFFREQ_2
#define MSP430_XT1HFFREQ_2   (0x0008)    /* XT1 HF freq range: 4-8MHz */
#endif

#ifndef MSP430_XT1HFFREQ_3
#define MSP430_XT1HFFREQ_3   (0x000C)    /* XT1 HF freq range: 8-24MHz */
#endif

#ifndef MSP430_SELA__REFOCLK
#define MSP430_SELA__REFOCLK (0x0200)   /* ACLK source select: REFOCLK */
#endif

#ifndef MSP430_SELA__VLOCLK
#define MSP430_SELA__VLOCLK  (0x0100)   /* ACLK source select: VLOCLK */
#endif

#ifndef MSP430_SELA__XT1CLK
#define MSP430_SELA__XT1CLK  (0x0000)   /* ACLK source select: XT1CLK */
#endif

#ifndef MSP430_SELA__DCOCLK
#define MSP430_SELA__DCOCLK  (0x0300)   /* ACLK source select: DCOCLK */
#endif

#ifndef MSP430_SELS__DCOCLK
#define MSP430_SELS__DCOCLK  (0x0030)   /* SMCLK source select: DCOCLK */
#endif

#ifndef MSP430_SELM__DCOCLK
#define MSP430_SELM__DCOCLK  (0x0003)   /* MCLK source select: DCOCLK */
#endif

#ifndef MSP430_DIVS__1
#define MSP430_DIVS__1       (0x0000)    /* SMCLK source divider: /1 */
#endif

#ifndef MSP430_DIVS__2
#define MSP430_DIVS__2       (0x0010)    /* SMCLK source divider: /2 */
#endif

#ifndef MSP430_DIVS__4
#define MSP430_DIVS__4       (0x0020)    /* SMCLK source divider: /4 */
#endif

#ifndef MSP430_DIVS__8
#define MSP430_DIVS__8       (0x0030)    /* SMCLK source divider: /8 */
#endif

#ifndef MSP430_DIVS__16
#define MSP430_DIVS__16      (0x0040)    /* SMCLK source divider: /16 */
#endif

#ifndef MSP430_DIVS__32
#define MSP430_DIVS__32      (0x0050)    /* SMCLK source divider: /32 */
#endif

#ifndef MSP430_DIVA__1
#define MSP430_DIVA__1       (0x0000)    /* ACLK source divider: /1 */
#endif

#ifndef MSP430_DIVM__1
#define MSP430_DIVM__1       (0x0000)    /* MCLK source divider: /1 */
#endif

#ifndef MSP430_FRCTLPW
#define MSP430_FRCTLPW       (0xA500)    /* FRAM control password */
#endif

#ifndef MSP430_NWAITS_0
#define MSP430_NWAITS_0      (0x0000)    /* FRAM wait states: 0 */
#endif

#ifndef MSP430_NWAITS_1
#define MSP430_NWAITS_1      (0x0010)    /* FRAM wait states: 1 */
#endif

#ifndef REFON
#define REFON               (0x0001)    /* Reference on */
#endif

/* DCOFSEL field is at bits 1-3 of CSCTL1 register.
 * These are fallback defines - TI msp430.h should provide correct values.
 * Values: DCOFSEL field value << 1 (shifted to bit position 1)
 */
#ifndef DCOFSEL_0
#define DCOFSEL_0           (0x0000)    /* DCO frequency select 0: DCORSEL=0: 1MHz */
#endif

#ifndef DCOFSEL_1
#define DCOFSEL_1           (0x0002)    /* DCO frequency select 1: DCORSEL=0: 2.67MHz, DCORSEL=1: 5.33MHz */
#endif

#ifndef DCOFSEL_2
#define DCOFSEL_2           (0x0004)    /* DCO frequency select 2: DCORSEL=0: 3.5MHz, DCORSEL=1: 7MHz */
#endif

#ifndef DCOFSEL_3
#define DCOFSEL_3           (0x0006)    /* DCO frequency select 3: DCORSEL=0: 4MHz, DCORSEL=1: 8MHz */
#endif

#ifndef DCOFSEL_4
#define DCOFSEL_4           (0x0008)    /* DCO frequency select 4: DCORSEL=0: 5.33MHz, DCORSEL=1: 16MHz */
#endif

#ifndef DCOFSEL_5
#define DCOFSEL_5           (0x000A)    /* DCO frequency select 5: DCORSEL=0: 6.67MHz, DCORSEL=1: 21MHz */
#endif

#ifndef DCOFSEL_6
#define DCOFSEL_6           (0x000C)    /* DCO frequency select 6: DCORSEL=0: 8MHz, DCORSEL=1: 24MHz */
#endif

#ifndef DCORSEL
#define DCORSEL             (0x0040)    /* DCO range select */
#endif

/*---------------------------------------------------------------------------*/
/* EXTERNAL VARIABLES - Clock frequency cache                               */
/*---------------------------------------------------------------------------*/

/* Cache current clock frequencies for fast access */
extern volatile unsigned long g_mclk_hz;         /* MCLK frequency in Hz */
extern volatile unsigned long g_smclk_hz;        /* SMCLK frequency in Hz */
extern volatile unsigned long g_aclk_hz;         /* ACLK frequency in Hz */
extern volatile unsigned long g_vlo_hz;          /* Measured VLO frequency */
extern volatile unsigned long g_xt1_hz;          /* Crystal frequency if enabled */
extern volatile bool g_xt1_enabled;              /* XT1 oscillator status */
extern volatile bool g_clock_initialized;        /* Initialization status */

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Sets the CPU clocks with advanced options
 * @param mclk The desired MCLK frequency (not indexed as MHz)
 * @param smclk_div The divider for SMCLK
 * @param aclk_source The source for ACLK
 */
void tiku_cpu_msp430_clock_set_advanced(unsigned int mclk,
                                        unsigned int smclk_div,
                                        tiku_aclk_source_t aclk_source);

/*---------------------------------------------------------------------------*/
/* OSCILLATOR FAULT HANDLING                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initializes the LFXT crystal oscillator
 * @param bypass If true, LFXT will be bypassed and external clock used
 * @return Clock initialization result
 */
tiku_clock_result_t tiku_cpu_msp430_lfxt_init(bool bypass);

/**
 * @brief Disables the LFXT oscillator
 */
void tiku_cpu_msp430_lfxt_disable(void);

/*---------------------------------------------------------------------------*/
/* CLOCK FAULT HANDLING                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Checks if any clock fault is present
 * @return true if fault present, false otherwise
 */
bool tiku_cpu_msp430_clock_has_fault(void);

/**
 * @brief Clears all clock fault flags
 */
void tiku_cpu_msp430_clock_clear_faults(void);

/**
 * @brief Sets a custom clock fault handler
 * @param handler Function pointer to fault handler
 */
void tiku_cpu_msp430_clock_set_fault_handler(void (*handler)(unsigned int fault_type));

/*---------------------------------------------------------------------------*/
/* CLOCK FREQUENCY GETTERS                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Gets the current Main Clock (MCLK) frequency in Hz
 * @return MCLK frequency in Hz
 */
unsigned long tiku_cpu_msp430_clock_get_hz(void);

/**
 * @brief Gets the current Auxiliary Clock (ACLK) frequency in Hz
 * @return ACLK frequency in Hz
 */
unsigned long tiku_cpu_msp430_aclk_get_hz(void);

/**
 * @brief Gets the current Sub-Main Clock (SMCLK) frequency in Hz
 * @return SMCLK frequency in Hz
 */
unsigned long tiku_cpu_msp430_smclk_get_hz(void);

/*---------------------------------------------------------------------------*/
/* UTILITY FUNCTIONS                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Delays for specified number of CPU cycles
 * @param cycles Number of cycles to delay
 */
void tiku_cpu_msp430_delay_cycles(unsigned long cycles);

/**
 * @brief Converts CPU frequency enum to actual MHz value for display
 * @param freq_enum The frequency enum value (e.g., CPU_FREQ_7MHZ)
 * @return Actual frequency string (e.g., "7")
 */
const char* tiku_cpu_freq_to_mhz_str(unsigned int freq_enum);

/*---------------------------------------------------------------------------*/
/* CPU BOOT AND POWER MANAGEMENT FUNCTIONS                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief MSP430-specific CPU initialization
 */
void tiku_cpu_boot_msp430_setup(void);

/**
 * @brief Initialize CPU frequency on MSP430
 * @param freq_mhz Desired frequency in MHz
 */
void tiku_cpu_freq_msp430_init(unsigned int freq_mhz);

/**
 * @brief Initializes all GPIO pins to a default state
 */
void tiku_cpu_boot_msp430_pins_init_low(void);

/**
 * @brief Enters Low Power Mode 0
 */
void tiku_cpu_boot_msp430_power_lpm0_enter(void);

/**
 * @brief Enters Low Power Mode 3
 */
void tiku_cpu_boot_msp430_power_lpm3_enter(void);

/**
 * @brief Enters Low Power Mode 4
 */
void tiku_cpu_boot_msp430_power_lpm4_enter(void);

/**
 * @brief Enables global interrupts
 */
void tiku_cpu_boot_msp430_global_interrupts_enable(void);

/**
 * @brief Disables global interrupts
 */
void tiku_cpu_boot_msp430_global_interrupts_disable(void);

/**
 * @brief Performs a software triggered reset of the device
 */
void tiku_cpu_boot_msp430_reset(void);

/**
 * @brief Initialize CPU boot sequence (GPIO pins, LOCKLPM5, interrupts)
 */
void tiku_cpu_boot_msp430_init(void);

#endif /* TIKU_CPU_FREQ_BOOT_ARCH_H_ */
