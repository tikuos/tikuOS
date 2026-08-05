/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ra8p1_regs.h - RA8P1 register bases and the few bits the port needs.
 *
 * Every address here is from the RA8P1 Group User's Manual: Hardware
 * (R01UH1064EJ0130) or the Group Datasheet (R01DS0439EJ0110); the section is
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_REGS_H_
#define TIKU_RA8P1_REGS_H_

#include <stdint.h>

/** @brief 32-bit MMIO accessor; volatile so the compiler keeps every access. */
#define TIKU_REG32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
/** @brief 8-bit MMIO accessor, for the byte-wide protect registers. */
#define TIKU_REG8(a)    (*(volatile uint8_t *)(uintptr_t)(a))
/** @brief 16-bit MMIO accessor; the watchdogs insist on halfword access. */
#define TIKU_REG16(a)   (*(volatile uint16_t *)(uintptr_t)(a))

/*---------------------------------------------------------------------------*/
/* System control (UM 9, "Clock Generation Circuit")                         */
/*---------------------------------------------------------------------------*/
#define RA8P1_SYSC_BASE         0x4001E000UL
#define RA8P1_SCKDIVCR          (RA8P1_SYSC_BASE + 0x020UL)  /* UM 9.2.2  */
#define RA8P1_SCKSCR            (RA8P1_SYSC_BASE + 0x026UL)  /* UM 9.2.5  */

/*
 * Clock registers are write-protected: PRCR_S.PRC0 must be 1, and every write
 * to PRCR_S itself must carry the key in the top byte or it is ignored.  A
 * missing unlock does not fault -- the write is simply dropped, and the
 * symptom is an oscillator that never starts.
 */
#define RA8P1_PRCR_S            (RA8P1_SYSC_BASE + 0x3FAUL)  /* 16-bit    */
#define RA8P1_PRCR_KEY          0xA500U
#define RA8P1_PRCR_PRC0         (1U << 0)   /* clock generation circuit    */

/* Main clock oscillator: the board's 24 MHz crystal (UM 9). */
#define RA8P1_MOSCCR            (RA8P1_SYSC_BASE + 0x032UL)  /* 8-bit     */
#define RA8P1_MOSCCR_MOSTP      (1U << 0)   /* 1 = stopped                 */
#define RA8P1_MOSCWTCR          (RA8P1_SYSC_BASE + 0x0A2UL)  /* 8-bit     */
#define RA8P1_OSCSF             (RA8P1_SYSC_BASE + 0x03CUL)  /* 8-bit     */
#define RA8P1_OSCSF_HOCOSF      (1U << 0)
#define RA8P1_OSCSF_MOSCSF      (1U << 3)
#define RA8P1_OSCSF_PLLSF       (1U << 5)
#define RA8P1_MOMCR             (RA8P1_SYSC_BASE + 0xA50UL)  /* 8-bit     */
#define RA8P1_MOMCR_DRV_24MHZ   (0x3U << 1)  /* MODRV0 = 011: 8-24 MHz     */
#define RA8P1_MOMCR_MOSEL_EXT   (1U << 6)    /* 0 = resonator              */

/*
 * SCKDIVCR packs one 4-bit divider code per clock domain.  Codes 0..6 are
 * 2^code and codes 8..11 are 3, 6, 12, 24 -- the odd divisors are NOT a
 * continuation of the power-of-two run, which is the trap in reading this
 * register as a shift count.
 */
#define RA8P1_SCKDIVCR_PCKD_SHIFT   0U
#define RA8P1_SCKDIVCR_PCKC_SHIFT   4U
#define RA8P1_SCKDIVCR_PCKB_SHIFT   8U
#define RA8P1_SCKDIVCR_PCKA_SHIFT   12U
#define RA8P1_SCKDIVCR_BCK_SHIFT    16U
#define RA8P1_SCKDIVCR_PCKE_SHIFT   20U
#define RA8P1_SCKDIVCR_ICK_SHIFT    24U
#define RA8P1_SCKDIVCR_MRPCK_SHIFT  28U
#define RA8P1_SCKSCR_CKSEL_MASK     0x07U

/*
 * SCKDIVCR2 carries the clocks SCKDIVCR does not: the two CPU clocks, the NPU
 * and the MRAM instruction bus.  CPUCLK0 is NOT the ICK field -- the core can
 * run at 1 GHz while ICLK is capped at 250 MHz, so they are separate knobs and
 * treating ICK as the core clock would cap the part at a quarter of its rate.
 */
#define RA8P1_SCKDIVCR2             (RA8P1_SYSC_BASE + 0x024UL)
#define RA8P1_SCKDIVCR2_CPUCK0_SHIFT  0U
#define RA8P1_SCKDIVCR2_CPUCK1_SHIFT  4U
#define RA8P1_SCKDIVCR2_NPUCK_SHIFT   8U
#define RA8P1_SCKDIVCR2_MRICK_SHIFT   12U

/* PLL1 (UM 9.2.6-9.2.8).  PLLMUL is the multiplier minus one: 0x27 is x40. */
#define RA8P1_PLLCCR                (RA8P1_SYSC_BASE + 0x0ACUL)  /* 32-bit */
#define RA8P1_PLLCCR2               (RA8P1_SYSC_BASE + 0x04CUL)  /* 16-bit */
#define RA8P1_PLLCR                 (RA8P1_SYSC_BASE + 0x02AUL)  /* 8-bit  */
#define RA8P1_PLLCR_PLLSTP          (1U << 0)   /* 1 = stopped             */
#define RA8P1_PLLCCR_PLIDIV(n)      (((uint32_t)(n) & 0x3UL) << 0)
#define RA8P1_PLLCCR_SRC_HOCO       (1UL << 4)  /* 0 = main oscillator     */
#define RA8P1_PLLCCR_PLLMUL(m)      ((((uint32_t)(m) - 1UL) & 0x1FFUL) << 8)
#define RA8P1_PLLCCR2_PLODIVP(c)    (((uint16_t)(c) & 0xFU) << 0)
#define RA8P1_PLLCCR2_PLODIVQ(c)    (((uint16_t)(c) & 0xFU) << 4)
#define RA8P1_PLLCCR2_PLODIVR(c)    (((uint16_t)(c) & 0xFU) << 8)
/** @brief PLL output-divider codes; note these are NOT the SCKDIVCR set. */
#define RA8P1_PLODIV_2              0x1U
#define RA8P1_PLODIV_4              0x3U
#define RA8P1_PLODIV_6              0x5U

/*
 * MRAM must be TOLD the frequency it is about to run at, before the clock
 * rises: MRCMHZ picks the read wait states (<=100 MHz none, <=200 one,
 * <=250 two).  Writes need the key in the top byte and must be 32-bit.
 */
#define RA8P1_MRCFREQ               0x4013C004UL
#define RA8P1_MRCFREQ_KEY           (0x1EUL << 24)

/*
 * The SCI has its OWN clock, SCICLK -- it is NOT PCLKA.  Both are MOCO at /1
 * out of reset, which is why a PCLKA-derived baud divisor works at boot and
 * then silently does not once the tree moves: SCICKCR keeps the console on
 * MOCO until it is told otherwise.  Switching needs the request/ready
 * handshake (UM 9.2.54), and the clock is OFF while the request is asserted.
 */
#define RA8P1_SCICKDIVCR        (RA8P1_SYSC_BASE + 0x054UL)  /* 8-bit */
#define RA8P1_SCICKCR           (RA8P1_SYSC_BASE + 0x055UL)  /* 8-bit */
#define RA8P1_SCICKCR_SREQ      (1U << 6)
#define RA8P1_SCICKCR_SRDY      (1U << 7)
#define RA8P1_SCICKSEL_MOCO     0x1U
#define RA8P1_SCICKSEL_PLL1P    0x5U
#define RA8P1_SCICKDIV_1        0x0U
#define RA8P1_SCICKDIV_2        0x1U

/* SRAM needs a wait state once ICLK passes half its 250 MHz maximum. */
#define RA8P1_SRAMWTSC              0x40002008UL
#define RA8P1_SRAMWTSC_WTEN         (1U << 0)

/*
 * Reset status (UM 6.2).  Note the offsets are NOT adjacent: RSTSR1 sits at
 * 0x0C0 while RSTSR0 and RSTSR2 are up at 0xA40/0xA44.
 */
#define RA8P1_RSTSR0            (RA8P1_SYSC_BASE + 0xA40UL)  /* 8-bit  */
#define RA8P1_RSTSR1            (RA8P1_SYSC_BASE + 0x0C0UL)  /* 16-bit */
#define RA8P1_RSTSR2            (RA8P1_SYSC_BASE + 0xA44UL)  /* 8-bit  */
#define RA8P1_RSTSR0_PORF       (1U << 0)
#define RA8P1_RSTSR0_DPSRSTF    (1U << 7)
#define RA8P1_RSTSR1_IWDTRF     (1U << 0)
#define RA8P1_RSTSR1_WDT0RF     (1U << 1)
#define RA8P1_RSTSR1_SWRF       (1U << 2)

/** @brief Unique ID words (UM 60.5.41), four of them, read-only. */
#define RA8P1_UIDR(n)           (0x02F07B00UL + (4UL * (n)))

/*---------------------------------------------------------------------------*/
/* Module stop (UM 11.2, "Module Stop Control Registers")                    */
/*---------------------------------------------------------------------------*/
#define RA8P1_MSTP_BASE         0x40203000UL
#define RA8P1_MSTPCRB           (RA8P1_MSTP_BASE + 0x04UL)   /* UM 11.2.7 */
#define RA8P1_MSTPB_SCI8        (1UL << 23)                  /* SCI8       */

/*---------------------------------------------------------------------------*/
/* I/O ports (UM 21.2)                                                       */
/*                                                                           */
/* PmnPFS is one 32-bit register per pin: 0x40*m + 4*n from the PFS base,     */
/* with m = 0..9, A..D.  PWPR_S gates writes to all of them.                 */
/*---------------------------------------------------------------------------*/
#define RA8P1_PFS_BASE          0x40400800UL
#define RA8P1_PFS(m, n)         (RA8P1_PFS_BASE + (0x40UL * (m)) + (4UL * (n)))
#define RA8P1_PWPR_S            (RA8P1_PFS_BASE + 0x514UL)   /* UM 21.2.8 */
#define RA8P1_PWPR_PFSWE        (1UL << 6)
#define RA8P1_PWPR_B0WI         (1UL << 7)
#define RA8P1_PFS_PSEL_SHIFT    24U                          /* PSEL[28:24] */
#define RA8P1_PFS_PMR           (1UL << 16)                  /* peripheral  */
#define RA8P1_PFS_PDR           (1UL << 2)                   /* 1 = output  */
#define RA8P1_PFS_PODR          (1UL << 0)                   /* output data */
#define RA8P1_PFS_PSEL_SCI      0x04UL                       /* UM Tbl 21.20 */

/*
 * Port control registers, one 0x20 block per port (UM 21.2.1-21.2.3).  These
 * are the same direction and data bits as PmnPFS, reachable a whole port at a
 * time; PCNTR3 in particular gives set and clear as separate write-only
 * fields, so a pin can be driven without a read-modify-write that could race
 * an interrupt touching another pin on the same port.
 */
#define RA8P1_PORT_BASE(m)      (0x40400000UL + (0x20UL * (m)))
#define RA8P1_PORT_PCNTR1(m)    (RA8P1_PORT_BASE(m) + 0x00UL) /* PODR|PDR   */
#define RA8P1_PORT_PCNTR2(m)    (RA8P1_PORT_BASE(m) + 0x04UL) /* EIDR|PIDR  */
#define RA8P1_PORT_PCNTR3(m)    (RA8P1_PORT_BASE(m) + 0x08UL) /* PORR|POSR  */
#define RA8P1_PORT_PODR_SHIFT   16U     /* PCNTR1 output data, bits 31:16   */
#define RA8P1_PORT_PORR_SHIFT   16U     /* PCNTR3 clear,       bits 31:16   */
#define RA8P1_PORT_MAX          0xDU    /* PORTD; m = 0..9, A..D            */

/*---------------------------------------------------------------------------*/
/* SCI_B (UM 39; bases from the datasheet's peripheral map)                   */
/*---------------------------------------------------------------------------*/
#define RA8P1_SCI_BASE(ch)      (0x40358000UL + (0x100UL * (ch)))
#define RA8P1_SCI_RDR(ch)       (RA8P1_SCI_BASE(ch) + 0x00UL)
#define RA8P1_SCI_TDR(ch)       (RA8P1_SCI_BASE(ch) + 0x04UL)
#define RA8P1_SCI_CCR0(ch)      (RA8P1_SCI_BASE(ch) + 0x08UL)
#define RA8P1_SCI_CCR1(ch)      (RA8P1_SCI_BASE(ch) + 0x0CUL)
#define RA8P1_SCI_CCR2(ch)      (RA8P1_SCI_BASE(ch) + 0x10UL)
#define RA8P1_SCI_CCR3(ch)      (RA8P1_SCI_BASE(ch) + 0x14UL)
#define RA8P1_SCI_CSR(ch)       (RA8P1_SCI_BASE(ch) + 0x48UL)
#define RA8P1_SCI_CFCLR(ch)     (RA8P1_SCI_BASE(ch) + 0x68UL)
#define RA8P1_SCI_CFCLR_ORERC   (1UL << 24)
#define RA8P1_SCI_CCR0_RE       (1UL << 0)
#define RA8P1_SCI_CCR0_TE       (1UL << 4)
#define RA8P1_SCI_CCR0_RIE      (1UL << 16)  /* receive interrupt enable */
#define RA8P1_SCI_CSR_ORER      (1UL << 24)
#define RA8P1_SCI_CSR_TDRE      (1UL << 29)
#define RA8P1_SCI_CSR_TEND      (1UL << 30)
#define RA8P1_SCI_CSR_RDRF      (1UL << 31)

/*
 * CCR2 carries the baud divisor.  MDDR (31:24) and BCP (2:0) keep their reset
 * values; ABCSE2/ABCSE/ABCS/BGDM all zero select the x16 base clock, which is
 * what the manual's BRR tables (UM Table 39.11) are computed for.
 */
#define RA8P1_SCI_CCR2_BASE     0xFF000004UL
#define RA8P1_SCI_CCR2_BRR(n)   (((uint32_t)(n) & 0xFFUL) << 8)
#define RA8P1_SCI_CCR2_CKS(n)   (((uint32_t)(n) & 0x03UL) << 20)

/*---------------------------------------------------------------------------*/
/* ICU event link (UM 14.2.17)                                               */
/*                                                                           */
/* Peripheral interrupts do NOT have fixed NVIC slots on this family: any of  */
/* the 96 slots can carry any event, and IELSRn says which.  Nothing reaches  */
/* the NVIC until its slot is linked -- which is why SysTick, a core          */
/* exception, was the tick that needed none of this.                          */
/*---------------------------------------------------------------------------*/
#define RA8P1_ICU_BASE          0x4000C000UL
#define RA8P1_ICU_IELSR(n)      (RA8P1_ICU_BASE + 0x300UL + (4UL * (n)))
#define RA8P1_ICU_IELSR_IR      (1UL << 16)   /* interrupt status, write 0 */

/** @brief Event numbers this port links (UM Table 14.5). */
#define RA8P1_EVENT_SCI8_RXI    0x2FCUL
#define RA8P1_EVENT_SCI8_ERI    0x2FFUL

/*---------------------------------------------------------------------------*/
/* CAC (UM 10, "Clock Frequency Accuracy Measurement Circuit")                */
/*                                                                           */
/* Counts one clock against another entirely on-chip, which is the only way   */
/* to state this port's clock rate without appealing to a host stopwatch.     */
/*---------------------------------------------------------------------------*/
#define RA8P1_CAC_BASE          0x40202400UL
#define RA8P1_CACR0             (RA8P1_CAC_BASE + 0x00UL)   /* 8-bit  */
#define RA8P1_CACR1             (RA8P1_CAC_BASE + 0x01UL)   /* 8-bit  */
#define RA8P1_CACR2             (RA8P1_CAC_BASE + 0x02UL)   /* 8-bit  */
#define RA8P1_CAICR             (RA8P1_CAC_BASE + 0x03UL)   /* 8-bit  */
#define RA8P1_CASTR             (RA8P1_CAC_BASE + 0x04UL)   /* 8-bit  */
#define RA8P1_CAULVR            (RA8P1_CAC_BASE + 0x06UL)   /* 16-bit */
#define RA8P1_CALLVR            (RA8P1_CAC_BASE + 0x08UL)   /* 16-bit */
#define RA8P1_CACNTBR           (RA8P1_CAC_BASE + 0x0AUL)   /* 16-bit */
#define RA8P1_CACR0_CFME        (1U << 0)
#define RA8P1_CACR1_FMCS(n)     (((n) & 0x7U) << 1)   /* target clock  */
/* RPS selects WHERE the reference comes from, and 0 is the external CACREF
 * PIN -- an internal clock needs it set.  Left clear, the measurement simply
 * never completes and the count reads 0. */
#define RA8P1_CACR2_RPS_INT     (1U << 0)
#define RA8P1_CACR2_RSCS(n)     (((n) & 0x7U) << 1)   /* reference     */
#define RA8P1_CACR2_RCDS(n)     (((n) & 0x3U) << 4)   /* ref divider   */
#define RA8P1_CASTR_FERRF       (1U << 0)
#define RA8P1_CASTR_MENDF       (1U << 1)
#define RA8P1_CASTR_OVFF        (1U << 2)
#define RA8P1_CAICR_FERRFCL     (1U << 4)
#define RA8P1_CAICR_MENDFCL     (1U << 5)
#define RA8P1_CAICR_OVFFCL      (1U << 6)
/** @brief CAC clock selects, shared by FMCS and RSCS (UM 10.2.2, 10.2.3). */
#define RA8P1_CAC_CLK_MAIN      0U
#define RA8P1_CAC_CLK_SUB       1U
#define RA8P1_CAC_CLK_HOCO      2U
#define RA8P1_CAC_CLK_MOCO      3U
#define RA8P1_CAC_CLK_LOCO      4U
#define RA8P1_CAC_CLK_PCLKB     5U

#define RA8P1_MSTPCRC           (RA8P1_MSTP_BASE + 0x08UL)  /* UM 11.2.8 */
#define RA8P1_MSTPC_CAC         (1UL << 0)

/*---------------------------------------------------------------------------*/
/* IWDT (UM 29, "Independent Watchdog Timer")                                */
/*                                                                           */
/* Counts IWDTCLK = LOCO/2 = 16.384 kHz, which is why this and not WDT is the */
/* kernel's watchdog: WDT counts PCLKB and would need re-arming every time R4 */
/* moves the clock.  OFS0 reads 0xFFFFFFFF on this board -- MEASURED -- so    */
/* IWDTSTRT is 1: register start mode, counting begins on the first refresh.  */
/*---------------------------------------------------------------------------*/
#define RA8P1_IWDT_BASE         0x40202200UL
#define RA8P1_IWDT_RR           (RA8P1_IWDT_BASE + 0x00UL)  /* 8-bit  */
#define RA8P1_IWDT_CR           (RA8P1_IWDT_BASE + 0x02UL)  /* 16-bit */
#define RA8P1_IWDT_SR           (RA8P1_IWDT_BASE + 0x04UL)  /* 16-bit */
#define RA8P1_IWDT_CR_TOPS(n)   (((uint16_t)(n) & 0x3U) << 0)
#define RA8P1_IWDT_CR_CKS(n)    (((uint16_t)(n) & 0xFU) << 4)
#define RA8P1_IWDT_CR_RPES_NONE (0x3U << 8)   /* no window end   */
#define RA8P1_IWDT_CR_RPSS_NONE (0x3U << 12)  /* no window start */
/** @brief IWDTCLK in Hz: the LOCO, always divided by two (UM 9.10.32). */
#define RA8P1_IWDTCLK_HZ        16384UL

/*---------------------------------------------------------------------------*/
/* Cortex-M85 core peripherals (Armv8.1-M, not part-specific)                */
/*---------------------------------------------------------------------------*/
#define RA8P1_SCB_VTOR          0xE000ED08UL
#define RA8P1_SCB_CPUID         0xE000ED00UL
#define RA8P1_SYST_CSR          0xE000E010UL
#define RA8P1_SYST_RVR          0xE000E014UL
#define RA8P1_SYST_CVR          0xE000E018UL
#define RA8P1_SYST_CSR_ENABLE   (1UL << 0)
#define RA8P1_SYST_CSR_TICKINT  (1UL << 1)
#define RA8P1_SYST_CSR_CLKSOURCE (1UL << 2)   /* processor clock, not ref  */
#define RA8P1_SYST_CSR_COUNTFLAG (1UL << 16)
/* SAU: readable only from the secure state, which makes it the cheapest
 * direct evidence of which world the image is executing in (UM has nothing to
 * say here -- this is Armv8-M architecture, B8.3). */
#define RA8P1_SAU_CTRL          0xE000EDD0UL
#define RA8P1_SAU_TYPE          0xE000EDD4UL
#define RA8P1_NVIC_ISER(i)      (0xE000E100UL + (4UL * (i)))
#define RA8P1_NVIC_ICER(i)      (0xE000E180UL + (4UL * (i)))
#define RA8P1_SCB_AIRCR         0xE000ED0CUL
#define RA8P1_AIRCR_VECTKEY     0x05FA0000UL
#define RA8P1_AIRCR_SYSRESETREQ (1UL << 2)

#endif /* TIKU_RA8P1_REGS_H_ */
