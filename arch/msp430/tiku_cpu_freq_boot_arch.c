/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - MSP430 CPU frequency configuration.
 *
 * Clock-system setup plus the frequency set and get paths, for the CS_A
 * module of the FR5969, FR5994 and FR6989 and the clock system of the FR2433.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_platform.h"
#include "tiku_cpu_freq_boot_arch.h"

/*---------------------------------------------------------------------------*/
/* CS MODULE ABSTRACTION MACROS                                              */
/*---------------------------------------------------------------------------*/

/* Writing CSKEY_H to CSCTL0_H unlocks the CS registers and writing 0 locks
 * them; a CS without a key gets empty macros. */
#if TIKU_DEVICE_CS_HAS_KEY
#define TIKU_CS_UNLOCK()    do { CSCTL0_H = CSKEY_H; } while(0)
#define TIKU_CS_LOCK()      do { CSCTL0_H = 0; } while(0)
#else
#define TIKU_CS_UNLOCK()    do { } while(0)
#define TIKU_CS_LOCK()      do { } while(0)
#endif

/* Clock settings as last applied, returned by the getters below. */
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

/* Clock frequency cache, documented in tiku_cpu_freq_boot_arch.h. */
volatile unsigned long g_mclk_hz = 0;         /* MCLK frequency in Hz */
volatile unsigned long g_smclk_hz = 0;        /* SMCLK frequency in Hz */
volatile unsigned long g_aclk_hz = 0;         /* ACLK frequency in Hz */
volatile unsigned long g_vlo_hz = 0;          /* Never measured: stays 0 */
volatile unsigned long g_xt1_hz = 0;          /* Crystal frequency if enabled */
volatile bool g_xt1_enabled = false;         /* XT1 oscillator status */
volatile bool g_clock_initialized = false;   /* Initialization status */

/* Called when a crystal fails to start; NULL for none. */
static void (*g_fault_handler)(unsigned int) = NULL;

static void cpu_freq_msp430_init(unsigned int freq_mhz, unsigned int sfreq_div, bool enable_lfxt_crystal, bool enable_hfxt_crystal);

/**
 * @brief Make every pin of each port the device has an output driven low.
 *
 * No pin is left as a floating input, and PnIE is cleared wherever the port
 * has interrupt capability.
 */

 void tiku_cpu_boot_msp430_pins_init_low(void)
 {
    /* TIKU_DEVICE_HAS_PORTn picks the ports.  PnIE is cleared only where
     * <msp430.h> defines it: the FR2433 P3, for one, has no P3IE. */

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
  * @brief Enters Low Power Mode 0, with GIE set.
  *
  * The CPU and MCLK stop; SMCLK and ACLK keep running.
  */
 void tiku_cpu_boot_msp430_power_lpm0_enter(void)
 {

     __bis_SR_register(LPM0_bits | GIE);

     __no_operation();

}

 /**
  * @brief Enters Low Power Mode 3, with GIE set.
  *
  * The CPU, MCLK, SMCLK and the DCO stop; ACLK keeps running.
  */
 void tiku_cpu_boot_msp430_power_lpm3_enter(void)
 {

     __bis_SR_register(LPM3_bits | GIE);

     __no_operation();

    }

 /**
  * @brief Enters Low Power Mode 4, with GIE set.
  *
  * The CPU and every clock stop.
  */
 void tiku_cpu_boot_msp430_power_lpm4_enter(void)
 {

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

     /* PMMPW unlocks PMMCTL0; PMMSWPOR triggers a software POR. */

     PMMCTL0 = PMMPW | PMMSWPOR;

 }

 /*---------------------------------------------------------------------------*/
 /* OSCILLATOR FAULT HANDLING                                                 */
 /*---------------------------------------------------------------------------*/

#if TIKU_DEVICE_HAS_LFXT
/**
 * @brief Clear the LFXT fault flag and OFIFG until LFXTOFFG stays clear.
 *
 * Other oscillator fault flags are not checked.
 *
 * @param timeout Clear attempts before giving up
 * @return true once LFXT runs, false on timeout
 */
static bool wait_for_lfxt_fault_clear(unsigned long timeout)
{
    unsigned long count = 0;

    TIKU_CS_UNLOCK();

    do {
        CSCTL5 &= ~LFXTOFFG;

        /* OFIFG summarises every oscillator fault. */
        SFRIFG1 &= ~OFIFG;

        if (++count > timeout) {
            CPU_FREQ_PRINTF("Timeout reached while waiting for LFXT fault clear\n");

            TIKU_CS_LOCK();

            return false;  /* Timeout */
        }

    /* Loops on LFXTOFFG alone; OFIFG may stay set for another source. */
    } while (CSCTL5 & LFXTOFFG);

    TIKU_CS_LOCK();

    CPU_FREQ_PRINTF("CPU_FREQ: LFXT fault cleared\n");

    return true;  /* Success: XT1 is stable */
}
#endif /* TIKU_DEVICE_HAS_LFXT */

#if TIKU_DEVICE_HAS_HFXT
/**
 * @brief Clear the HFXT fault flag and OFIFG until HFXTOFFG stays clear.
 *
 * Other oscillator fault flags are not checked.
 *
 * @param timeout Clear attempts before giving up
 * @return true once HFXT runs, false on timeout
 */
static bool wait_for_hfxt_fault_clear(unsigned long timeout)
{
    unsigned long count = 0;

    TIKU_CS_UNLOCK();

    do {
        CSCTL5 &= ~HFXTOFFG;

        /* OFIFG summarises every oscillator fault. */
        SFRIFG1 &= ~OFIFG;

        if (++count > timeout) {

            CPU_FREQ_PRINTF("Timeout reached while waiting for HFXT fault clear\n");

            TIKU_CS_LOCK();

            return false;  /* Timeout */
        }

    /* Loops on HFXTOFFG alone; OFIFG may stay set for another source. */
    } while (CSCTL5 & HFXTOFFG);

    TIKU_CS_LOCK();

    return true;  /* Success: HFXT is stable */
}
#endif /* TIKU_DEVICE_HAS_HFXT */

/**
 * @brief Clear every oscillator fault flag until OFIFG stays clear.
 *
 * @param timeout Clear attempts before giving up
 * @return true once OFIFG stays clear, false on timeout
 */
static bool wait_for_all_fault_clear(unsigned long timeout)
{
    unsigned long count = 0;

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

        SFRIFG1 &= ~OFIFG;                 /* summary fault flag */

        if (++count > timeout) {

            CPU_FREQ_PRINTF("Timeout reached while waiting for all fault clear\n");

            TIKU_CS_LOCK();

            return false;

        }

    } while (SFRIFG1 & OFIFG);             /* until OFIFG stays clear */

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
  * @param freq MCLK frequency code (CPU_FREQ_*), not MHz.
  * @param sfreq_div The divider for SMCLK.
  * @param aclk_source The source for ACLK.
  */

 void tiku_cpu_msp430_clock_set_advanced(tiku_clk_freq_t freq,
                                         tiku_clk_div_t sfreq_div,
                                         tiku_aclk_source_t aclk_source)
 {
#if defined(TIKU_DEVICE_CS_TYPE_FR2X33)
     aclk_source = TIKU_ACLK_REFO;
#else
     if (aclk_source == TIKU_ACLK_REFO || aclk_source == TIKU_ACLK_DCO) {
         aclk_source = TIKU_ACLK_LFMODCLK;
     }
#endif
     /* Validate input parameters */
     if (freq < CPU_FREQ_MIN_MHZ || freq > CPU_FREQ_8MHZ) {
         freq = CPU_FREQ_8MHZ;  /* Default/clamp to 8MHz */
     }

     /* FRAM wait states are set before the clocks change.  Up to 8 MHz, the
      * only range this function selects, FRAM needs none.
      */
     CPU_FREQ_PRINTF("Configuring FRAM wait states for %s MHz\n", tiku_cpu_freq_to_mhz_str(freq));

     /* FRCTLPW (0xA500) unlocks FRCTL0. */
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
      * FR2433 CS: the FLL locks DCOCLKDIV to 32768 * (FLLN + 1) Hz from REFO
      * within the DCO range DCORSEL selects; SLAU445 gives the ranges
      * 0..7 = 1, 2, 4, 8, 12, 16, 20 and 24 MHz. Each target uses the
      * nearest supported range at or above its nominal rate. SCG0 holds the
      * FLL off while CSCTL1 and CSCTL2 change.
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
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_2;
            CSCTL2 = FLLD_0 + 81;   /* 32768*(81+1) = 2,686,976 Hz */
            clk.freq = TIKU_CLK_FREQ_2_677MHZ;
            break;
        case TIKU_CLK_FREQ_3_5MHZ:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_2;
            CSCTL2 = FLLD_0 + 106;  /* 32768*(106+1) = 3,506,176 Hz */
            clk.freq = TIKU_CLK_FREQ_3_5MHZ;
            break;
        case TIKU_CLK_FREQ_4MHZ:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_2;
            CSCTL2 = FLLD_0 + 121;  /* 32768*(121+1) = 3,997,696 Hz */
            clk.freq = TIKU_CLK_FREQ_4MHZ;
            break;
        case TIKU_CLK_FREQ_5_33MHZ:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_3;
            CSCTL2 = FLLD_0 + 162;  /* 32768*(162+1) = 5,341,184 Hz */
            clk.freq = TIKU_CLK_FREQ_5_33MHZ;
            break;
        case TIKU_CLK_FREQ_7MHZ:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_3;
            CSCTL2 = FLLD_0 + 213;  /* 32768*(213+1) = 7,012,352 Hz */
            clk.freq = TIKU_CLK_FREQ_7MHZ;
            break;
        case TIKU_CLK_FREQ_8MHZ:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_3;
            CSCTL2 = FLLD_0 + 243;  /* 32768*(243+1) = 7,995,392 Hz */
            clk.freq = TIKU_CLK_FREQ_8MHZ;
            break;
        default:
            CSCTL1 = DCOFTRIMEN | DCOFTRIM_3 | DCORSEL_3;
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

     /* SCG0 holds the FLL off again, so the DCO stays at the locked tap; a
      * running FLL keeps retuning the DCO, and that jitter corrupts UART
      * bytes. */
     __bis_SR_register(SCG0);

#else
     /* FR5969/FR5994/FR6989 CS_A: the DCO is set by DCOFSEL and DCORSEL.
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
                /* SELA selects XT1CLK or REFOCLK on this part; every
                 * request gets REFOCLK. */
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
     /* FR5969/FR5994/FR6989 CS_A:
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
            case TIKU_ACLK_LFMODCLK:
                sela = MSP430_SELA__LFMODCLK;
                break;
            case TIKU_ACLK_LFXT:
                sela = MSP430_SELA__XT1CLK;
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
         case TIKU_ACLK_LFMODCLK: g_aclk_hz = 39062UL; break;
         case TIKU_ACLK_LFXT: g_aclk_hz = XT1_FREQ_32KHZ; g_xt1_hz = XT1_FREQ_32KHZ; break;
         case TIKU_ACLK_DCO:  g_aclk_hz = g_mclk_hz; break;
         default:             g_aclk_hz = VLO_FREQ_NOMINAL_HZ; break;
     }
     clk.aclk_src = aclk_source;
     g_clock_initialized = true;

     /* Clear any oscillator fault flags before locking */
#if TIKU_DEVICE_HAS_LFXT && TIKU_DEVICE_HAS_HFXT
     CSCTL5 &= ~(LFXTOFFG | HFXTOFFG);
#elif TIKU_DEVICE_HAS_LFXT
     CSCTL5 &= ~LFXTOFFG;
#elif !defined(TIKU_DEVICE_CS_TYPE_FR2X33)
     /* CS_A part without LFXT: OFIFG only */
     SFRIFG1 &= ~OFIFG;
#else
     /* FR2433: clear DCO fault flag */
     CSCTL7 &= ~DCOFFG;
#endif
     SFRIFG1 &= ~OFIFG;

     /* Lock CS registers */
     TIKU_CS_LOCK();

     /* Settling time after the switch */
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
 * @param bypass true: external clock on LFXIN; false: crystal
 * @return TIKU_CLOCK_OK, or TIKU_CLOCK_FAULT_XT1 if LFXT does not start
 */
 tiku_clock_result_t tiku_cpu_msp430_lfxt_init(bool bypass)
 {
     unsigned int count = 0;

     TIKU_CS_UNLOCK();

     /* Route the LFXT pins (device header). */
     TIKU_DEVICE_LFXT_PSEL_REG |= TIKU_DEVICE_LFXT_PSEL_BITS;

     TIKU_DEVICE_LFXT_PSEL1_REG &= ~TIKU_DEVICE_LFXT_PSEL1_BITS;

     if (bypass) {

        CPU_FREQ_PRINTF("LFXT bypassed\n");
        CSCTL4 |= LFXTBYPASS;      /* external clock on LFXIN */

    } else {

        clk.aclk_src = TIKU_ACLK_LFXT;
        CPU_FREQ_PRINTF("LFXT crystal mode\n");
        CSCTL4 &= ~LFXTBYPASS;     /* crystal mode */

    }

     /* LFXT on, at full drive for start-up. */
     CSCTL4 &= ~LFXTOFF;

     CSCTL4 = (CSCTL4 & ~LFXTDRIVE_3) | LFXTDRIVE_3;

     /* Clear the fault flags until they stay clear, or time out. */
     do {

        CSCTL5 &= ~LFXTOFFG;       /* LFXT fault */
        SFRIFG1 &= ~OFIFG;         /* summary fault */
        __delay_cycles(10000);     /* settle */

        if (++count > CLOCK_FAULT_TIMEOUT) {

            /* Give up: LFXT off, CS locked. */
             tiku_cpu_msp430_lfxt_disable();

             TIKU_CS_LOCK();
             CPU_FREQ_PRINTF("LFXT fault detected\n");

             g_xt1_enabled = false;
             if (g_fault_handler) {
                 g_fault_handler(TIKU_CLOCK_FAULT_XT1);
             }
             return TIKU_CLOCK_FAULT_XT1;
         }
     } while (SFRIFG1 & OFIFG);

     /* Lowest drive once stable: less supply current. */
     CSCTL4 = (CSCTL4 & ~LFXTDRIVE_3) | LFXTDRIVE_0;

     TIKU_CS_LOCK();

     g_xt1_enabled = true;
     g_xt1_hz = XT1_FREQ_32KHZ;

     CPU_FREQ_PRINTF("LFXT initialized successfully\n");

     return TIKU_CLOCK_OK;
 }
#else
/* No LFXT on this device: every call returns TIKU_CLOCK_FAULT_XT1. */
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
 * @param bypass true: external clock on HFXIN; false: crystal
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
     unsigned int attempts = 1000U;
     CPU_FREQ_PRINTF("Clearing oscillator fault flags\n");

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
         SFRIFG1 &= ~OFIFG;                      /* summary fault */

         CPU_FREQ_PRINTF("Clearing fault flags (SFRIFG1=0x%x)\n", SFRIFG1);
     } while ((SFRIFG1 & OFIFG) && --attempts != 0U);

     TIKU_CS_LOCK();

     CPU_FREQ_PRINTF("Fault clear finished (SFRIFG1=0x%x)\n", SFRIFG1);
 }

 /**
  * @brief Sets a custom clock fault handler.
  */
 void tiku_cpu_msp430_clock_set_fault_handler(void (*handler)(unsigned int fault_type))
 {
     g_fault_handler = handler;
 }

 /**
  * @brief Spin for @p cycles loop iterations, several CPU cycles each.
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
 *        If ACLK uses LFXT, select LFMODCLK before disabling the crystal.
 */
void tiku_cpu_msp430_lfxt_disable(void)
{
    TIKU_CS_UNLOCK();

    CPU_FREQ_PRINTF("Disabling LFXT\n");

    if ((CSCTL2 & SELA_7) == MSP430_SELA__XT1CLK) {
        CSCTL2 = (CSCTL2 & ~SELA_7) | MSP430_SELA__LFMODCLK;
        clk.aclk_src = TIKU_ACLK_LFMODCLK;
        g_aclk_hz = 39062UL;
    }
    CSCTL4 |= LFXTOFF;

    /* Clear any fault flags left set. */
    CSCTL5 &= ~LFXTOFFG;
    SFRIFG1 &= ~OFIFG;

    TIKU_CS_LOCK();

}
#else
void tiku_cpu_msp430_lfxt_disable(void)
{
    /* No LFXT on this device: nothing to turn off. */
}
#endif /* TIKU_DEVICE_HAS_LFXT */


 /**
  * @brief Boot-time CPU setup: interrupts off, every pin low, LOCKLPM5 clear.
  */
void tiku_cpu_boot_msp430_init(void)
 {
     CPU_FREQ_PRINTF("Booting up CPU\n");

     CPU_FREQ_PRINTF("Disabling global interrupts\n");
     tiku_cpu_boot_msp430_global_interrupts_disable();

     CPU_FREQ_PRINTF("Initializing all pins as outputs and driven low\n");
     tiku_cpu_boot_msp430_pins_init_low();

     /* Clearing LOCKLPM5 applies the port settings above to the pins; clock
      * setup runs after this. */
     PM5CTL0 &= ~LOCKLPM5;

     /* This function leaves interrupts disabled; tiku_sched_loop() enables
      * GIE after it starts the autostart processes. */

     CPU_FREQ_PRINTF("Bootup completed\n");

 }


/**
 * @brief MSP430-specific frequency initialization
 * @param freq_mhz MCLK frequency code (CPU_FREQ_*), not MHz
 * @param sfreq_div The SMCLK divider value
 * @param enable_lfxt_crystal Whether to start the LFXT crystal
 * @param enable_hfxt_crystal Ignored
 */

static void cpu_freq_msp430_init(unsigned int freq_mhz, unsigned int sfreq_div, bool enable_lfxt_crystal, bool enable_hfxt_crystal)
 {
     (void)enable_hfxt_crystal; /* no HFXT start-up here */

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

     /* Settling time for the clock. */
     tiku_cpu_msp430_delay_cycles(1000000);

     /* LFXT still faulting after the wait: turn it off. */
     if (!wait_for_lfxt_fault_clear(CLOCK_FAULT_TIMEOUT)) {

        CPU_FREQ_PRINTF("LFXT crystal fault detected. Disabling crystal.\n");

        tiku_cpu_msp430_lfxt_disable();

      }
    }
    else
#else
    (void)enable_lfxt_crystal;
#endif /* TIKU_DEVICE_HAS_LFXT */
    {

        CPU_FREQ_PRINTF("Initializing Microcontroller without LFXT crystal. Sfreq divider: %d\n", sfreq_div);

        /* REFO on FR2433; LFMODCLK on CS_A parts. */
        tiku_cpu_msp430_clock_set_advanced(freq_mhz, sfreq_div, TIKU_ACLK_REFO);

    }

    CPU_FREQ_PRINTF("CPU frequency initialization completed\n");
 }

/* Getters for the cached clock settings. */

/**
 * @brief Gets the MCLK frequency code last set.
 *
 * @return The TIKU_CLK_FREQ_* code, not MHz.
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
 * @brief Gets the current ACLK source.
 *
 * @return The ACLK source.
 */
tiku_aclk_source_t tiku_cpu_msp430_clock_get_aclk_source(void)
{
#if defined(TIKU_DEVICE_CS_TYPE_FR2X33)
    return (CSCTL4 & SELA__REFOCLK) ? TIKU_ACLK_REFO : TIKU_ACLK_LFXT;
#else
    switch (CSCTL2 & SELA_7) {
    case MSP430_SELA__XT1CLK: return TIKU_ACLK_LFXT;
    case MSP430_SELA__LFMODCLK: return TIKU_ACLK_LFMODCLK;
    default: return TIKU_ACLK_VLO;
    }
#endif
}

void tiku_cpu_boot_msp430_setup(void)
{
    tiku_cpu_boot_msp430_init();
}

/**
 * @brief Initialize CPU frequency on MSP430
 * @param freq_mhz MCLK frequency code (CPU_FREQ_*), not MHz
 */
void tiku_cpu_freq_msp430_init(unsigned int freq_mhz)
{
    cpu_freq_msp430_init(freq_mhz, TIKU_CLK_DIV_1, false, false);
}

/** @brief Divide only MCLK, keeping the 8 MHz DCO behind SMCLK. */
void tiku_cpu_msp430_boot_divide(unsigned long hz)
{
    unsigned int div;
    if (g_smclk_hz != 8000000UL) return;
    switch (hz) {
    case 8000000UL: div = 0; break;
    case 4000000UL: div = 1; break;
    case 2000000UL: div = 2; break;
    case 1000000UL: div = 3; break;
    default: return;
    }
    TIKU_CS_UNLOCK();
#if defined(TIKU_DEVICE_CS_TYPE_FR2X33)
    CSCTL5 = (CSCTL5 & ~0x0007U) | div;
#else
    CSCTL3 = (CSCTL3 & ~0x0007U) | div;
#endif
    TIKU_CS_LOCK();
    g_mclk_hz = hz;
}

unsigned long tiku_cpu_msp430_clock_get_hz(void)
{
    return g_mclk_hz;
}

unsigned long tiku_cpu_msp430_aclk_get_hz(void)
{
    switch (tiku_cpu_msp430_clock_get_aclk_source()) {
    case TIKU_ACLK_REFO: return REFO_FREQ_HZ;
    case TIKU_ACLK_LFXT: return XT1_FREQ_32KHZ;
    case TIKU_ACLK_LFMODCLK: return 39062UL;
    default: return VLO_FREQ_NOMINAL_HZ;
    }
}

unsigned long tiku_cpu_msp430_smclk_get_hz(void)
{
    return g_smclk_hz;
}
