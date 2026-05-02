/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_arch.c - MSP430 CPU frequency configuration
 *
 * This file provides the core CPU frequency configuration functions
 * for the Tiku Operating System on MSP430 microcontrollers.
 * It includes clock system configuration, frequency setting, and
 * frequency getting functions.
 *
 * Supports: MSP430FR5969, MSP430FR5994, MSP430FR2433
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

#include "tiku_platform.h"
#include "tiku_cpu_freq_boot_arch.h"

/*---------------------------------------------------------------------------*/
/* CS MODULE ABSTRACTION MACROS                                              */
/*---------------------------------------------------------------------------*/

#if TIKU_DEVICE_CS_HAS_KEY
#define TIKU_CS_UNLOCK()    do { CSCTL0_H = CSKEY_H; } while(0)
#define TIKU_CS_LOCK()      do { CSCTL0_H = 0; } while(0)
#else
#define TIKU_CS_UNLOCK()    do { } while(0)
#define TIKU_CS_LOCK()      do { } while(0)
#endif

/* Module-private clock configuration */
static struct {
    tiku_clk_freq_t    freq;
    tiku_clk_div_t     smclk_div;
    tiku_aclk_source_t aclk_src;
} clk;


/**
 * @brief Converts CPU frequency enum to actual MHz string for display
 */
const char* tiku_cpu_freq_to_mhz_str(unsigned int freq_enum)
{

    switch (freq_enum) {
        case CPU_FREQ_1MHZ:      return "1";
        case CPU_FREQ_2_677MHZ:  return "2.67";
        case CPU_FREQ_3_5MHZ:    return "3.5";
        case CPU_FREQ_4MHZ:      return "4";
        case CPU_FREQ_5_33MHZ:   return "5.33";
        case CPU_FREQ_7MHZ:      return "7";
        case CPU_FREQ_8MHZ:      return "8";
        case CPU_FREQ_16MHZ:     return "16";
        default:                 return "unknown";
    }

}

/* Global variables from header file */
volatile unsigned long g_mclk_hz = 0;         /* MCLK frequency in Hz */
volatile unsigned long g_smclk_hz = 0;        /* SMCLK frequency in Hz */
volatile unsigned long g_aclk_hz = 0;         /* ACLK frequency in Hz */
volatile unsigned long g_vlo_hz = 0;          /* Measured VLO frequency */
volatile unsigned long g_xt1_hz = 0;          /* Crystal frequency if enabled */
volatile bool g_xt1_enabled = false;         /* XT1 oscillator status */
volatile bool g_clock_initialized = false;   /* Initialization status */

/* Clock fault handler */
static void (*g_fault_handler)(unsigned int) = NULL;

/* Forward declaration */
static void cpu_freq_msp430_init(unsigned int freq_mhz, unsigned int sfreq_div, bool enable_lfxt_crystal, bool enable_hfxt_crystal);

/**
 * @brief Initializes all GPIO pins to a default state.
 *
 * All pins are configured as outputs and driven low to prevent floating
 * inputs. Interrupts for pins with interrupt capability are disabled.
 */

 void tiku_cpu_boot_msp430_pins_init_low(void)
 {
    /* Initialize all available GPIO pins as outputs driven low.
     * Port availability is determined by the device header.
     * Not all ports have interrupt capability (e.g., FR2433 P3 has no P3IE). */

#if TIKU_DEVICE_HAS_PORT1
    P1DIR = 0xFF; P1OUT = 0x00; P1IE = 0x00;
#endif
#if TIKU_DEVICE_HAS_PORT2
    P2DIR = 0xFF; P2OUT = 0x00; P2IE = 0x00;
#endif
#if TIKU_DEVICE_HAS_PORT3
    P3DIR = 0xFF; P3OUT = 0x00;
#ifdef P3IE
    P3IE = 0x00;
#endif
#endif
#if TIKU_DEVICE_HAS_PORT4
    P4DIR = 0xFF; P4OUT = 0x00;
#ifdef P4IE
    P4IE = 0x00;
#endif
#endif
#if TIKU_DEVICE_HAS_PORT5
    P5DIR = 0xFF; P5OUT = 0x00;
#ifdef P5IE
    P5IE = 0x00;
#endif
#endif
#if TIKU_DEVICE_HAS_PORT6
    P6DIR = 0xFF; P6OUT = 0x00;
#ifdef P6IE
    P6IE = 0x00;
#endif
#endif
#if TIKU_DEVICE_HAS_PORT7
    P7DIR = 0xFF; P7OUT = 0x00;
#ifdef P7IE
    P7IE = 0x00;
#endif
#endif
#if TIKU_DEVICE_HAS_PORT8
    P8DIR = 0xFF; P8OUT = 0x00;
#ifdef P8IE
    P8IE = 0x00;
#endif
#endif
#if TIKU_DEVICE_HAS_PORT9
    P9DIR = 0xFF; P9OUT = 0x00;
#ifdef P9IE
    P9IE = 0x00;
#endif
#endif
#if TIKU_DEVICE_HAS_PORTJ
    PJDIR = 0xFF; PJOUT = 0x00;
#endif

}

/*---------------------------------------------------------------------------*/
/* POWER MANAGEMENT                                                         */
/*---------------------------------------------------------------------------*/

 /**
  * @brief Enters Low Power Mode 0.
  *
  * CPU is disabled. MCLK is disabled. SMCLK and ACLK remain active.
  */
 void tiku_cpu_boot_msp430_power_lpm0_enter(void)
 {

     // Enter Low Power Mode 0
     // LPM0_bits is the bit mask for LPM0
     // GIE is the global interrupt enable bit

     __bis_SR_register(LPM0_bits | GIE);

     __no_operation();

}

 /**
  * @brief Enters Low Power Mode 3.
  *
  * CPU, MCLK, SMCLK, and DCO are disabled. ACLK remains active.
  */
 void tiku_cpu_boot_msp430_power_lpm3_enter(void)
 {

     // Enter Low Power Mode 3
     // LPM3_bits is the bit mask for LPM3
     // GIE is the global interrupt enable bit

     __bis_SR_register(LPM3_bits | GIE);

     __no_operation();

    }

 /**
  * @brief Enters Low Power Mode 4.
  *
  * CPU and all clocks are disabled.
  */
 void tiku_cpu_boot_msp430_power_lpm4_enter(void)
 {

    // Enter Low Power Mode 4
    // LPM4_bits is the bit mask for LPM4
    // GIE is the global interrupt enable bit

     __bis_SR_register(LPM4_bits | GIE);

     __no_operation();

}

/*---------------------------------------------------------------------------*/
/* INTERRUPT CONTROL                                                        */
/*---------------------------------------------------------------------------*/

 /**
  * @brief Enables global interrupts.
  */

 void tiku_cpu_boot_msp430_global_interrupts_enable(void)
 {

    __enable_interrupt();

}

 /**
  * @brief Disables global interrupts.
  */
 void tiku_cpu_boot_msp430_global_interrupts_disable(void)
 {

    __disable_interrupt();

}

/*---------------------------------------------------------------------------*/
/* SYSTEM CONTROL                                                           */
/*---------------------------------------------------------------------------*/

 /**
  * @brief Performs a software triggered reset of the device.
  */
 void tiku_cpu_boot_msp430_reset(void)
 {

     // Perform a software triggered reset of the device
     // PMMMPW is the password for the PMM module
     // PMMSWPOR is the reset command
     // PMMCTL0 is the PMM control register

     PMMCTL0 = PMMPW | PMMSWPOR;

 }

 /*---------------------------------------------------------------------------*/
 /* OSCILLATOR FAULT HANDLING                                                 */
 /*---------------------------------------------------------------------------*/

#if TIKU_DEVICE_HAS_LFXT
/**
 * @brief Waits ONLY for the XT1 (typically Low-Frequency) crystal fault flag
 *        to clear, ignoring all other oscillator faults.
 *
 * @param timeout The number of retries before the function gives up.
 * @return true if XT1 is stable, false if a timeout occurs.
 */
static bool wait_for_lfxt_fault_clear(unsigned long timeout)
{
    unsigned long count = 0;

    // Unlock CS registers
    TIKU_CS_UNLOCK();

    do {
        // Attempt to clear the XT1 fault flag
        CSCTL5 &= ~LFXTOFFG;

        // Clear the master fault flag
        SFRIFG1 &= ~OFIFG;

        if (++count > timeout) {
            CPU_FREQ_PRINTF("Timeout reached while waiting for LFXT fault clear\n");

            // Lock CS registers
            TIKU_CS_LOCK();

            return false;  /* Timeout */
        }

    // Check the XT1-specific fault flag directly
    } while (CSCTL5 & LFXTOFFG);

    // Lock CS registers
    TIKU_CS_LOCK();

    CPU_FREQ_PRINTF("CPU_FREQ: LFXT fault cleared\n");

    return true;  /* Success: XT1 is stable */
}
#endif /* TIKU_DEVICE_HAS_LFXT */

#if TIKU_DEVICE_HAS_HFXT
/**
 * @brief Waits ONLY for the XT2 (High-Frequency) crystal fault flag to clear,
 *        ignoring all other oscillator faults.
 *
 * @param timeout The number of retries before the function gives up.
 * @return true if XT2 is stable, false if a timeout occurs.
 */
static bool wait_for_hfxt_fault_clear(unsigned long timeout)
{
    unsigned long count = 0;

    // Unlock CS registers
    TIKU_CS_UNLOCK();

    do {
        // Attempt to clear the XT2 fault flag
        CSCTL5 &= ~HFXTOFFG;

        // Clear the master fault flag
        SFRIFG1 &= ~OFIFG;

        if (++count > timeout) {

            CPU_FREQ_PRINTF("Timeout reached while waiting for HFXT fault clear\n");

            // Lock CS registers
            TIKU_CS_LOCK();

            return false;  /* Timeout */
        }

    // Check the XT2-specific fault flag directly
    } while (CSCTL5 & HFXTOFFG);

    // Lock CS registers
    TIKU_CS_LOCK();

    return true;  /* Success: XT2 is stable */
}
#endif /* TIKU_DEVICE_HAS_HFXT */

/**
 * @brief Waits for all oscillator fault flags to clear with timeout.
*/
static bool wait_for_all_fault_clear(unsigned long timeout)
{
    unsigned long count = 0;

    // Unlock CS registers
    TIKU_CS_UNLOCK();

    do {
        /* Clear device-specific fault flags */
#if TIKU_DEVICE_HAS_LFXT && TIKU_DEVICE_HAS_HFXT
        CSCTL5 &= ~(LFXTOFFG | HFXTOFFG);
#elif TIKU_DEVICE_HAS_LFXT
        CSCTL5 &= ~LFXTOFFG;
#elif TIKU_DEVICE_HAS_HFXT
        CSCTL5 &= ~HFXTOFFG;
#else
        /* FR2433: clear DCO fault flag only */
        CSCTL7 &= ~DCOFFG;
#endif

        SFRIFG1 &= ~OFIFG;                 // clear master oscillator fault flag

        if (++count > timeout) {

            CPU_FREQ_PRINTF("Timeout reached while waiting for all fault clear\n");

            // Lock CS registers
            TIKU_CS_LOCK();

            return false;

        }

    } while (SFRIFG1 & OFIFG);             // loop until master flag stays clear

    // Lock CS registers
    TIKU_CS_LOCK();

    CPU_FREQ_PRINTF("All fault flags cleared\n");

    return true;

}

 /*---------------------------------------------------------------------------*/
 /* CLOCK SYSTEM CONFIGURATION                                               */
 /*---------------------------------------------------------------------------*/

 /**
  * @brief Sets the CPU clocks with advanced options.
  *
  * @param freq The desired MCLK frequency. It is not indexed as MHz.
  * @param sfreq_div The divider for SMCLK.
  * @param aclk_source The source for ACLK.
  */

 void tiku_cpu_msp430_clock_set_advanced(tiku_clk_freq_t freq,
                                         tiku_clk_div_t sfreq_div,
                                         tiku_aclk_source_t aclk_source)
 {
     /* Validate input parameters */
     if (freq < CPU_FREQ_MIN_MHZ || freq > CPU_FREQ_8MHZ) {
         freq = CPU_FREQ_8MHZ;  /* Default/clamp to 8MHz */
     }

     /* Configure FRAM wait states BEFORE touching clock system.
      * For frequencies up to 8MHz, 0 wait states is sufficient.
      */
     CPU_FREQ_PRINTF("Configuring FRAM wait states for %s MHz\n", tiku_cpu_freq_to_mhz_str(freq));

     /* 8MHz and below can use 0 wait states. FRCTLPW=0xA500 */
     FRCTL0 = FRCTLPW | NWAITS_0;

     /* Small delay for FRAM controller to apply new wait state setting */
     __delay_cycles(100);

     CPU_FREQ_PRINTF("FRAM wait states configured\n");

     /* Unlock CS registers for clock configuration */
     TIKU_CS_UNLOCK();

     /*-----------------------------------------------------------------------*/
     /* DCO FREQUENCY CONFIGURATION                                          */
     /*-----------------------------------------------------------------------*/

#if defined(TIKU_DEVICE_CS_TYPE_FR2X33)
     /*
      * FR2433 CS module: DCO frequency set via DCORSEL (3-bit field in CSCTL1).
      * Disable FLL before changing DCO settings, then re-enable.
      * DCORSEL values: 0=1MHz, 1=2.67MHz, 2=3.5MHz, 3=4MHz,
      *                 4=5.33MHz, 5=7MHz, 6=8MHz, 7=16MHz
      */
     __bis_SR_register(SCG0);  /* Disable FLL */

     CSCTL3 = SELREF__REFOCLK; /* FLL reference = REFO (32768 Hz) */

    switch (freq) {
        case TIKU_CLK_FREQ_1MHZ:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_0;
            CSCTL2 = FLLD_0 + 30;   /* 32768*(30+1) = 1,015,808 Hz */
            clk.freq = TIKU_CLK_FREQ_1MHZ;
            break;
        case TIKU_CLK_FREQ_2_677MHZ:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_1;
            CSCTL2 = FLLD_0 + 81;   /* 32768*(81+1) = 2,686,976 Hz */
            clk.freq = TIKU_CLK_FREQ_2_677MHZ;
            break;
        case TIKU_CLK_FREQ_3_5MHZ:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_2;
            CSCTL2 = FLLD_0 + 106;  /* 32768*(106+1) = 3,506,176 Hz */
            clk.freq = TIKU_CLK_FREQ_3_5MHZ;
            break;
        case TIKU_CLK_FREQ_4MHZ:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_3;
            CSCTL2 = FLLD_0 + 121;  /* 32768*(121+1) = 3,997,696 Hz */
            clk.freq = TIKU_CLK_FREQ_4MHZ;
            break;
        case TIKU_CLK_FREQ_5_33MHZ:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_4;
            CSCTL2 = FLLD_0 + 162;  /* 32768*(162+1) = 5,341,184 Hz */
            clk.freq = TIKU_CLK_FREQ_5_33MHZ;
            break;
        case TIKU_CLK_FREQ_7MHZ:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_5;
            CSCTL2 = FLLD_0 + 213;  /* 32768*(213+1) = 7,012,352 Hz */
            clk.freq = TIKU_CLK_FREQ_7MHZ;
            break;
        case TIKU_CLK_FREQ_8MHZ:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_6;
            CSCTL2 = FLLD_0 + 243;  /* 32768*(243+1) = 7,995,392 Hz */
            clk.freq = TIKU_CLK_FREQ_8MHZ;
            break;
        default:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_6;
            CSCTL2 = FLLD_0 + 243;
            clk.freq = TIKU_CLK_FREQ_8MHZ;
            break;
    }

     __delay_cycles(3);
     __bic_SR_register(SCG0);  /* Re-enable FLL */

     /* Clear DCO fault, then wait for FLL to lock (bounded) */
     CSCTL7 &= ~DCOFFG;
     __delay_cycles(200000);   /* ~25 ms at 8 MHz for FLL to settle */
     {
         unsigned long fllWait = 500000UL;
         while ((CSCTL7 & (FLLUNLOCK0 | FLLUNLOCK1)) && --fllWait)
             ;
     }

     /* Disable FLL to freeze DCO at the locked frequency.
      * This eliminates FLL hunting jitter that corrupts UART. */
     __bis_SR_register(SCG0);

#else
     /* FR5969/FR5994 CS_A module: DCO frequency set via DCOFSEL + DCORSEL.
      * DCOFSEL values: 0=1MHz, 1=2.67MHz, 2=3.5MHz, 3=4MHz (low range)
      * With DCORSEL=1: 0=1MHz, 1=5.33MHz, 2=7MHz, 3=8MHz, 4=16MHz, 6=24MHz
      */
    switch (freq) {
        case TIKU_CLK_FREQ_1MHZ:
            CSCTL1 = DCOFSEL_0;
            clk.freq = TIKU_CLK_FREQ_1MHZ;
            break;
        case TIKU_CLK_FREQ_2_677MHZ:
            CSCTL1 = DCOFSEL_1;
            clk.freq = TIKU_CLK_FREQ_2_677MHZ;
            break;
        case TIKU_CLK_FREQ_3_5MHZ:
            CSCTL1 = DCOFSEL_2;
            clk.freq = TIKU_CLK_FREQ_3_5MHZ;
            break;
        case TIKU_CLK_FREQ_4MHZ:
            CSCTL1 = DCOFSEL_3;
            clk.freq = TIKU_CLK_FREQ_4MHZ;
            break;
        case TIKU_CLK_FREQ_5_33MHZ:
            CSCTL1 = DCORSEL | DCOFSEL_1;
            clk.freq = TIKU_CLK_FREQ_5_33MHZ;
            break;
        case TIKU_CLK_FREQ_7MHZ:
            CSCTL1 = DCORSEL | DCOFSEL_2;
            clk.freq = TIKU_CLK_FREQ_7MHZ;
            break;
        case TIKU_CLK_FREQ_8MHZ:
            CSCTL1 = DCORSEL | DCOFSEL_3;
            clk.freq = TIKU_CLK_FREQ_8MHZ;
            break;
        default:
            CSCTL1 = DCORSEL | DCOFSEL_3;
            clk.freq = TIKU_CLK_FREQ_8MHZ;
            break;
    }
#endif /* TIKU_DEVICE_CS_TYPE_FR2X33 */

    /* Allow DCO to stabilize after frequency change */
    __delay_cycles(250);

    CPU_FREQ_PRINTF("DCO frequency configured successfully\n");

     /*-----------------------------------------------------------------------*/
     /* CLOCK SOURCE AND DIVIDER CONFIGURATION                               */
     /*-----------------------------------------------------------------------*/

#if defined(TIKU_DEVICE_CS_TYPE_FR2X33)
     /*
      * FR2433 CS module:
      *   CSCTL4: SELMS (MCLK+SMCLK source, bits 0-2), SELA (ACLK source, bit 8)
      *   CSCTL5: DIVM (MCLK divider, bits 0-2), DIVS (SMCLK divider, bits 4-5)
      */
    {
        unsigned int selms_val = SELMS__DCOCLKDIV; /* MCLK+SMCLK = DCOCLKDIV */
        unsigned int sela_val;

        switch(aclk_source) {
            case TIKU_ACLK_REFO:
                sela_val = SELA__REFOCLK;
                break;
            case TIKU_ACLK_VLO:
            case TIKU_ACLK_DCO:
            default:
                /* FR2433 ACLK only supports XT1CLK or REFOCLK.
                 * For VLO/DCO requests, fall back to REFOCLK. */
                sela_val = SELA__REFOCLK;
                break;
        }

        CSCTL4 = sela_val | selms_val;

        CPU_FREQ_PRINTF("Clock sources configured\n");

        /* Configure clock dividers in CSCTL5 */
        unsigned int divs = 0;

        switch(sfreq_div) {
            case 1:   divs = DIVS__1; clk.smclk_div = TIKU_CLK_DIV_1; break;
            case 2:   divs = DIVS__2; clk.smclk_div = TIKU_CLK_DIV_2; break;
            case 4:   divs = DIVS__4; clk.smclk_div = TIKU_CLK_DIV_4; break;
            case 8:   divs = DIVS__8; clk.smclk_div = TIKU_CLK_DIV_8; break;
            default:  divs = DIVS__1; clk.smclk_div = TIKU_CLK_DIV_1; break;
        }

        CSCTL5 = divs | DIVM__1;

        CPU_FREQ_PRINTF("Clock dividers applied\n");
    }

#else
     /* FR5969/FR5994 CS_A module:
      *   CSCTL2: SELA, SELS, SELM (independent source selects)
      *   CSCTL3: DIVA, DIVS, DIVM (independent dividers)
      */
    {
        /* Configure ACLK source */
        unsigned int sela;

        switch(aclk_source) {
            case TIKU_ACLK_VLO:
                sela = MSP430_SELA__VLOCLK;
                break;
            case TIKU_ACLK_REFO:
                sela = MSP430_SELA__REFOCLK;
                break;
            case TIKU_ACLK_LFXT:
                sela = MSP430_SELA__XT1CLK;
                break;
            case TIKU_ACLK_DCO:
                sela = MSP430_SELA__DCOCLK;
                break;
            default:
                sela = MSP430_SELA__VLOCLK;
                break;
        }

        CPU_FREQ_PRINTF("ACLK source configured\n");

        /* Configure clock sources: ACLK, SMCLK=DCO, MCLK=DCO */
        CSCTL2 = sela | SELS__DCOCLK | SELM__DCOCLK;

        CPU_FREQ_PRINTF("Clock sources configured\n");

        /* Configure clock dividers */
        unsigned int divs = 0;

        switch(sfreq_div) {
            case 1:   divs = DIVS__1;  clk.smclk_div = TIKU_CLK_DIV_1;  break;
            case 2:   divs = DIVS__2;  clk.smclk_div = TIKU_CLK_DIV_2;  break;
            case 4:   divs = DIVS__4;  clk.smclk_div = TIKU_CLK_DIV_4;  break;
            case 8:   divs = DIVS__8;  clk.smclk_div = TIKU_CLK_DIV_8;  break;
            case 16:  divs = DIVS__16; clk.smclk_div = TIKU_CLK_DIV_16; break;
            case 32:  divs = DIVS__32; clk.smclk_div = TIKU_CLK_DIV_32; break;
            default:  divs = DIVS__1;  clk.smclk_div = TIKU_CLK_DIV_1;  break;
        }

        CPU_FREQ_PRINTF("Clock dividers configured\n");

        /* Set all dividers (ACLK/1, SMCLK/configured, MCLK/1) */
        CSCTL3 = DIVA__1 | divs | DIVM__1;

        CPU_FREQ_PRINTF("Clock dividers applied\n");
    }
#endif /* TIKU_DEVICE_CS_TYPE_FR2X33 */

     /*-----------------------------------------------------------------------*/
     /* FREQUENCY CACHE UPDATE                                               */
     /*-----------------------------------------------------------------------*/

     /* Update clock frequency cache */
     switch (clk.freq) {
         case TIKU_CLK_FREQ_1MHZ:      g_mclk_hz = 1000000UL; break;
         case TIKU_CLK_FREQ_2_677MHZ:  g_mclk_hz = 2670000UL; break;
         case TIKU_CLK_FREQ_3_5MHZ:    g_mclk_hz = 3500000UL; break;
         case TIKU_CLK_FREQ_4MHZ:      g_mclk_hz = 4000000UL; break;
         case TIKU_CLK_FREQ_5_33MHZ:   g_mclk_hz = 5330000UL; break;
         case TIKU_CLK_FREQ_7MHZ:      g_mclk_hz = 7000000UL; break;
         case TIKU_CLK_FREQ_8MHZ:      g_mclk_hz = 8000000UL; break;
         default:                       g_mclk_hz = 8000000UL; break;
     }
     g_smclk_hz = g_mclk_hz / clk.smclk_div;
     switch(aclk_source) {
         case TIKU_ACLK_VLO:  g_aclk_hz = VLO_FREQ_NOMINAL_HZ; break;
         case TIKU_ACLK_REFO: g_aclk_hz = REFO_FREQ_HZ; break;
         case TIKU_ACLK_LFXT: g_aclk_hz = XT1_FREQ_32KHZ; g_xt1_hz = XT1_FREQ_32KHZ; break;
         case TIKU_ACLK_DCO:  g_aclk_hz = g_mclk_hz; break;
         default:             g_aclk_hz = VLO_FREQ_NOMINAL_HZ; break;
     }
     g_clock_initialized = true;

     /* Clear any oscillator fault flags before locking */
#if TIKU_DEVICE_HAS_LFXT && TIKU_DEVICE_HAS_HFXT
     CSCTL5 &= ~(LFXTOFFG | HFXTOFFG);
#elif TIKU_DEVICE_HAS_LFXT
     CSCTL5 &= ~LFXTOFFG;
#elif !defined(TIKU_DEVICE_CS_TYPE_FR2X33)
     /* FR5969/5994 without XT: still clear fault flags */
     SFRIFG1 &= ~OFIFG;
#else
     /* FR2433: clear DCO fault flag */
     CSCTL7 &= ~DCOFFG;
#endif
     SFRIFG1 &= ~OFIFG;

     /* Lock CS registers */
     TIKU_CS_LOCK();

     /* Additional delay for clock to fully stabilize */
     __delay_cycles(500);

#if TIKU_DEVICE_HAS_LFXT
     if (aclk_source == TIKU_ACLK_LFXT) {

        CPU_FREQ_PRINTF("ACLK source is XT1\n");
        wait_for_lfxt_fault_clear(1000);

    }
#endif


     CPU_FREQ_PRINTF("CS registers locked\n");


     CPU_FREQ_PRINTF("Clock configuration completed\n");
 }


/*---------------------------------------------------------------------------*/
/* CRYSTAL OSCILLATOR CONFIGURATION                                         */
/*---------------------------------------------------------------------------*/


#if TIKU_DEVICE_HAS_LFXT
/**
 * @brief Initializes the LFXT crystal oscillator
 * @param bypass If true, the LFXT will be bypassed and an external clock will be used
 * @return TIKU_CLOCK_OK if the LFXT is initialized successfully, TIKU_CLOCK_FAULT_XT1 if not
 */
 tiku_clock_result_t tiku_cpu_msp430_lfxt_init(bool bypass)
 {
     unsigned int count = 0;

     // Unlock CS
     TIKU_CS_UNLOCK();

     // Route LFXT pins (device-specific)
     TIKU_DEVICE_LFXT_PSEL_REG |= TIKU_DEVICE_LFXT_PSEL_BITS;

     TIKU_DEVICE_LFXT_PSEL1_REG &= ~TIKU_DEVICE_LFXT_PSEL1_BITS;

     // Bypass vs crystal
     if (bypass) {

        CPU_FREQ_PRINTF("LFXT bypassed\n");
        CSCTL4 |= LFXTBYPASS;      // external clock on LFXIN

    } else {

        clk.aclk_src = TIKU_ACLK_LFXT;
        CPU_FREQ_PRINTF("LFXT crystal mode\n");
        CSCTL4 &= ~LFXTBYPASS;     // crystal mode

    }

     // Turn on LFXT and start with max drive for startup
     CSCTL4 &= ~LFXTOFF;

     CSCTL4 = (CSCTL4 & ~LFXTDRIVE_3) | LFXTDRIVE_3;

     // Clear oscillator fault flags until stable (or timeout)
     do {

        CSCTL5 &= ~LFXTOFFG;       // clear LFXT fault
        SFRIFG1 &= ~OFIFG;         // clear global osc fault
        __delay_cycles(10000);     // small settle

        if (++count > CLOCK_FAULT_TIMEOUT) {

            // Give up: turn LFXT off and lock CS
             CSCTL4 |= LFXTOFF;

             TIKU_CS_LOCK();
             CPU_FREQ_PRINTF("LFXT fault detected\n");

             g_xt1_enabled = false;
             if (g_fault_handler) {
                 g_fault_handler(TIKU_CLOCK_FAULT_XT1);
             }
             return TIKU_CLOCK_FAULT_XT1;
         }
     } while (SFRIFG1 & OFIFG);

     // Drop drive to lowest once stable (saves power)
     CSCTL4 = (CSCTL4 & ~LFXTDRIVE_3) | LFXTDRIVE_0;

     // Lock CS
     TIKU_CS_LOCK();

     g_xt1_enabled = true;
     g_xt1_hz = XT1_FREQ_32KHZ;

     CPU_FREQ_PRINTF("LFXT initialized successfully\n");

     return TIKU_CLOCK_OK;
 }
#else
/* No LFXT on this device — provide stub to avoid linker errors */
tiku_clock_result_t tiku_cpu_msp430_lfxt_init(bool bypass)
{
    (void)bypass;
    CPU_FREQ_PRINTF("LFXT not available on this device\n");
    return TIKU_CLOCK_FAULT_XT1;
}
#endif /* TIKU_DEVICE_HAS_LFXT */

#if TIKU_DEVICE_HAS_HFXT
/**
 * @brief Initializes the HFXT high-frequency crystal oscillator (4-24 MHz).
 * @param bypass If true, HFXT will be bypassed and an external clock is expected on HFXIN.
 * @param freq_hz Crystal/external clock frequency in Hz.
 * @return TIKU_CLOCK_OK on success, TIKU_CLOCK_FAULT_HFXT on failure.
 */
tiku_clock_result_t tiku_cpu_msp430_hfxt_init(bool bypass, unsigned long freq_hz)
{

    unsigned int count = 0;

    /* Unlock CS registers */
    TIKU_CS_UNLOCK();

    /* Route HFXT pins (device-specific) */
    TIKU_DEVICE_HFXT_PSEL_REG |= TIKU_DEVICE_HFXT_PSEL_BITS;

    TIKU_DEVICE_HFXT_PSEL1_REG &= ~TIKU_DEVICE_HFXT_PSEL1_BITS;

    if (bypass) {

        CPU_FREQ_PRINTF("HFXT bypassed\n");
        CSCTL4 |= HFXTBYPASS;      /* External clock on HFXIN */

    } else {

        CPU_FREQ_PRINTF("HFXT crystal mode\n");
        CSCTL4 &= ~HFXTBYPASS;     /* Crystal mode */

        /* Program the HFFREQ range based on freq_hz */
        #ifdef HFFREQ_0
        #ifndef HFFREQ
        #define HFFREQ (HFFREQ0 | HFFREQ1)
        #endif

        CSCTL4 &= ~(HFFREQ);
        if (freq_hz == 0UL) {
            CSCTL4 |= HFFREQ_0;
        } else if (freq_hz <= 4000000UL) {
            CSCTL4 |= HFFREQ_0;
        } else if (freq_hz <= 8000000UL) {
            CSCTL4 |= HFFREQ_1;
        } else if (freq_hz <= 16000000UL) {
            CSCTL4 |= HFFREQ_2;
        } else {
            CSCTL4 |= HFFREQ_3;
        }
        #endif /* HFFREQ_0 */
    }

    /* Turn HFXT on */
    CSCTL4 &= ~HFXTOFF;

    #ifdef HFXTDRIVE_3
    CSCTL4 = (CSCTL4 & ~HFXTDRIVE_3) | HFXTDRIVE_3;
    #elif defined(HFXTDRIVE)
    CSCTL4 |= HFXTDRIVE;
    #endif

    /* Clear oscillator fault flags until stable (or timeout) */
    do {
        CSCTL5 &= ~HFXTOFFG;
        SFRIFG1 &= ~OFIFG;
        __delay_cycles(10000);

        if (++count > CLOCK_FAULT_TIMEOUT) {
            CSCTL4 |= HFXTOFF;
            TIKU_CS_LOCK();

            CPU_FREQ_PRINTF("HFXT fault detected\n");
            if (g_fault_handler) {
                g_fault_handler(TIKU_CLOCK_FAULT_HFXT);
            }
            return TIKU_CLOCK_FAULT_HFXT;
        }
    } while (SFRIFG1 & OFIFG);

    #ifdef HFXTDRIVE_3
    CSCTL4 = (CSCTL4 & ~HFXTDRIVE_3) | HFXTDRIVE_0;
    #elif defined(HFXTDRIVE)
    CSCTL4 &= ~HFXTDRIVE;
    #endif

    /* Lock CS */
    TIKU_CS_LOCK();

    CPU_FREQ_PRINTF("HFXT initialized successfully\n");
    return TIKU_CLOCK_OK;
}
#endif /* TIKU_DEVICE_HAS_HFXT */


/*---------------------------------------------------------------------------*/
/* CLOCK FAULT HANDLING                                                     */
/*---------------------------------------------------------------------------*/

 /**
  * @brief Checks if any clock fault is present.
  */
 bool tiku_cpu_msp430_clock_has_fault(void)
 {
     return (SFRIFG1 & OFIFG) ? true : false;
 }

 /**
  * @brief Clears all clock fault flags.
  */
 void tiku_cpu_msp430_clock_clear_faults(void)
 {
     CPU_FREQ_PRINTF("Clearing oscillator fault flags\n");

     // Unlock CS registers
     TIKU_CS_UNLOCK();

     CPU_FREQ_PRINTF("CS registers unlocked for fault clearing\n");

     do {
#if TIKU_DEVICE_HAS_LFXT && TIKU_DEVICE_HAS_HFXT
         CSCTL5 &= ~(LFXTOFFG | HFXTOFFG);
#elif TIKU_DEVICE_HAS_LFXT
         CSCTL5 &= ~LFXTOFFG;
#elif defined(TIKU_DEVICE_CS_TYPE_FR2X33)
         CSCTL7 &= ~DCOFFG;
#endif
         SFRIFG1 &= ~OFIFG;                      // clear master fault

         CPU_FREQ_PRINTF("Clearing fault flags (SFRIFG1=0x%x)\n", SFRIFG1);
     } while (SFRIFG1 & OFIFG);                      // repeat if it re-asserts

     TIKU_CS_LOCK();

     CPU_FREQ_PRINTF("Fault clearing completed, CS registers locked\n");
 }

 /**
  * @brief Sets a custom clock fault handler.
  */
 void tiku_cpu_msp430_clock_set_fault_handler(void (*handler)(unsigned int fault_type))
 {
     g_fault_handler = handler;
 }

 /**
  * @brief Delays for specified number of CPU cycles.
  */
 void tiku_cpu_msp430_delay_cycles(unsigned long cycles)
 {
     while(cycles > 0) {
         __no_operation();
         cycles--;
     }
 }


#if TIKU_DEVICE_HAS_LFXT
/**
 * @brief Disables the LFXT (XT1 @ 32.768 kHz) oscillator.
 *        If ACLK is using LFXT, re-route it to REFO first.
 */
void tiku_cpu_msp430_lfxt_disable(void)
{
    // Unlock CS
    TIKU_CS_UNLOCK();

    CPU_FREQ_PRINTF("Disabling LFXT\n");

    // Turn off LFXT
    CSCTL4 |= LFXTOFF;

    // Clear any lingering oscillator fault flags
    CSCTL5 &= ~LFXTOFFG;
    SFRIFG1 &= ~OFIFG;

    // Lock CS
    TIKU_CS_LOCK();

}
#else
void tiku_cpu_msp430_lfxt_disable(void)
{
    /* No LFXT on this device — nothing to do */
}
#endif /* TIKU_DEVICE_HAS_LFXT */


 /**
  * @brief MSP430-specific CPU initialization
  * Interface to the external world for initializing the clock frequency and other settings
  */
void tiku_cpu_boot_msp430_init(void)
 {
     CPU_FREQ_PRINTF("Booting up CPU\n");

     // Disable global interrupts first
     CPU_FREQ_PRINTF("Disabling global interrupts\n");
     tiku_cpu_boot_msp430_global_interrupts_disable();

     CPU_FREQ_PRINTF("Initializing all pins as outputs and driven low\n");
     // Initialize all pins as outputs and driven low
     tiku_cpu_boot_msp430_pins_init_low();

     // ADD: Other code here if needed

     // Unlock PM5 module BEFORE clock configuration (per TI reference)
     // This activates previously configured port settings
     PM5CTL0 &= ~LOCKLPM5;

     /* Do NOT enable global interrupts here.  The scheduler loop
      * (tiku_sched_loop) enables GIE at the correct time, after
      * autostart processes have been launched and the event queue
      * is ready.  Enabling GIE prematurely causes timer ISRs to
      * fire before the process system is initialized, flooding the
      * event queue with unhandled POLL events. */

     CPU_FREQ_PRINTF("Bootup completed\n");

 }


/**
 * @brief MSP430-specific frequency initialization
 * @param freq_mhz The desired MCLK frequency in MHz
 * @param sfreq_div The SMCLK divider value
 * @param enable_lfxt_crystal Whether to enable LFXT crystal
 * @param enable_hfxt_crystal Whether to enable HFXT crystal
 * Internal function for initializing the clock frequency and other settings. Not exposed to the external world.
 */

static void cpu_freq_msp430_init(unsigned int freq_mhz, unsigned int sfreq_div, bool enable_lfxt_crystal, bool enable_hfxt_crystal)
 {
     (void)enable_hfxt_crystal; /* Reserved for future use */

     CPU_FREQ_PRINTF("Initializing CPU frequency: %s MHz\n", tiku_cpu_freq_to_mhz_str(freq_mhz));

     /* Validate frequency */
     if (freq_mhz > CPU_FREQ_MAX_MHZ) {

         freq_mhz = CPU_FREQ_8MHZ;

         CPU_FREQ_PRINTF("Frequency clamped to safe value: %s MHz\n", tiku_cpu_freq_to_mhz_str(freq_mhz));

        }

#if TIKU_DEVICE_HAS_LFXT
    if (enable_lfxt_crystal) {

    CPU_FREQ_PRINTF("Initializing Microcontroller with LFXT crystal. Sfreq divider: %d\n", sfreq_div);

     /* Initialize LFXT */
     tiku_cpu_msp430_lfxt_init(false);
     /* Set clock frequency */
     tiku_cpu_msp430_clock_set_advanced(freq_mhz, sfreq_div, TIKU_ACLK_LFXT);

     // Wait for clock to stabilize
     tiku_cpu_msp430_delay_cycles(1000000);

     // Check if clock is stable
     if (!wait_for_lfxt_fault_clear(CLOCK_FAULT_TIMEOUT)) {

        CPU_FREQ_PRINTF("LFXT crystal fault detected. Disabling crystal.\n");

        // Disable crystal
        tiku_cpu_msp430_lfxt_disable();

      }
    }
    else
#else
    (void)enable_lfxt_crystal;
#endif /* TIKU_DEVICE_HAS_LFXT */
    {

        CPU_FREQ_PRINTF("Initializing Microcontroller without LFXT crystal. Sfreq divider: %d\n", sfreq_div);

        /* Set clock frequency with REFO as ACLK source (available on all devices) */
        tiku_cpu_msp430_clock_set_advanced(freq_mhz, sfreq_div, TIKU_ACLK_REFO);

    }

    CPU_FREQ_PRINTF("CPU frequency initialization completed\n");
 }

/*
* Functions to return the clock frequency and other settings
*
*/

/**
 * @brief Gets the current CPU frequency in MHz.
 *
 * @return The CPU frequency in MHz.
 */
tiku_clk_freq_t tiku_cpu_msp430_clock_get_freq(void)
 {

    return clk.freq;

}

/**
 * @brief Gets the current SMCLK divider.
 *
 * @return The SMCLK divider.
 */
tiku_clk_div_t tiku_cpu_msp430_clock_get_sfreq_div(void)
 {
    return clk.smclk_div;
 }

/**
 * @brief Gets   the current ACLK source.
 *
 * @return The ACLK source.
 */
tiku_aclk_source_t tiku_cpu_msp430_clock_get_aclk_source(void)
{
    return clk.aclk_src;
}

/**
 * @brief Initialize CPU frequency on MSP430
 * @param freq_mhz Desired frequency in MHz
 */
void tiku_cpu_freq_msp430_init(unsigned int freq_mhz)
{
    cpu_freq_msp430_init(freq_mhz, TIKU_CLK_DIV_1, false, false);
}

unsigned long tiku_cpu_msp430_clock_get_hz(void)
{
    return g_mclk_hz;
}

unsigned long tiku_cpu_msp430_aclk_get_hz(void)
{
    return g_aclk_hz;
}

unsigned long tiku_cpu_msp430_smclk_get_hz(void)
{
    return g_smclk_hz;
}


