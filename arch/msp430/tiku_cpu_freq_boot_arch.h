/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.h - MSP430 CPU frequency configuration.
 *
 * Declares the clock-system setup and the frequency set and get entry points
 * that the portable CPU HAL forwards to on MSP430.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CPU_FREQ_BOOT_ARCH_H_
#define TIKU_CPU_FREQ_BOOT_ARCH_H_

#include <tiku.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Divide MCLK alone to 8, 4, 2 or 1 MHz, at boot or at run time.
 *
 * SMCLK and ACLK keep their rates.  Any other rate, or an SMCLK other than
 * 8 MHz, leaves the clocks unchanged.
 *
 * @param hz  Target MCLK in Hz
 */
void tiku_cpu_msp430_boot_divide(unsigned long hz);

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Clock and CPU system status structure
 */
typedef struct {
    unsigned long fram_size;    /**< FRAM size */
    unsigned long ram_size;     /**< RAM size */
    unsigned long mclk_hz;      /**< MCLK frequency */
    unsigned int smclk_div;     /**< SMCLK divider */
    unsigned long aclk_hz;      /**< ACLK frequency */
    bool lfxt_fault;            /**< LFXT fault */
    bool hfxt_fault;            /**< HFXT fault */
} clock_cpu_status_t;

/**
 * @brief ACLK source options
 */
typedef enum {
    TIKU_ACLK_VLO = 0,    /**< Internal VLO (~10kHz, varies 4-20kHz) */
    TIKU_ACLK_REFO,       /**< Internal REFO (32.768kHz) */
    TIKU_ACLK_LFXT,       /**< External crystal on XT1 */
    TIKU_ACLK_DCO         /**< DCO (same as MCLK) */
} tiku_aclk_source_t;

/**
 * @brief SMCLK source options
 */
typedef enum {
    TIKU_SMCLK_HFXT = 0,   /**< External crystal */
    TIKU_SMCLK_REFO,       /**< Internal REFO */
    TIKU_SMCLK_DCO,        /**< DCO */
    TIKU_SMCLK_VLO         /**< VLO */
} tiku_smclk_source_t;

/**
 * @brief Crystal oscillator modes
 */
typedef enum {
    TIKU_XT1_BYPASS = 0,  /**< External clock input */
    TIKU_XT1_LF_XTAL,     /**< Low frequency crystal (32.768kHz) */
    TIKU_XT1_HF_XTAL      /**< High frequency crystal (4-24MHz) */
} tiku_crystal_mode_t;

/**
 * @brief DCO frequency range
 */
typedef enum {
    TIKU_DCO_RANGE_LOW = 0,   /**< 1-8 MHz */
    TIKU_DCO_RANGE_HIGH = 1   /**< 8-24 MHz */
} tiku_dco_range_t;

/**
 * @brief Clock validation result
 */
typedef enum {
    TIKU_CLOCK_OK = 0,          /**< Oscillator running */
    TIKU_CLOCK_FAULT_XT1,       /**< LFXT (XT1) did not start */
    TIKU_CLOCK_FAULT_LFXT,      /**< Not returned by this port */
    TIKU_CLOCK_FAULT_HFXT,      /**< HFXT did not start */
    TIKU_CLOCK_FAULT_TIMEOUT,   /**< Not returned by this port */
    TIKU_CLOCK_INVALID_FREQ     /**< Not returned by this port */
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

/**
 * @brief MCLK frequency codes; the CPU_FREQ_* macros carry the same values.
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

/**
 * @name MCLK frequency codes, the argument of tiku_cpu_freq_msp430_init()
 * Codes, not MHz: CPU_FREQ_8MHZ is 7.  Codes above CPU_FREQ_8MHZ select 8 MHz.
 * @{
 */
#define CPU_FREQ_1MHZ        1
#define CPU_FREQ_2_677MHZ    2
#define CPU_FREQ_3_5MHZ      3
#define CPU_FREQ_4MHZ        4
#define CPU_FREQ_5_33MHZ     5
#define CPU_FREQ_7MHZ        6
#define CPU_FREQ_8MHZ        7
#define CPU_FREQ_16MHZ       8
/** @} */

/**
 * @name Clock frequency limits, in MHz
 * The clock code also compares CPU_FREQ_* codes against them.
 * @{
 */
#define CPU_FREQ_MIN_MHZ    1
#define CPU_FREQ_MAX_MHZ    16
#define CPU_FRAM_THRESH_MHZ 8    /**< Above this MCLK FRAM needs wait states */
/** @} */

/** @name Crystal frequencies, in Hz
 * @{ */
#define XT1_FREQ_32KHZ       32768UL
#define LFXT_FREQ_32KHZ      32768UL
#define LFXT_FREQ_4MHZ       4000000UL
#define HFXT_FREQ_8MHZ       8000000UL
#define HFXT_FREQ_12MHZ      12000000UL
#define HFXT_FREQ_16MHZ      16000000UL
#define HFXT_FREQ_24MHZ      24000000UL
/** @} */

/** @name Internal oscillator frequencies, in Hz (VLO: nominal and range)
 * @{ */
#define REFO_FREQ_HZ        32768UL
#define VLO_FREQ_NOMINAL_HZ 10000UL
#define VLO_FREQ_MIN_HZ     4000UL
#define VLO_FREQ_MAX_HZ     20000UL
/** @} */

/** Polls before crystal start-up or a crystal fault wait gives up. */
#define CLOCK_FAULT_TIMEOUT 50000UL

/*---------------------------------------------------------------------------*/
/* MSP430 HARDWARE REGISTER DEFINITIONS                                     */
/*---------------------------------------------------------------------------*/

/*
 * Clock-system bit values, each defined here unless something defined it
 * first; DCOFSEL_*, DCORSEL and REFON normally come from <msp430.h>.
 */

#ifndef MSP430_XT1OFF
#define MSP430_XT1OFF        (0x0001)    /**< XT1 oscillator off */
#endif

#ifndef MSP430_XT1BYPASS
#define MSP430_XT1BYPASS     (0x1000)    /**< XT1 bypass select */
#endif

#ifndef MSP430_XT1DRIVE_0
#define MSP430_XT1DRIVE_0    (0x0000)    /**< XT1 drive strength: lowest */
#endif

#ifndef MSP430_XT1DRIVE_1
#define MSP430_XT1DRIVE_1    (0x0040)    /**< XT1 drive strength: low */
#endif

#ifndef MSP430_XT1DRIVE_2
#define MSP430_XT1DRIVE_2    (0x0080)    /**< XT1 drive strength: high */
#endif

#ifndef MSP430_XT1DRIVE_3
#define MSP430_XT1DRIVE_3    (0x00C0)    /**< XT1 drive strength: highest */
#endif

#ifndef MSP430_XT1HFFREQ_1
#define MSP430_XT1HFFREQ_1   (0x0004)    /**< XT1 HF freq range: 1-4MHz */
#endif

#ifndef MSP430_XT1HFFREQ_2
#define MSP430_XT1HFFREQ_2   (0x0008)    /**< XT1 HF freq range: 4-8MHz */
#endif

#ifndef MSP430_XT1HFFREQ_3
#define MSP430_XT1HFFREQ_3   (0x000C)    /**< XT1 HF freq range: 8-24MHz */
#endif

#ifndef MSP430_SELA__REFOCLK
#define MSP430_SELA__REFOCLK (0x0200)   /**< ACLK source select: REFOCLK */
#endif

#ifndef MSP430_SELA__VLOCLK
#define MSP430_SELA__VLOCLK  (0x0100)   /**< ACLK source select: VLOCLK */
#endif

#ifndef MSP430_SELA__XT1CLK
#define MSP430_SELA__XT1CLK  (0x0000)   /**< ACLK source select: XT1CLK */
#endif

#ifndef MSP430_SELA__DCOCLK
#define MSP430_SELA__DCOCLK  (0x0300)   /**< ACLK source select: DCOCLK */
#endif

#ifndef MSP430_SELS__DCOCLK
#define MSP430_SELS__DCOCLK  (0x0030)   /**< SMCLK source select: DCOCLK */
#endif

#ifndef MSP430_SELM__DCOCLK
#define MSP430_SELM__DCOCLK  (0x0003)   /**< MCLK source select: DCOCLK */
#endif

#ifndef MSP430_DIVS__1
#define MSP430_DIVS__1       (0x0000)    /**< SMCLK source divider: /1 */
#endif

#ifndef MSP430_DIVS__2
#define MSP430_DIVS__2       (0x0010)    /**< SMCLK source divider: /2 */
#endif

#ifndef MSP430_DIVS__4
#define MSP430_DIVS__4       (0x0020)    /**< SMCLK source divider: /4 */
#endif

#ifndef MSP430_DIVS__8
#define MSP430_DIVS__8       (0x0030)    /**< SMCLK source divider: /8 */
#endif

#ifndef MSP430_DIVS__16
#define MSP430_DIVS__16      (0x0040)    /**< SMCLK source divider: /16 */
#endif

#ifndef MSP430_DIVS__32
#define MSP430_DIVS__32      (0x0050)    /**< SMCLK source divider: /32 */
#endif

#ifndef MSP430_DIVA__1
#define MSP430_DIVA__1       (0x0000)    /**< ACLK source divider: /1 */
#endif

#ifndef MSP430_DIVM__1
#define MSP430_DIVM__1       (0x0000)    /**< MCLK source divider: /1 */
#endif

#ifndef MSP430_FRCTLPW
#define MSP430_FRCTLPW       (0xA500)    /**< FRAM control password */
#endif

#ifndef MSP430_NWAITS_0
#define MSP430_NWAITS_0      (0x0000)    /**< FRAM wait states: 0 */
#endif

#ifndef MSP430_NWAITS_1
#define MSP430_NWAITS_1      (0x0010)    /**< FRAM wait states: 1 */
#endif

#ifndef REFON
#define REFON               (0x0001)    /**< Reference on */
#endif

/*
 * DCOFSEL sits at bits 1-3 of CSCTL1, so each value below is the field value
 * shifted left by 1.  DCO frequency per DCOFSEL value:
 *
 *   DCOFSEL   DCORSEL = 0   DCORSEL = 1
 *   0         1 MHz         1 MHz
 *   1         2.67 MHz      5.33 MHz
 *   2         3.5 MHz       7 MHz
 *   3         4 MHz         8 MHz
 *   4         5.33 MHz      16 MHz
 *   5         6.67 MHz      21 MHz
 *   6         8 MHz         24 MHz
 */
/** @name DCOFSEL values and the DCORSEL bit of CSCTL1
 * @{ */
#ifndef DCOFSEL_0
#define DCOFSEL_0           (0x0000)
#endif

#ifndef DCOFSEL_1
#define DCOFSEL_1           (0x0002)
#endif

#ifndef DCOFSEL_2
#define DCOFSEL_2           (0x0004)
#endif

#ifndef DCOFSEL_3
#define DCOFSEL_3           (0x0006)
#endif

#ifndef DCOFSEL_4
#define DCOFSEL_4           (0x0008)
#endif

#ifndef DCOFSEL_5
#define DCOFSEL_5           (0x000A)
#endif

#ifndef DCOFSEL_6
#define DCOFSEL_6           (0x000C)
#endif

#ifndef DCORSEL
#define DCORSEL             (0x0040)    /**< DCO range select */
#endif
/** @} */

/*---------------------------------------------------------------------------*/
/* EXTERNAL VARIABLES - Clock frequency cache                               */
/*---------------------------------------------------------------------------*/

/* Clock state as last configured; reading it does not touch the CS. */

/** MCLK frequency in Hz. */
extern volatile unsigned long g_mclk_hz;
/** SMCLK frequency in Hz. */
extern volatile unsigned long g_smclk_hz;
/** ACLK frequency in Hz, as the requested source's nominal rate. */
extern volatile unsigned long g_aclk_hz;
/** VLO frequency in Hz; nothing measures the VLO, so it stays 0. */
extern volatile unsigned long g_vlo_hz;
/** 32768 once LFXT has started or been chosen for ACLK, else 0. */
extern volatile unsigned long g_xt1_hz;
/** true after tiku_cpu_msp430_lfxt_init() starts LFXT, false after a fault. */
extern volatile bool g_xt1_enabled;
/** true once tiku_cpu_msp430_clock_set_advanced() has run. */
extern volatile bool g_clock_initialized;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Set MCLK, the SMCLK divider and the ACLK source.
 *
 * MCLK and SMCLK run from the DCO, FRAM wait states are set to 0, and
 * g_mclk_hz, g_smclk_hz and g_aclk_hz are updated.
 *
 * @param mclk         MCLK frequency code (CPU_FREQ_*), not MHz; codes
 *                     outside 1..CPU_FREQ_8MHZ select 8 MHz
 * @param smclk_div    SMCLK divider: 1, 2, 4 or 8 (also 16 and 32 on
 *                     FR59xx); any other value divides by 1
 * @param aclk_source  ACLK source
 */
void tiku_cpu_msp430_clock_set_advanced(unsigned int mclk,
                                        unsigned int smclk_div,
                                        tiku_aclk_source_t aclk_source);

/*---------------------------------------------------------------------------*/
/* OSCILLATOR FAULT HANDLING                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Start the LFXT crystal oscillator.
 *
 * Starts at full drive and drops to the lowest drive once the fault flags
 * stay clear.
 *
 * @param bypass  true: external clock on LFXIN; false: crystal
 * @return TIKU_CLOCK_OK, or TIKU_CLOCK_FAULT_XT1 when the oscillator still
 *         faults after CLOCK_FAULT_TIMEOUT polls (LFXT is then off and any
 *         fault handler has run); devices without LFXT always return
 *         TIKU_CLOCK_FAULT_XT1
 */
tiku_clock_result_t tiku_cpu_msp430_lfxt_init(bool bypass);

/**
 * @brief Turn the LFXT oscillator off and clear its fault flags.
 *
 * Does nothing on devices without LFXT.
 */
void tiku_cpu_msp430_lfxt_disable(void);

/*---------------------------------------------------------------------------*/
/* CLOCK FAULT HANDLING                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Report whether an oscillator fault is pending.
 * @return true while OFIFG is set in SFRIFG1
 */
bool tiku_cpu_msp430_clock_has_fault(void);

/**
 * @brief Clear the device's oscillator fault flags and OFIFG.
 *
 * @note Repeats until OFIFG stays clear, with no bound: a fault that keeps
 *       re-asserting holds the caller here.
 */
void tiku_cpu_msp430_clock_clear_faults(void);

/**
 * @brief Set the function called when a crystal fails to start.
 * @param handler Called with TIKU_CLOCK_FAULT_XT1 or TIKU_CLOCK_FAULT_HFXT;
 *                NULL for none
 */
void tiku_cpu_msp430_clock_set_fault_handler(void (*handler)(unsigned int fault_type));

/*---------------------------------------------------------------------------*/
/* CLOCK FREQUENCY GETTERS                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Gets the current Main Clock (MCLK) frequency in Hz
 * @return MCLK frequency in Hz (g_mclk_hz)
 */
unsigned long tiku_cpu_msp430_clock_get_hz(void);

/**
 * @brief Gets the current Auxiliary Clock (ACLK) frequency in Hz
 * @return ACLK frequency in Hz (g_aclk_hz)
 */
unsigned long tiku_cpu_msp430_aclk_get_hz(void);

/**
 * @brief Gets the current Sub-Main Clock (SMCLK) frequency in Hz
 * @return SMCLK frequency in Hz (g_smclk_hz)
 */
unsigned long tiku_cpu_msp430_smclk_get_hz(void);

/*---------------------------------------------------------------------------*/
/* UTILITY FUNCTIONS                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Spin for @p cycles loop iterations.
 *
 * Each iteration takes several CPU cycles, so the delay is longer than
 * @p cycles CPU cycles.
 *
 * @param cycles Number of loop iterations
 */
void tiku_cpu_msp430_delay_cycles(unsigned long cycles);

/**
 * @brief Converts CPU frequency enum to actual MHz value for display
 * @param freq_enum The frequency enum value (e.g., CPU_FREQ_7MHZ)
 * @return Actual frequency string (e.g., "7"), or "unknown"
 */
const char* tiku_cpu_freq_to_mhz_str(unsigned int freq_enum);

/*---------------------------------------------------------------------------*/
/* CPU BOOT AND POWER MANAGEMENT FUNCTIONS                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Declared only: this port has no definition, so a call fails to link.
 */
void tiku_cpu_boot_msp430_setup(void);

/**
 * @brief Set the MCLK frequency at boot.
 *
 * SMCLK runs undivided from the DCO and ACLK is requested from
 * TIKU_ACLK_REFO; no crystal is started.
 *
 * @param freq_mhz  MCLK frequency code (CPU_FREQ_*), not MHz
 */
void tiku_cpu_freq_msp430_init(unsigned int freq_mhz);

/**
 * @brief Make every pin of each port the device has an output driven low.
 *
 * Pin interrupts on those ports are disabled.
 */
void tiku_cpu_boot_msp430_pins_init_low(void);

/**
 * @brief Enter LPM0 with interrupts enabled (GIE is set).
 */
void tiku_cpu_boot_msp430_power_lpm0_enter(void);

/**
 * @brief Enter LPM3 with interrupts enabled (GIE is set).
 */
void tiku_cpu_boot_msp430_power_lpm3_enter(void);

/**
 * @brief Enter LPM4 with interrupts enabled (GIE is set).
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
 * @brief Reset the device with a software POR (PMMSWPOR); does not return.
 */
void tiku_cpu_boot_msp430_reset(void);

/**
 * @brief Boot-time CPU setup: interrupts off, every pin low, LOCKLPM5 clear.
 *
 * Interrupts stay disabled on return; tiku_sched_loop() enables them.
 */
void tiku_cpu_boot_msp430_init(void);

#endif /* TIKU_CPU_FREQ_BOOT_ARCH_H_ */
