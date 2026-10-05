/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ra8p1_regs.h - RA8P1 register addresses and the fields this port uses.
 *
 * Addresses are from the RA8P1 Group User's Manual: Hardware (R01UH1064EJ0130)
 * or the Group Datasheet (R01DS0439EJ0110), section cited per block; the
 * Cortex-M85 and Ethos-U55 blocks cite Arm's manuals.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_REGS_H_
#define TIKU_RA8P1_REGS_H_

#include <stdint.h>

/** @brief 32-bit MMIO accessor; volatile so the compiler keeps every access. */
#define TIKU_REG32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
/** @brief 8-bit MMIO accessor, for byte-wide registers. */
#define TIKU_REG8(a)    (*(volatile uint8_t *)(uintptr_t)(a))
/** @brief 16-bit MMIO accessor, for halfword registers such as PRCR_S. */
#define TIKU_REG16(a)   (*(volatile uint16_t *)(uintptr_t)(a))

/*---------------------------------------------------------------------------*/
/* SYSTEM CONTROL (UM 9, CLOCK GENERATION CIRCUIT)                           */
/*---------------------------------------------------------------------------*/
#define RA8P1_SYSC_BASE         0x4001E000UL
#define RA8P1_SCKDIVCR          (RA8P1_SYSC_BASE + 0x020UL)  /* UM 9.2.2  */
#define RA8P1_SCKSCR            (RA8P1_SYSC_BASE + 0x026UL)  /* UM 9.2.5  */

/*
 * Clock registers are write-protected: PRCR_S.PRC0 must be 1, and a write to
 * PRCR_S itself is ignored unless it carries the key in the top byte.  A
 * write to a locked register is dropped with no fault.
 */
#define RA8P1_PRCR_S            (RA8P1_SYSC_BASE + 0x3FAUL)  /* 16-bit    */
#define RA8P1_PRCR_KEY          0xA500U
#define RA8P1_PRCR_PRC0         (1U << 0)   /* clock generation circuit    */
#define RA8P1_PRCR_PRC1         (1U << 1)   /* low-power modes, incl. VSCR */

/*
 * Voltage scaling (UM 11.7).  VSCR_1 is 0.95 V and permits the full 1 GHz;
 * VSCR_2, the reset default at 0.925 V, carries up to the 600 MHz class per
 * the ch.70 current tables.  The transition is asynchronous: write VSCM,
 * then wait for VSCMTSF to clear.  The CM85 caches must be off while it is
 * in flight (UM 11.7, stated for TCM and cache both).  PRC1 gates writes.
 */
#define RA8P1_VSCR              (RA8P1_SYSC_BASE + 0x014UL)  /* 8-bit */
#define RA8P1_VSCR_VSCM_MASK    0x07U
#define RA8P1_VSCR_VSCM_1       0x01U        /* 0.95 V: 800 MHz and 1 GHz  */
#define RA8P1_VSCR_VSCM_2       0x02U        /* 0.925 V: reset default     */
#define RA8P1_VSCR_VSCMTSF      (1U << 4)

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
 * SCKDIVCR packs one 4-bit divider code per clock domain.  Codes 0..6 divide
 * by 2^code and codes 8..11 by 3, 6, 12 and 24, so a code is not a shift
 * count.
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
 * and the MRAM instruction bus.  The core runs on CPUCLK0, set here, which
 * reaches 1 GHz; SCKDIVCR's ICK sets ICLK, which is capped at 250 MHz.
 */
#define RA8P1_SCKDIVCR2             (RA8P1_SYSC_BASE + 0x024UL)
#define RA8P1_SCKDIVCR2_CPUCK0_SHIFT  0U
#define RA8P1_SCKDIVCR2_CPUCK1_SHIFT  4U
#define RA8P1_SCKDIVCR2_NPUCK_SHIFT   8U
#define RA8P1_SCKDIVCR2_MRICK_SHIFT   12U

/*
 * PLL1 (UM 9.2.6-9.2.8, Table 9.1).  PLLMUL is the multiplier minus one:
 * 0x27 is x40.  The windows that bound every operating point:
 *
 *   PLL input, after PLIDIV   8 to 24 MHz
 *   multiplier                40 to 300, plus a fraction of 0/0.33/0.5/0.66
 *   VCO                       960 to 2400 MHz
 *   PLL1P out                 60 to 1200 MHz; Table 9.2's note caps any
 *                             PLL used as a clock source at 1 GHz
 *
 * A 24 MHz reference at PLIDIV /1 times the x40 floor is the 960 MHz VCO
 * minimum, and PLODIVP's smallest divisor is /2, so x40 gives 480 MHz at
 * PLL1P.  1 GHz takes a 2000 MHz VCO: 8 MHz in (PLIDIV /3) times 250, out
 * through /2.
 */
#define RA8P1_PLLCCR                (RA8P1_SYSC_BASE + 0x0ACUL)  /* 32-bit */
#define RA8P1_PLLCCR2               (RA8P1_SYSC_BASE + 0x04CUL)  /* 16-bit */
#define RA8P1_PLLCR                 (RA8P1_SYSC_BASE + 0x02AUL)  /* 8-bit  */
#define RA8P1_PLLCR_PLLSTP          (1U << 0)   /* 1 = stopped             */
#define RA8P1_PLLCCR_PLIDIV(n)      (((uint32_t)(n) & 0x3UL) << 0)
#define RA8P1_PLLCCR_SRC_HOCO       (1UL << 4)  /* 0 = main oscillator     */
#define RA8P1_PLLCCR_PLLMULNF(c)    (((uint32_t)(c) & 0x3UL) << 6)
#define RA8P1_PLLCCR_PLLMUL(m)      ((((uint32_t)(m) - 1UL) & 0x1FFUL) << 8)
#define RA8P1_PLLCCR2_PLODIVP(c)    (((uint16_t)(c) & 0xFU) << 0)
#define RA8P1_PLLCCR2_PLODIVQ(c)    (((uint16_t)(c) & 0xFU) << 4)
#define RA8P1_PLLCCR2_PLODIVR(c)    (((uint16_t)(c) & 0xFU) << 8)

/** @brief PLIDIV codes: /1, /2 and /3; there is no /4. */
#define RA8P1_PLIDIV_1              0x0U
#define RA8P1_PLIDIV_2              0x1U
#define RA8P1_PLIDIV_3              0x2U

/** @brief PLLMULNF fraction codes, added to PLLMUL. */
#define RA8P1_PLLMULNF_0            0x0U
#define RA8P1_PLLMULNF_033          0x1U
#define RA8P1_PLLMULNF_066          0x2U
#define RA8P1_PLLMULNF_050          0x3U

/*
 * PLL output-divider codes: a different set from SCKDIVCR's, and not
 * contiguous.  There is no /1: P's smallest divisor is /2, so PLL1P is at
 * most half the VCO.  Q and R also offer /5 and /1.5, which P does not.
 */
#define RA8P1_PLODIV_2              0x1U
#define RA8P1_PLODIV_3              0x2U
#define RA8P1_PLODIV_4              0x3U
#define RA8P1_PLODIV_6              0x5U
#define RA8P1_PLODIV_8              0x7U
#define RA8P1_PLODIV_16             0xFU

/*
 * Clock-tree restrictions (UM Table 9.2 notes).  No register refuses a tree
 * that breaks them and nothing reports it: the part runs out of spec.
 *
 *   ordering   CPUCLK0/CPUCLK1/NPUCLK/MRICLK all >= ICLK
 *              CPUCLK0/CPUCLK1/NPUCLK        all >= MRICLK
 *              ICLK >= MRPCLK, ICLK >= BCLK, ICLK >= PCLKA >= PCLKB
 *              PCLKD >= PCLKA >= PCLKB
 *   ratios     integer N:1 against ICLK (N <= 64); PCLKC/D/E may also be 1:N
 *   mixing     if any SCKDIVCR/SCKDIVCR2 field selects /3 /6 /12 /24, no
 *              other field may select /2 /4 /8 /16 /32 /64
 *   source     PLSRCSEL must be 0 (main oscillator) when CPUCLK0 > 960 MHz
 */

/*
 * MRCFREQ.MRCMHZ tells the MRAM its clock frequency, in whole MHz rounded up,
 * and picks the read wait states: up to 100 MHz none, up to 200 one, up to
 * 250 two.  Values above 250 are reserved, so MRICLK stays at or below
 * 250 MHz.  Writes need the key in the top byte and must be 32-bit.
 *
 * UM 60.4.3: when the clock rises, MRCFREQ is written before the change;
 * when it falls, after.  The prefetch buffer is disabled across the change,
 * and MRCFREQ is read back: a write that did not land leaves the MRAM on too
 * few wait states.
 */
#define RA8P1_MRCPFB                0x4013C000UL /* 8-bit, resets disabled */
#define RA8P1_MRCPFB_MPFBEN         (1U << 0)
#define RA8P1_MRCFREQ               0x4013C004UL
#define RA8P1_MRCFREQ_KEY           (0x1EUL << 24)
#define RA8P1_MREFREQ               0x4013C008UL /* extra MRAM, same form */
#define RA8P1_MREFREQ_KEY           (0xE1UL << 24)
#define RA8P1_MRCFREQ_MHZ_MASK      0x3FFUL
#define RA8P1_MRCFREQ_MHZ_MAX       250UL

/*
 * The SCI runs on its own clock, SCICLK, separate from PCLKA.  Both are MOCO
 * at /1 out of reset, and SCICKCR keeps SCICLK on MOCO when PCLKA changes,
 * so a baud divisor computed from PCLKA is right only until then.  Switching
 * needs the request/ready handshake (UM 9.2.54), and the clock is off while
 * the request is asserted.
 */
#define RA8P1_SCICKDIVCR        (RA8P1_SYSC_BASE + 0x054UL)  /* 8-bit */
#define RA8P1_SCICKCR           (RA8P1_SYSC_BASE + 0x055UL)  /* 8-bit */

/*
 * Octal-SPI clock, a private clock register in the SCICKCR/GTCLKCR family.
 * The OSPI module has no divider of its own, so OCTACLK alone sets the flash
 * bus speed; it comes out of reset on MOCO.  OM_SCLK is OCTACLK divided by
 * two (UM Table 4.4): a 240 MHz OCTACLK gives a 120 MHz OM_SCLK.  PRCR.PRC0
 * gates writes here, as with every clock register.
 */
#define RA8P1_OCTACKDIVCR       (RA8P1_SYSC_BASE + 0x06DUL)  /* 8-bit */
#define RA8P1_OCTACKCR          (RA8P1_SYSC_BASE + 0x075UL)  /* 8-bit */
#define RA8P1_OCTACKCR_SEL_MASK 0x0FU
#define RA8P1_OCTACKCR_SEL_MOCO 0x01U
#define RA8P1_OCTACKCR_SEL_PLL1P 0x05U
#define RA8P1_OCTACKCR_SREQ     (1U << 6)
#define RA8P1_OCTACKCR_SRDY     (1U << 7)
/*
 * SCICKDIVCR and OCTACKDIVCR share one encoding, and it is not monotonic:
 * /4 is 0x2 while /3 is 0x5, so a code cannot be computed from the divisor.
 * Every value is listed here.
 *
 * The ceilings differ (UM Table 9.2): SCICLK 120 MHz, OCTACLK 333.33 MHz.
 * OM_SCLK, which is OCTACLK/2, is capped at 166.67 MHz and by the fitted
 * flash part (133 MHz for the MX25LW).
 */
#define RA8P1_CKDIV_1           0x0U
#define RA8P1_CKDIV_2           0x1U
#define RA8P1_CKDIV_4           0x2U
#define RA8P1_CKDIV_6           0x3U
#define RA8P1_CKDIV_8           0x4U
#define RA8P1_CKDIV_3           0x5U
#define RA8P1_CKDIV_5           0x6U
#define RA8P1_CKDIV_10          0x7U
#define RA8P1_CKDIV_16          0x8U
#define RA8P1_CKDIV_32          0x9U

#define RA8P1_OCTACKDIV_1       0x0U
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
 * Dual-core control (UM 2.9.1).  CPU1 is the Cortex-M33, present only on the
 * K variant of this part (Table 1.15).
 *
 * ACTCSR and CRPT take a 0xA5 key in the upper byte of a 16-bit write; a
 * write without it is discarded with no fault.  CPUWAIT resets to 0, so an
 * activated core begins executing at once, and the bit is sampled only as
 * the core leaves reset: setting it later does not stall a running core.
 * INITVTOR resets to 0x0200_0000, the M85's own vector table, so a CPU1
 * released without setting it runs the M85's image on an M33.
 */
#define RA8P1_CPU_CTRL_BASE     0x4000F000UL
#define RA8P1_CPU1INITVTOR      (RA8P1_CPU_CTRL_BASE + 0x044UL) /* 32-bit */
#define RA8P1_CPU1WAITCR        (RA8P1_CPU_CTRL_BASE + 0x054UL) /*  8-bit */
#define RA8P1_CPU1ACTCSR        (RA8P1_CPU_CTRL_BASE + 0x064UL) /* 16-bit */
#define RA8P1_CPU1CRPT          (RA8P1_CPU_CTRL_BASE + 0x844UL) /* 16-bit */
#define RA8P1_CPUCTRL_KEY       (0xA5U << 8)
#define RA8P1_CPUWAIT           (1U << 0)
#define RA8P1_ACTCSR_ACTREQ     (1U << 0)
#define RA8P1_ACTCSR_ACT        (1U << 7)
#define RA8P1_CRPT_PROTECT      (1U << 0)

/*
 * Inter-processor communication (UM 3).  IPC0* carries CPU1 -> CPU0 and
 * IPC1* the other way; each direction has two four-deep 32-bit FIFOs plus
 * eight software interrupt bits, and IPC_IRQ0 (event 0x05B) is the line the
 * ICU can route to this core's NVIC.
 *
 * STA is read-only: its bits are set through ISET and cleared through CLR.
 * A write to STA clears nothing, and an ISR that clears that way re-enters
 * forever.
 */
#define RA8P1_IPC_BASE          0x40020000UL      /* secure view          */
#define RA8P1_IPC0STA0          (RA8P1_IPC_BASE + 0x0C0UL)  /* CPU1->CPU0 */
#define RA8P1_IPC0ISET0         (RA8P1_IPC_BASE + 0x0C4UL)
#define RA8P1_IPC0TXD0          (RA8P1_IPC_BASE + 0x0C8UL)
#define RA8P1_IPC0RXD0          (RA8P1_IPC_BASE + 0x0CCUL)
#define RA8P1_IPC0CLR0          (RA8P1_IPC_BASE + 0x0D0UL)
#define RA8P1_IPC1STA0          (RA8P1_IPC_BASE + 0x100UL)  /* CPU0->CPU1 */
#define RA8P1_IPC1ISET0         (RA8P1_IPC_BASE + 0x104UL)
#define RA8P1_IPC1TXD0          (RA8P1_IPC_BASE + 0x108UL)
#define RA8P1_IPC1RXD0          (RA8P1_IPC_BASE + 0x10CUL)
#define RA8P1_IPC1CLR0          (RA8P1_IPC_BASE + 0x110UL)
#define RA8P1_IPC_IRQ(n)        (1UL << (n))      /* IRQ7..0, STA/ISET/CLR */
#define RA8P1_IPC_STA_RDY       (1UL << 16)       /* FIFO not empty       */
#define RA8P1_IPC_STA_FULL      (1UL << 17)
#define RA8P1_IPC_STA_RERR      (1UL << 24)       /* read while empty     */
#define RA8P1_IPC_STA_FERR      (1UL << 25)       /* write while full     */
#define RA8P1_IPC_CLR_RCLR      (1UL << 24)
#define RA8P1_IPC_CLR_FCLR      (1UL << 25)

/*
 * Reset status (UM 6.2).  The offsets are not adjacent: RSTSR1 is at 0x0C0,
 * RSTSR0 and RSTSR2 at 0xA40 and 0xA44.
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
/* MODULE STOP (UM 11.2, MODULE STOP CONTROL REGISTERS)                      */
/*---------------------------------------------------------------------------*/
#define RA8P1_MSTP_BASE         0x40203000UL
#define RA8P1_MSTPCRB           (RA8P1_MSTP_BASE + 0x04UL)   /* UM 11.2.7 */
#define RA8P1_MSTPB_SCI8        (1UL << 23)                  /* SCI8       */

/*---------------------------------------------------------------------------*/
/* OCTAL SPI CONTROLLER (UM 45): EK-RA8P1 U3, MX25LW51245G, 64 MB            */
/*---------------------------------------------------------------------------*/
#define RA8P1_OSPI_BASE(n)      (0x40268000UL + (0x400UL * (n)))
#define RA8P1_OSPI_WRAPCFG(n)   (RA8P1_OSPI_BASE(n) + 0x000UL)
#define RA8P1_OSPI_COMCFG(n)    (RA8P1_OSPI_BASE(n) + 0x004UL)
#define RA8P1_OSPI_BMCFG(n, c)  (RA8P1_OSPI_BASE(n) + 0x008UL + (0x4UL * (c)))
#define RA8P1_OSPI_CMCFG0(n, c) (RA8P1_OSPI_BASE(n) + 0x010UL + (0x10UL * (c)))
#define RA8P1_OSPI_CMCFG1(n, c) (RA8P1_OSPI_BASE(n) + 0x014UL + (0x10UL * (c)))
#define RA8P1_OSPI_CMCFG2(n, c) (RA8P1_OSPI_BASE(n) + 0x018UL + (0x10UL * (c)))
#define RA8P1_OSPI_LIOCFG(n, c) (RA8P1_OSPI_BASE(n) + 0x050UL + (0x04UL * (c)))
#define RA8P1_OSPI_CMCTL(n, c)  (RA8P1_OSPI_BASE(n) + 0x068UL + (0x04UL * (c)))

/* Manual command: issues one transaction and reads its reply, with no
 * memory-map mode set up; the device is identified this way. */
#define RA8P1_OSPI_CDCTL0(n)    (RA8P1_OSPI_BASE(n) + 0x070UL)
#define RA8P1_OSPI_CDCTL1(n)    (RA8P1_OSPI_BASE(n) + 0x074UL)
#define RA8P1_OSPI_CDCTL2(n)    (RA8P1_OSPI_BASE(n) + 0x078UL)
#define RA8P1_OSPI_CDTBUF(n, b) (RA8P1_OSPI_BASE(n) + 0x080UL + (0x10UL * (b)))
#define RA8P1_OSPI_CDABUF(n, b) (RA8P1_OSPI_BASE(n) + 0x084UL + (0x10UL * (b)))
#define RA8P1_OSPI_CDD0BUF(n,b) (RA8P1_OSPI_BASE(n) + 0x088UL + (0x10UL * (b)))
#define RA8P1_OSPI_CDD1BUF(n,b) (RA8P1_OSPI_BASE(n) + 0x08CUL + (0x10UL * (b)))
#define RA8P1_OSPI_BMCTL0(n)    (RA8P1_OSPI_BASE(n) + 0x060UL)
#define RA8P1_OSPI_LIOCTL(n)    (RA8P1_OSPI_BASE(n) + 0x108UL)

/* Memory-map command config.  CMCFG1 holds the read opcode and its latency;
 * FFMT picks the frame shape and ADDSIZE the address width. */
#define RA8P1_CMCFG0_FFMT_NORMAL  (0UL << 0)
#define RA8P1_CMCFG0_FFMT_8D      (1UL << 0)
#define RA8P1_CMCFG0_ADDSIZE(n)   ((((uint32_t)(n) - 1UL) & 0x3UL) << 2)
#define RA8P1_CMCFG1_RDCMD(c)     (((uint32_t)(c) & 0xFFFFUL) << 0)
#define RA8P1_CMCFG1_RDLATE(n)    (((uint32_t)(n) & 0x1FUL) << 16)

/*
 * BMCFGCHn: bridge behaviour for one channel.  PREEN enables prefetch;
 * without it every incremental read pays the full command, address and
 * latency again.
 */
#define RA8P1_BMCFG_WRMD        (1UL << 0)
#define RA8P1_BMCFG_MWRCOMB     (1UL << 7)
#define RA8P1_BMCFG_MWRSIZE(n)  (((uint32_t)(n) & 0xFFUL) << 8)
#define RA8P1_BMCFG_MWRSIZE_64  0x0FUL    /* max: 64 bytes per combined frame */
#define RA8P1_BMCFG_PREEN       (1UL << 16)
#define RA8P1_BMCFG_CMBTIM(n)   (((uint32_t)(n) & 0xFFUL) << 24)

/* BMCTL0: two bits per (channel, chip select).  01 = read enable. */
#define RA8P1_BMCTL0_CH0CS1_RD    (1UL << 2)
#define RA8P1_BMCTL0_CH0CS1_WR    (1UL << 3)
#define RA8P1_CMCFG2_WRCMD(c)     (((uint32_t)(c) & 0xFFFFUL) << 0)
#define RA8P1_CMCFG2_WRLATE(n)    (((uint32_t)(n) & 0x1FUL) << 16)

/*
 * LIOCFGCSn protocol mode; the manual marks every encoding outside its list
 * "setting prohibited".  The list has 8D-8D-8D and no 8S-8S-8S, so octal
 * flash runs in DTR on this controller.
 */
#define RA8P1_LIOCFG_PRTMD_MASK   0x3FFUL
#define RA8P1_LIOCFG_PRTMD_1S1S1S 0x000UL
#define RA8P1_LIOCFG_PRTMD_8D8D8D 0x3FFUL
/* Extends the DDR sampling window; the amount depends on the memory's
 * output delay and is calibrated against read data. */
#define RA8P1_LIOCFG_DDRSMPEX(n)  (((uint32_t)(n) & 0xFUL) << 28)
#define RA8P1_LIOCFG_DDRSMPEX_MASK (0xFUL << 28)
#define RA8P1_LIOCFG_CSMIN(n)     (((uint32_t)(n) & 0xFUL) << 16)
#define RA8P1_LIOCFG_CSMIN_MASK   (0xFUL << 16)

/*
 * WRAPCFG.DSSFTCSn: delay cells on the OM_DQS input, 0..31, which the
 * configuration flow calls "drive/sample timing".  At its reset value of 0
 * the strobe sits on the data transition, and every other byte reads back
 * wrong.
 */
#define RA8P1_WRAPCFG_DSSFTCS0(n)   (((uint32_t)(n) & 0x1FUL) << 8)
#define RA8P1_WRAPCFG_DSSFTCS0_MASK (0x1FUL << 8)
#define RA8P1_WRAPCFG_DSSFTCS1(n)   (((uint32_t)(n) & 0x1FUL) << 24)
#define RA8P1_WRAPCFG_DSSFTCS1_MASK (0x1FUL << 24)
#define RA8P1_OSPI_COMSTT(n)    (RA8P1_OSPI_BASE(n) + 0x184UL)

/* LIOCTL.RSTCS0 drives the OM_RESET pin: 0 = low.  It resets to 0, so once
 * the pin is muxed to the OSPI function the flash is held in hardware reset
 * until the bit is set. */
#define RA8P1_OSPI_LIOCTL_RSTCS0 (1UL << 16)

/* Mapped windows, from the address map: OSPI0 gets 256 MB per chip select,
 * OSPI1 gets 128 MB. */
#define RA8P1_OSPI0_CS0_ADDR    0x80000000UL
#define RA8P1_OSPI1_CS0_ADDR    0x70000000UL

/* Manual-command transaction descriptor (CDTBUFn).  Sizes are byte counts.
 * 8D-8D-8D octal sends a 2-byte command: the opcode and its complement. */
#define RA8P1_CDTBUF_CMDSIZE(n) (((uint32_t)(n) & 0x3UL) << 0)    /*  1:0 */
#define RA8P1_CDTBUF_ADDSIZE(n) (((uint32_t)(n) & 0x7UL) << 2)    /*  4:2 */
#define RA8P1_CDTBUF_DATASIZE(n) (((uint32_t)(n) & 0xFUL) << 5)   /*  8:5 */
#define RA8P1_CDTBUF_LATE(n)    (((uint32_t)(n) & 0x1FUL) << 9)   /* 13:9 */
#define RA8P1_CDTBUF_CMD(c)     (((uint32_t)(c) & 0xFFFFUL) << 16)
#define RA8P1_CDTBUF_TRTYPE_WRITE (1UL << 15)   /* 0 = read from the slave */

#define RA8P1_CDCTL0_TRREQ      (1UL << 0)   /* self-clears on completion */
#define RA8P1_CDCTL0_PERMD      (1UL << 1)
#define RA8P1_CDCTL0_CSSEL      (1UL << 3)   /* 0 = CS0, 1 = CS1 */

/** @brief Module stop: OSPI0 (the board's flash) is MSTPB16, OSPI1 MSTPB17. */
#define RA8P1_MSTPB_OSPI0       (1UL << 16)
#define RA8P1_MSTPB_OSPI1       (1UL << 17)
/** @brief Module stop for the USB 2.0 high-speed controller (UM 11.2.7). */
#define RA8P1_MSTPB_USBHS       (1UL << 12)

/*---------------------------------------------------------------------------*/
/* USB 2.0 HIGH-SPEED MODULE (UM 38)                                         */
/*---------------------------------------------------------------------------*/
/*
 * The Renesas pipe controller: 16-bit registers throughout, a default
 * control pipe plus nine programmable ones, and an on-chip UTMI transceiver
 * on dedicated DP/DM pins, so the data lines need no pin muxing.
 *
 * High speed runs the PHY's own PLL from a 12/20/24/48 MHz reference on the
 * EXTAL pin, on this board the 24 MHz crystal.  USBCLK feeds USBFS, and
 * USBHS only in CL-only mode, which has no high speed, so USBHS needs no
 * USBCKCR request/ready handshake.
 */
#define RA8P1_USBHS_BASE        0x40351000UL
#define RA8P1_USBHS_SYSCFG      (RA8P1_USBHS_BASE + 0x000UL)  /* 16-bit */
#define RA8P1_USBHS_SYSSTS0     (RA8P1_USBHS_BASE + 0x004UL)
#define RA8P1_USBHS_PLLSTA      (RA8P1_USBHS_BASE + 0x006UL)
#define RA8P1_USBHS_DVSTCTR0    (RA8P1_USBHS_BASE + 0x008UL)
#define RA8P1_USBHS_TESTMODE    (RA8P1_USBHS_BASE + 0x00CUL)
#define RA8P1_USBHS_INTENB0     (RA8P1_USBHS_BASE + 0x030UL)
#define RA8P1_USBHS_INTENB1     (RA8P1_USBHS_BASE + 0x032UL)
#define RA8P1_USBHS_SOFCFG      (RA8P1_USBHS_BASE + 0x03CUL)
#define RA8P1_USBHS_PHYSET      (RA8P1_USBHS_BASE + 0x03EUL)
#define RA8P1_USBHS_INTSTS0     (RA8P1_USBHS_BASE + 0x040UL)
#define RA8P1_USBHS_INTSTS1     (RA8P1_USBHS_BASE + 0x042UL)
#define RA8P1_USBHS_FRMNUM      (RA8P1_USBHS_BASE + 0x04CUL)
#define RA8P1_USBHS_USBADDR     (RA8P1_USBHS_BASE + 0x050UL)
#define RA8P1_USBHS_DCPCTR      (RA8P1_USBHS_BASE + 0x060UL)
#define RA8P1_USBHS_LPCTRL      (RA8P1_USBHS_BASE + 0x100UL)
#define RA8P1_USBHS_LPSTS       (RA8P1_USBHS_BASE + 0x102UL)

/* Control transfers.  The hardware decodes the setup packet into these four
 * registers; a SETUP needs no FIFO read. */
#define RA8P1_USBHS_USBREQ      (RA8P1_USBHS_BASE + 0x054UL)  /* type|request */
#define RA8P1_USBHS_USBVAL      (RA8P1_USBHS_BASE + 0x056UL)  /* wValue       */
#define RA8P1_USBHS_USBINDX     (RA8P1_USBHS_BASE + 0x058UL)  /* wIndex       */
#define RA8P1_USBHS_USBLENG     (RA8P1_USBHS_BASE + 0x05AUL)  /* wLength      */
#define RA8P1_USBHS_DCPCFG      (RA8P1_USBHS_BASE + 0x05CUL)
#define RA8P1_USBHS_DCPMAXP     (RA8P1_USBHS_BASE + 0x05EUL)

/*
 * The CFIFO port is the only way to reach the DCP's 64-byte buffer, and its
 * address depends on the access width.  With BIGEND=0 (little endian), UM
 * Table 38.8 puts 8-bit access at CFIFOHH, 0x017, and 16-bit access at
 * CFIFOH, 0x016; 8-bit access to CFIFOLL at 0x014, the offset the register
 * is listed under, is "access prohibited".
 *
 * Bytes written to 0x014 are discarded with no error: FRDY reads ready, BVAL
 * commits and BEMP fires on an empty buffer, and the host times out waiting
 * for the IN data.
 */
#define RA8P1_USBHS_CFIFO       (RA8P1_USBHS_BASE + 0x014UL)  /* 32-bit     */
#define RA8P1_USBHS_CFIFOH      (RA8P1_USBHS_BASE + 0x016UL)  /* 16-bit, LE */
#define RA8P1_USBHS_CFIFOHH     (RA8P1_USBHS_BASE + 0x017UL)  /* 8-bit,  LE */
#define RA8P1_USBHS_CFIFOSEL    (RA8P1_USBHS_BASE + 0x020UL)
#define RA8P1_USBHS_CFIFOCTR    (RA8P1_USBHS_BASE + 0x022UL)
#define RA8P1_USBHS_BRDYENB     (RA8P1_USBHS_BASE + 0x036UL)
#define RA8P1_USBHS_BEMPENB     (RA8P1_USBHS_BASE + 0x03AUL)
#define RA8P1_USBHS_BRDYSTS     (RA8P1_USBHS_BASE + 0x046UL)
#define RA8P1_USBHS_BEMPSTS     (RA8P1_USBHS_BASE + 0x04AUL)
#define RA8P1_USBHS_PIPESEL     (RA8P1_USBHS_BASE + 0x064UL)
#define RA8P1_USBHS_PIPECFG     (RA8P1_USBHS_BASE + 0x068UL)
#define RA8P1_USBHS_PIPEBUF     (RA8P1_USBHS_BASE + 0x06AUL)
#define RA8P1_USBHS_PIPEMAXP    (RA8P1_USBHS_BASE + 0x06CUL)
#define RA8P1_USBHS_PIPECTR(n)  (RA8P1_USBHS_BASE + 0x070UL + (2UL * ((n) - 1U)))

/* D0FIFO: a second port, used by the bulk pipes so the DCP keeps CFIFO.
 * 32-bit access is valid at N+0 with BIGEND=0 (UM Table 38.6), so bulk data
 * moves a word at a time through 0x018. */
#define RA8P1_USBHS_D0FIFO      (RA8P1_USBHS_BASE + 0x018UL)  /* 32-bit, LE */
#define RA8P1_USBHS_D0FIFOSEL   (RA8P1_USBHS_BASE + 0x028UL)
#define RA8P1_USBHS_D0FIFOCTR   (RA8P1_USBHS_BASE + 0x02AUL)

/* PIPECFG / PIPEBUF / PIPEMAXP / PIPEnCTR */
#define RA8P1_PIPECFG_EPNUM(n)  ((uint16_t)((n) & 0xFU))
#define RA8P1_PIPECFG_DIR_IN    (1U << 4)    /* 1 = transmitting to host   */
#define RA8P1_PIPECFG_SHTNAK    (1U << 7)
#define RA8P1_PIPECFG_CNTMD     (1U << 8)
#define RA8P1_PIPECFG_DBLB      (1U << 9)
#define RA8P1_PIPECFG_BFRE      (1U << 10)
#define RA8P1_PIPECFG_TYPE_BULK (1U << 14)
/* BUFSIZE counts 64-byte blocks minus one; a pipe occupies
 * (BUFSIZE + 1) * (DBLB + 1) blocks starting at BUFNMB. */
#define RA8P1_PIPEBUF_BUFNMB(n)  ((uint16_t)((n) & 0xFFU))
#define RA8P1_PIPEBUF_BUFSIZE(n) ((uint16_t)(((n) & 0x1FU) << 10))
#define RA8P1_PIPEMAXP_MXPS(n)   ((uint16_t)((n) & 0x7FFU))
#define RA8P1_PIPECTR_PID_MASK   0x3U
#define RA8P1_PIPECTR_PID_NAK    0U
#define RA8P1_PIPECTR_PID_BUF    1U
#define RA8P1_PIPECTR_PID_STALL  2U
#define RA8P1_PIPECTR_PBUSY      (1U << 5)
#define RA8P1_PIPECTR_SQCLR      (1U << 8)
#define RA8P1_PIPECTR_ACLRM      (1U << 9)
#define RA8P1_PIPECTR_INBUFM     (1U << 14)
#define RA8P1_PIPECTR_BSTS       (1U << 15)

/* CFIFOSEL.  ISEL sets the direction of DCP access and has no effect for
 * other pipes; MBW sets the access width, which must match the width of the
 * accesses made to the port. */
#define RA8P1_CFIFOSEL_CURPIPE(n) ((uint16_t)((n) & 0xFU))
#define RA8P1_CFIFOSEL_ISEL       (1U << 5)   /* 1 = writing to the buffer  */
#define RA8P1_CFIFOSEL_BIGEND     (1U << 8)
#define RA8P1_CFIFOSEL_MBW_8      (0U << 10)
#define RA8P1_CFIFOSEL_MBW_16     (1U << 10)
#define RA8P1_CFIFOSEL_MBW_32     (2U << 10)
#define RA8P1_CFIFOSEL_REW        (1U << 14)
#define RA8P1_CFIFOSEL_RCNT       (1U << 15)

/* CFIFOCTR */
#define RA8P1_CFIFOCTR_DTLN_MASK  0xFFFU
#define RA8P1_CFIFOCTR_FRDY       (1U << 13)
#define RA8P1_CFIFOCTR_BCLR       (1U << 14)
#define RA8P1_CFIFOCTR_BVAL       (1U << 15)

/* DCPCTR.  PID is the pipe's response: NAK defers, BUF answers from the
 * buffer, STALL refuses. */
#define RA8P1_DCPCTR_PID_MASK   0x3U
#define RA8P1_DCPCTR_PID_NAK    0U
#define RA8P1_DCPCTR_PID_BUF    1U
#define RA8P1_DCPCTR_PID_STALL  2U
#define RA8P1_DCPCTR_CCPL       (1U << 2)   /* 1 = finish the transfer     */
#define RA8P1_DCPCTR_PBUSY      (1U << 5)
#define RA8P1_DCPCTR_SQMON      (1U << 6)
#define RA8P1_DCPCTR_SQSET      (1U << 7)
#define RA8P1_DCPCTR_SQCLR      (1U << 8)
#define RA8P1_DCPCTR_BSTS       (1U << 15)

#define RA8P1_DCPCFG_DIR        (1U << 4)
#define RA8P1_DCPMAXP_MXPS(n)   ((uint16_t)((n) & 0x7FU))

/* SYSCFG.  DRPD, the host pull-downs, resets to 1; device mode clears it. */
#define RA8P1_SYSCFG_USBE       (1U << 0)
#define RA8P1_SYSCFG_DPRPU      (1U << 4)   /* D+ pull-up = soft connect    */
#define RA8P1_SYSCFG_DRPD       (1U << 5)   /* D+/D- pull-downs (host)      */
#define RA8P1_SYSCFG_DCFM       (1U << 6)   /* 0 = device, 1 = host         */
#define RA8P1_SYSCFG_HSE        (1U << 7)   /* high-speed operation enable  */
#define RA8P1_SYSCFG_CNEN       (1U << 8)

/* PHYSET.  DIRPD resets to 1: the transceiver is powered down until the
 * bit is cleared. */
#define RA8P1_PHYSET_DIRPD      (1U << 0)   /* 1 = low power                */
#define RA8P1_PHYSET_PLLRESET   (1U << 1)   /* resets to 1; clear once only */
#define RA8P1_PHYSET_CDPEN      (1U << 3)
#define RA8P1_PHYSET_CLKSEL(n)  (((uint16_t)(n) & 0x3U) << 4)
#define RA8P1_PHYSET_CLKSEL_MASK (0x3U << 4)
#define RA8P1_PHYSET_CLKSEL_12M 0U
#define RA8P1_PHYSET_CLKSEL_48M 1U
#define RA8P1_PHYSET_CLKSEL_20M 2U
#define RA8P1_PHYSET_CLKSEL_24M 3U          /* this board's crystal         */
#define RA8P1_PHYSET_REPSTART   (1U << 11)
#define RA8P1_PHYSET_HSEB       (1U << 15)  /* CL-only mode: no high speed  */

#define RA8P1_LPSTS_SUSPENDM    (1U << 14)  /* 1 = UTMI normal mode         */
#define RA8P1_PLLSTA_PLLLOCK    (1U << 0)
#define RA8P1_SYSSTS0_IDMON     (1U << 2)   /* OTG role: 1 = device      */

/* DVSTCTR0.RHST in device mode: 010 means bus reset in progress or full
 * speed, and 011 bus reset in progress or high speed, so a speed read during
 * a reset is not final. */
#define RA8P1_DVSTCTR0_RHST_MASK 0x7U
#define RA8P1_DVSTCTR0_RHST_NONE 0U
#define RA8P1_DVSTCTR0_RHST_FULL 2U
#define RA8P1_DVSTCTR0_RHST_HIGH 3U

/* INTSTS0 */
#define RA8P1_INTSTS0_CTSQ_MASK  0x7U
#define RA8P1_INTSTS0_VALID      (1U << 3)
#define RA8P1_INTSTS0_DVSQ_SHIFT 4U
#define RA8P1_INTSTS0_DVSQ_MASK  (0x7U << 4)
#define RA8P1_INTSTS0_VBSTS      (1U << 7)
#define RA8P1_INTSTS0_BRDY       (1U << 8)
#define RA8P1_INTSTS0_NRDY       (1U << 9)
#define RA8P1_INTSTS0_BEMP       (1U << 10)
#define RA8P1_INTSTS0_CTRT       (1U << 11)
#define RA8P1_INTSTS0_DVST       (1U << 12)
#define RA8P1_INTSTS0_SOFR       (1U << 13)
#define RA8P1_INTSTS0_RESM       (1U << 14)
#define RA8P1_INTSTS0_VBINT      (1U << 15)

/* INTENB0 mirrors INTSTS0's upper bits. */
#define RA8P1_INTENB0_BRDYE      (1U << 8)
#define RA8P1_INTENB0_NRDYE      (1U << 9)
#define RA8P1_INTENB0_BEMPE      (1U << 10)
#define RA8P1_INTENB0_CTRE       (1U << 11)
#define RA8P1_INTENB0_DVSE       (1U << 12)
#define RA8P1_INTENB0_SOFE       (1U << 13)
#define RA8P1_INTENB0_RSME       (1U << 14)
#define RA8P1_INTENB0_VBSE       (1U << 15)

/** @brief PSEL codes: the OSPI function on any pin (OM_0_* here), and the
 *         GLCDC function. */
#define RA8P1_PFS_PSEL_OSPI     0x1CUL
#define RA8P1_PFS_PSEL_GLCDC    0x19UL

/** @brief Macronix RDID opcode, and the manufacturer byte that leads its
 *         reply (then type and density). */
#define RA8P1_MX_MANUFACTURER   0xC2U
#define RA8P1_MX_CMD_RDID       0x9FU

/*---------------------------------------------------------------------------*/
/* EXTERNAL BUS / SDRAM CONTROLLER (UM 15.6)                                 */
/*---------------------------------------------------------------------------*/
/*
 * The SDRAM window is at 0x6800_0000.  The registers are byte, half and word
 * wide and not evenly spaced; each offset is from the manual.
 */
#define RA8P1_SDRAM_BASE        0x68000000UL   /* the mapped SDRAM window */
#define RA8P1_BUS_BASE          0x40003000UL

#define RA8P1_SDCCR             (RA8P1_BUS_BASE + 0xC00UL)   /*  8-bit */
#define RA8P1_SDCMOD            (RA8P1_BUS_BASE + 0xC01UL)   /*  8-bit */
#define RA8P1_SDAMOD            (RA8P1_BUS_BASE + 0xC02UL)   /*  8-bit */
#define RA8P1_SDSELF            (RA8P1_BUS_BASE + 0xC10UL)   /*  8-bit */
#define RA8P1_SDRFCR            (RA8P1_BUS_BASE + 0xC14UL)   /* 16-bit */
#define RA8P1_SDRFEN            (RA8P1_BUS_BASE + 0xC16UL)   /*  8-bit */
#define RA8P1_SDICR             (RA8P1_BUS_BASE + 0xC20UL)   /*  8-bit */
#define RA8P1_SDIR              (RA8P1_BUS_BASE + 0xC24UL)   /* 16-bit */
#define RA8P1_SDADR             (RA8P1_BUS_BASE + 0xC40UL)   /*  8-bit */
#define RA8P1_SDTR              (RA8P1_BUS_BASE + 0xC44UL)   /* 32-bit */
#define RA8P1_SDMOD             (RA8P1_BUS_BASE + 0xC48UL)   /* 16-bit */
#define RA8P1_SDSR              (RA8P1_BUS_BASE + 0xC50UL)   /*  8-bit */

#define RA8P1_SDCCR_EXENB       (1U << 0)      /* SDRAM access enable      */
/* BSIZE[1:0] at 5:4 encodes 16, 32 and 8 bits as 00, 01 and 10. */
#define RA8P1_SDCCR_BSIZE_16    (0U << 4)
#define RA8P1_SDCCR_BSIZE_32    (1U << 4)
#define RA8P1_SDCCR_BSIZE_8     (2U << 4)
/* MXC[1:0]: the column-address shift; 01 = 9 bits, for a 512-column part
 * (A0-A8). */
#define RA8P1_SDADR_MXC_9BIT    (1U << 0)
#define RA8P1_SDRFEN_RFEN       (1U << 0)      /* auto-refresh enable      */
/* Continuous access holds the row open across consecutive accesses.  It is
 * off at reset and ignores writes once EXENB is set, so it is enabled before
 * EXENB; without it every access pays a full ACT/CAS/PRE. */
#define RA8P1_SDAMOD_BE         (1U << 0)
#define RA8P1_SDICR_INIRQ       (1U << 0)      /* start the init sequencer */
#define RA8P1_SDSR_INIST        (1U << 3)      /* init sequence running    */
#define RA8P1_SDSR_MRSST        (1U << 2)
#define RA8P1_SDSR_SRFST        (1U << 4)

/* SDTR timing, all in SDCLK cycles: CL 2:0, WR 8, RP 11:9, RCD 13:12 and
 * RAI 18:16.  RP, RCD and RAI encode the value minus one, CL the value
 * itself, and WR is a flag (0 = 1 cycle, 1 = 2). */
#define RA8P1_SDTR_CL(c)        (((uint32_t)(c) & 0x7UL) << 0)
#define RA8P1_SDTR_WR_2CYC      (1UL << 8)
#define RA8P1_SDTR_RP(c)        ((((uint32_t)(c) - 1UL) & 0x7UL) << 9)
#define RA8P1_SDTR_RCD(c)       ((((uint32_t)(c) - 1UL) & 0x3UL) << 12)
#define RA8P1_SDTR_RAI(c)       ((((uint32_t)(c) - 1UL) & 0x7UL) << 16)

/* SDIR: initialisation sequencer timing. */
#define RA8P1_SDIR_ARFI(c)      (((uint16_t)(c) & 0xFU) << 0)     /*  3:0 */
#define RA8P1_SDIR_ARFC(c)      (((uint16_t)(c) & 0xFU) << 4)     /*  7:4 */
#define RA8P1_SDIR_PRC(c)       (((uint16_t)(c) & 0x7U) << 8)     /* 10:8 */

/** @brief SDCLK output enable; the register is in the SYSC block. */
#define RA8P1_SDCKOCR           (RA8P1_SYSC_BASE + 0x053UL)  /* 8-bit */
#define RA8P1_SDCKOCR_SDCKOEN   (1U << 0)

/** @brief PSEL value that selects the external-bus function on any pin. */
#define RA8P1_PFS_PSEL_BUS      0x0BUL

/* PmnPFS.DSCR[1:0] at 11:10: 00 low, 01 middle, 10 high-speed high drive,
 * 11 high drive. */
#define RA8P1_PFS_DSCR_HS_HIGH  (2UL << 10)

/*---------------------------------------------------------------------------*/
/* DMA CONTROLLER (UM 17)                                                    */
/*---------------------------------------------------------------------------*/
/* Eight channels, 0x40 apart, plus one global activation register. */
#define RA8P1_DMAC_BASE(n)      (0x4000A000UL + (0x40UL * (n)))
#define RA8P1_DMAC_DMSAR(n)     (RA8P1_DMAC_BASE(n) + 0x00UL)  /* 32-bit */
#define RA8P1_DMAC_DMDAR(n)     (RA8P1_DMAC_BASE(n) + 0x04UL)  /* 32-bit */
#define RA8P1_DMAC_DMCRA(n)     (RA8P1_DMAC_BASE(n) + 0x08UL)  /* 32-bit */
#define RA8P1_DMAC_DMTMD(n)     (RA8P1_DMAC_BASE(n) + 0x10UL)  /* 16-bit */
#define RA8P1_DMAC_DMINT(n)     (RA8P1_DMAC_BASE(n) + 0x13UL)  /*  8-bit */
#define RA8P1_DMAC_DMAMD(n)     (RA8P1_DMAC_BASE(n) + 0x14UL)  /* 16-bit */
#define RA8P1_DMAC_DMCNT(n)     (RA8P1_DMAC_BASE(n) + 0x1CUL)  /*  8-bit */
#define RA8P1_DMAC_DMREQ(n)     (RA8P1_DMAC_BASE(n) + 0x1DUL)  /*  8-bit */
#define RA8P1_DMAC_DMSTS(n)     (RA8P1_DMAC_BASE(n) + 0x1EUL)  /*  8-bit */

/** @brief Global DMAC activation; no channel runs while DMST is 0. */
#define RA8P1_DMAC_DMAST        0x4000A800UL
#define RA8P1_DMAC_DMAST_DMST   (1U << 0)

/* DMTMD: mode 15:14, repeat-area select 13:12, transfer size 9:8. */
#define RA8P1_DMTMD_MD_NORMAL   (0U << 14)
#define RA8P1_DMTMD_MD_BLOCK    (2U << 14)
#define RA8P1_DMTMD_SZ_8        (0U << 8)
#define RA8P1_DMTMD_SZ_16       (1U << 8)
#define RA8P1_DMTMD_SZ_32       (2U << 8)

/* DMAMD: source mode 15:14, dest mode 7:6.  00 fixed, 10 increment. */
#define RA8P1_DMAMD_SM_FIXED    (0U << 14)
#define RA8P1_DMAMD_SM_INC      (2U << 14)
#define RA8P1_DMAMD_DM_FIXED    (0U << 6)
#define RA8P1_DMAMD_DM_INC      (2U << 6)

#define RA8P1_DMINT_DTIE        (1U << 4)   /* transfer-end interrupt      */
#define RA8P1_DMCNT_DTE         (1U << 0)   /* channel enable              */
#define RA8P1_DMREQ_SWREQ       (1U << 0)   /* software trigger            */
#define RA8P1_DMREQ_CLRS        (1U << 4)   /* do not auto-clear SWREQ     */
#define RA8P1_DMSTS_ESIF        (1U << 0)   /* error                       */
#define RA8P1_DMSTS_DTIF        (1U << 4)   /* transfer end                */
#define RA8P1_DMSTS_ACT         (1U << 7)   /* channel active              */

/** @brief Module stop: MSTPCRA.MSTPA22 gates the whole DMAC. */
#define RA8P1_MSTPCRA           (RA8P1_MSTP_BASE + 0x00UL)
#define RA8P1_MSTPA_DMAC        (1UL << 22)

/*---------------------------------------------------------------------------*/
/* I/O PORTS (UM 21.2)                                                       */
/*---------------------------------------------------------------------------*/
/*
 * PmnPFS is one 32-bit register per pin, at 0x40*m + 4*n from the PFS base
 * with m = 0..9, A..D.  PWPR_S gates writes to all of them.
 */
#define RA8P1_PFS_BASE          0x40400800UL
#define RA8P1_PFS(m, n)         (RA8P1_PFS_BASE + (0x40UL * (m)) + (4UL * (n)))
#define RA8P1_PWPR_S            (RA8P1_PFS_BASE + 0x514UL)   /* UM 21.2.8 */
#define RA8P1_PWPR_PFSWE        (1UL << 6)
#define RA8P1_PWPR_B0WI         (1UL << 7)
#define RA8P1_PFS_PSEL_SHIFT    24U                          /* PSEL[28:24] */
#define RA8P1_PFS_PMR           (1UL << 16)                  /* peripheral  */
#define RA8P1_PFS_PDR           (1UL << 2)                   /* 1 = output  */
/* Open drain: the pin only pulls low, for a bus with a pull-up. */
#define RA8P1_PFS_NCODR         (1UL << 6)
#define RA8P1_PFS_PODR          (1UL << 0)                   /* output data */
#define RA8P1_PFS_PSEL_SCI      0x04UL                       /* UM Tbl 21.20 */

/*
 * Port control registers, one 0x20 block per port (UM 21.2.1-21.2.3): the
 * direction and data bits of PmnPFS, a whole port at a time.  PCNTR3 has
 * separate write-only set and clear fields, so a pin is driven without a
 * read-modify-write that could race an interrupt touching another pin of
 * the port.
 */
#define RA8P1_PORT_BASE(m)      (0x40400000UL + (0x20UL * (m)))
#define RA8P1_PORT_PCNTR1(m)    (RA8P1_PORT_BASE(m) + 0x00UL) /* PODR|PDR   */
#define RA8P1_PORT_PCNTR2(m)    (RA8P1_PORT_BASE(m) + 0x04UL) /* EIDR|PIDR  */
#define RA8P1_PORT_PCNTR3(m)    (RA8P1_PORT_BASE(m) + 0x08UL) /* PORR|POSR  */
#define RA8P1_PORT_PODR_SHIFT   16U     /* PCNTR1 output data, bits 31:16   */
#define RA8P1_PORT_PORR_SHIFT   16U     /* PCNTR3 clear,       bits 31:16   */
#define RA8P1_PORT_MAX          0xDU    /* PORTD; m = 0..9, A..D            */

/*---------------------------------------------------------------------------*/
/* SCI_B (UM 39)                                                             */
/*---------------------------------------------------------------------------*/
/* Bases are from the datasheet's peripheral map. */
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
/* ICU EVENT LINK (UM 14.2.17)                                               */
/*---------------------------------------------------------------------------*/
/*
 * Peripheral interrupts have no fixed NVIC slots on this family: any of the
 * 96 slots can carry any event, and IELSRn says which.  Nothing reaches the
 * NVIC until its slot is linked; SysTick, a core exception, needs no link.
 */
#define RA8P1_ICU_BASE          0x4000C000UL
#define RA8P1_ICU_IELSR(n)      (RA8P1_ICU_BASE + 0x300UL + (4UL * (n)))
#define RA8P1_ICU_IELSR_IR      (1UL << 16)   /* interrupt status, write 0 */

/*
 * Slot allocation, shared by the drivers and the wake-source reporter.  Any
 * slot can carry any event, and two drivers that link one slot take each
 * other's interrupt with no error.
 */
#define RA8P1_ICU_SLOT_UART_RXI 0U
#define RA8P1_ICU_SLOT_UART_ERI 1U
#define RA8P1_ICU_SLOT_HTIMER   2U
#define RA8P1_ICU_SLOT_DMAC0    3U
#define RA8P1_ICU_SLOT_USBHS    4U
#define RA8P1_ICU_SLOT_IPC      5U
#define RA8P1_ICU_SLOT_NPU      6U

/** @brief Event numbers this port links (UM Table 14.5). */
#define RA8P1_EVENT_SCI8_RXI    0x2FCUL
#define RA8P1_EVENT_SCI8_ERI    0x2FFUL
#define RA8P1_EVENT_GPT0_CCMPA  0x181UL
#define RA8P1_EVENT_DMAC0_INT   0x040UL
/* One event carries every USB source (VBUS, resume, frame, device state,
 * control stage, BRDY, BEMP), so the handler drains INTSTS0 on each.
 * 0x2C1 and 0x2C2 are the D0/D1FIFO DMA requests, unused by this port. */
#define RA8P1_EVENT_USBHS_USBIR 0x2C3UL
#define RA8P1_EVENT_IPC_IRQ0    0x05BUL

/*---------------------------------------------------------------------------*/
/* GPT32 (UM 23)                                                             */
/*---------------------------------------------------------------------------*/
/*
 * The htimer's counter and compare: a 32-bit free-running counter on PCLKD
 * with a compare that raises an event.  GTINTAD has no per-compare interrupt
 * enable (bits 7:0 are reserved): the compare always raises its event, and
 * the ICU link decides whether it reaches the NVIC.
 */
#define RA8P1_GPT_BASE(n)       (0x40322000UL + (0x100UL * (n)))
#define RA8P1_GPT_GTCR(n)       (RA8P1_GPT_BASE(n) + 0x2CUL)
#define RA8P1_GPT_GTST(n)       (RA8P1_GPT_BASE(n) + 0x3CUL)
#define RA8P1_GPT_GTCNT(n)      (RA8P1_GPT_BASE(n) + 0x48UL)
#define RA8P1_GPT_GTCCRA(n)     (RA8P1_GPT_BASE(n) + 0x4CUL)
#define RA8P1_GPT_GTPR(n)       (RA8P1_GPT_BASE(n) + 0x64UL)
#define RA8P1_GPT_GTWP(n)       (RA8P1_GPT_BASE(n) + 0x00UL)
#define RA8P1_GPT_GTIOR(n)      (RA8P1_GPT_BASE(n) + 0x34UL)
#define RA8P1_GPT_GTUDDTYC(n)   (RA8P1_GPT_BASE(n) + 0x30UL)

/* GTWP drops writes to the other GPT registers until the key opens it. */
#define RA8P1_GPT_GTWP_KEY      0xA500UL

/* GTIOR.GTIOA[4:0]: initial level, cycle-end action, compare-match action.
 * 0x09 = start low, high at compare match, low at cycle end.  OAE gates the
 * pin. */
#define RA8P1_GPT_GTIOA_CLK     0x09UL
#define RA8P1_GPT_GTIOR_OAE     (1UL << 8)

/** @brief Module stop: MSTPCRE.MSTPE19 gates GPT channel 12 (camera XCLK). */
#define RA8P1_MSTPCRE_GPT12     (1UL << 19)

/** @brief Pin function select for the GPT outputs. */
#define RA8P1_PFS_PSEL_GPT      0x03UL
#define RA8P1_GPT_GTCR_CST      (1UL << 0)     /* count start          */
/*
 * GTCLKCR is written before the module-stop release and is locked once
 * MSTPE31 is 0 (UM 23.10.1).  BPEN=1 selects the synchronous PCLKD core
 * clock.  The reset default is the asynchronous GPTCLK domain; with GPTCLK
 * on the 8 MHz MOCO under a 120 MHz PCLKA bus, the synchroniser drops every
 * register write and the block reads as zeros.
 */
#define RA8P1_GPT_GTCLKCR       0x40323F10UL
#define RA8P1_GPT_GTCLKCR_BPEN  (1UL << 0)
#define RA8P1_GPT_GTCR_MD_SAW   (0UL << 16)    /* saw-wave PWM mode 1  */
#define RA8P1_GPT_GTST_TCFA     (1UL << 0)     /* compare match A      */

#define RA8P1_MSTPCRE           (RA8P1_MSTP_BASE + 0x10UL)  /* UM 11.2.10 */
#define RA8P1_GPTCKDIVCR        (RA8P1_SYSC_BASE + 0x05CUL)  /* 8-bit */
#define RA8P1_GPTCKCR           (RA8P1_SYSC_BASE + 0x05DUL)  /* 8-bit */
#define RA8P1_MSTPE_GPT0        (1UL << 31)

/*---------------------------------------------------------------------------*/
/* CODE MRAM PROGRAMMING (UM 60.4.2)                                         */
/*---------------------------------------------------------------------------*/
/*
 * No bootrom and no erase: an ordinary STR to an MRAM address enters a
 * 32-byte program buffer, which commits when it fills, when a write leaves
 * its 32-byte boundary, or on an explicit MRCFL flush.  Writes of 1..31
 * bytes are legal: barrier, then flush.  MRCPC1, MRCBPROT1 and MRCFLR are
 * key-gated and take only 16-bit writes, dropping any other; MRPSC and MRCPS
 * are plain 8-bit registers with no key.
 */
#define RA8P1_MRAM_REG_BASE     0x4013C000UL
#define RA8P1_MRPSC             (RA8P1_MRAM_REG_BASE + 0x2800UL) /* 8-bit  */
#define RA8P1_MRPSC_MHSPEN      (1U << 0)   /* high-speed program mode     */
#define RA8P1_MRCPS             (RA8P1_MRAM_REG_BASE + 0x3010UL) /* 8-bit  */
#define RA8P1_MRCPS_PRGERRC     (1U << 0)
#define RA8P1_MRCPS_ECCERRC     (1U << 1)
#define RA8P1_MRCPS_ABUFEMP     (1U << 5)
#define RA8P1_MRCPS_ABUFFULL    (1U << 6)
#define RA8P1_MRCPS_PRGBSYC     (1U << 7)
/*
 * Programming the secure alias needs MRCPSEN, which outranks block
 * protection: a store without it raises a bus fault.  Each register carries
 * its own key and must be written 16 bits at a time.  BPCN1 is ignored
 * unless MRCPSEN is already 1, so MRCPSEN is written first.
 */
#define RA8P1_MRCPC1            (RA8P1_MRAM_REG_BASE + 0x3004UL) /* 16-bit */
#define RA8P1_MRCPC1_KEY        0x6800U
#define RA8P1_MRCPC1_MRCPSEN    (1U << 0)
#define RA8P1_MRCBPROT1         (RA8P1_MRAM_REG_BASE + 0x300CUL) /* 16-bit */
#define RA8P1_MRCBPROT1_KEY     0xB100U
#define RA8P1_MRCBPROT1_BPCN1   (1U << 0)
#define RA8P1_MRCFLR            (RA8P1_MRAM_REG_BASE + 0x3030UL) /* 16-bit */
#define RA8P1_MRCFLR_KEY        0xC300U
#define RA8P1_MRCFLR_MRCFL      (1U << 0)
/** @brief Program granule: buffer width, and the MPU/cache line width too. */
#define RA8P1_MRAM_GRANULE      32UL

/*---------------------------------------------------------------------------*/
/* CAC (UM 10, CLOCK FREQUENCY ACCURACY MEASUREMENT CIRCUIT)                 */
/*---------------------------------------------------------------------------*/
/* Counts one clock against another, on-chip. */
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
/* RPS selects the reference source: 0 is the external CACREF pin, 1 an
 * internal clock.  With RPS clear and no signal on the pin, the measurement
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
/* IWDT (UM 29, INDEPENDENT WATCHDOG TIMER)                                  */
/*---------------------------------------------------------------------------*/
/*
 * The kernel's watchdog.  It counts IWDTCLK = LOCO/2 = 16.384 kHz, which
 * does not change with the clock tree; WDT counts PCLKB, which does.  OFS0
 * reads 0xFFFFFFFF on this board, so IWDTSTRT is 1: register start mode,
 * where counting begins on the first refresh.
 */
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
/* WDT0/WDT1 (UM 28, WATCHDOG TIMER)                                         */
/*---------------------------------------------------------------------------*/
/*
 * Two instances 0x100 apart: WDT0 for CPU0, WDT1 for CPU1, the M33 that runs
 * the coprocessor payload.  The payload refreshes WDT1 on each loop pass; a
 * hang, even with interrupts masked, stops the refresh, and the underflow
 * raises an NMI on CPU1.  OFS3 reads 0xFFFFFFFF on this board, so WDTSTRT1
 * is 1: register start mode, configured through these registers with no
 * option-memory write.  WDTCR/WDTRR/WDTSR need their documented access
 * width; a 32-bit AHB read of the block returns bus-dependent bytes.
 */
#define RA8P1_WDT1_BASE         0x40202700UL
#define RA8P1_WDT1_RR           (RA8P1_WDT1_BASE + 0x00UL)  /* 8-bit  refresh */
#define RA8P1_WDT1_CR           (RA8P1_WDT1_BASE + 0x02UL)  /* 16-bit control */
#define RA8P1_WDT1_SR           (RA8P1_WDT1_BASE + 0x04UL)  /* 16-bit status  */
#define RA8P1_WDT1_RCR          (RA8P1_WDT1_BASE + 0x06UL)  /* 8-bit  reset   */

/* WDTCR fields.  The reset value 0x33F3 is TOPS 16384 x CKS /128, no
 * window.  The timeout must outlast the longest single payload operation,
 * so CKS is /8192: 16384 x 8192 PCLKB cycles is 2.1 s at 62.5 MHz. */
#define RA8P1_WDT1_CR_TOPS_16384 (0x3U << 0)
#define RA8P1_WDT1_CR_CKS_8192   (0x8U << 4)
#define RA8P1_WDT1_CR_RPES_NONE  (0x3U << 8)   /* window end 0%   */
#define RA8P1_WDT1_CR_RPSS_NONE  (0x3U << 12)  /* window start 100% */

/* WDTRCR.RSTIRQS bit 7: 0 = interrupt (the NMI), 1 = reset.  It resets to
 * 1; cleared, an underflow raises the NMI instead of a system reset. */
#define RA8P1_WDT1_RCR_RSTIRQS   (1U << 7)

/* WDTSR flags. */
#define RA8P1_WDT1_SR_UNDFF      (1U << 14)  /* underflow            */
#define RA8P1_WDT1_SR_REFEF      (1U << 15)  /* refresh error        */

/*
 * ICU NMI block (UM 14).  ICU0 and ICU1 share this base; each CPU sees its
 * own, so CPU1's payload writes these to reach ICU1.  WDT underflow/refresh
 * error is bit 1 in each.
 */
#define RA8P1_ICU_NMIER         (RA8P1_ICU_BASE + 0x100UL)  /* enable */
#define RA8P1_ICU_NMICLR        (RA8P1_ICU_BASE + 0x110UL)  /* clear  */
#define RA8P1_ICU_NMISR         (RA8P1_ICU_BASE + 0x120UL)  /* status */
#define RA8P1_ICU_NMI_WDT       (1UL << 1)   /* WDTEN / WDTCLR / WDTST */

/*---------------------------------------------------------------------------*/
/* GLCDC: GRAPHICS LCD CONTROLLER (UM 64)                                    */
/*---------------------------------------------------------------------------*/
/*
 * Shares the graphics power domain with the DRW: PDCTRGD must be open before
 * any of these registers answers.  The background plane generates the
 * timing, and the frame rate is LCDCLK / (BG_PERI.FH * BG_PERI.FV).
 *
 * Register writes are staged and take effect only when a VEN bit is set; a
 * configuration written without that reflect step is ignored.
 */
#define RA8P1_GLCDC_BASE        0x40342000UL
#define RA8P1_GLCDC_BG_EN       (RA8P1_GLCDC_BASE + 0x1000UL)
#define RA8P1_GLCDC_BG_PERI     (RA8P1_GLCDC_BASE + 0x1004UL)
#define RA8P1_GLCDC_BG_SYNC     (RA8P1_GLCDC_BASE + 0x1008UL)
#define RA8P1_GLCDC_BG_VSIZE    (RA8P1_GLCDC_BASE + 0x100CUL)
#define RA8P1_GLCDC_BG_HSIZE    (RA8P1_GLCDC_BASE + 0x1010UL)
#define RA8P1_GLCDC_BG_BGC      (RA8P1_GLCDC_BASE + 0x1014UL)
#define RA8P1_GLCDC_BG_MON      (RA8P1_GLCDC_BASE + 0x1018UL)

#define RA8P1_GLCDC_BG_EN_EN    (1UL << 0)
#define RA8P1_GLCDC_BG_EN_VEN   (1UL << 8)
#define RA8P1_GLCDC_BG_EN_SWRST (1UL << 16)
#define RA8P1_GLCDC_BG_MON_EN   (1UL << 0)

/* Graphics layers, n = 1 or 2. */
#define RA8P1_GLCDC_GR(n, off)  (RA8P1_GLCDC_BASE + 0x1100UL + \
                                 (0x100UL * ((n) - 1UL)) + (off))
#define RA8P1_GLCDC_GR_VEN(n)   RA8P1_GLCDC_GR(n, 0x00UL)
#define RA8P1_GLCDC_GR_FLMRD(n) RA8P1_GLCDC_GR(n, 0x04UL)
#define RA8P1_GLCDC_GR_FLM1(n)  RA8P1_GLCDC_GR(n, 0x08UL)
#define RA8P1_GLCDC_GR_FLM2(n)  RA8P1_GLCDC_GR(n, 0x0CUL)  /* base address */
#define RA8P1_GLCDC_GR_FLM3(n)  RA8P1_GLCDC_GR(n, 0x10UL)  /* line offset  */
#define RA8P1_GLCDC_GR_FLM5(n)  RA8P1_GLCDC_GR(n, 0x18UL)  /* per-line/num */
#define RA8P1_GLCDC_GR_FLM6(n)  RA8P1_GLCDC_GR(n, 0x1CUL)  /* format       */
#define RA8P1_GLCDC_GR_AB1(n)   RA8P1_GLCDC_GR(n, 0x20UL)
#define RA8P1_GLCDC_GR_AB2(n)   RA8P1_GLCDC_GR(n, 0x24UL)  /* vert window */
#define RA8P1_GLCDC_GR_AB3(n)   RA8P1_GLCDC_GR(n, 0x28UL)  /* horiz window */
#define RA8P1_GLCDC_OUT_BRIGHT1 (RA8P1_GLCDC_BASE + 0x13C8UL)
#define RA8P1_GLCDC_OUT_BRIGHT2 (RA8P1_GLCDC_BASE + 0x13CCUL)
#define RA8P1_GLCDC_OUT_CONTRAST (RA8P1_GLCDC_BASE + 0x13D0UL)
#define RA8P1_GLCDC_GR_MON(n)   RA8P1_GLCDC_GR(n, 0x54UL)

#define RA8P1_GLCDC_GR_VEN_PVEN   (1UL << 0)
#define RA8P1_GLCDC_GR_FLMRD_RENB (1UL << 0)
#define RA8P1_GLCDC_GR_FLM6_RGB565 (0UL << 28)  /* FORMAT[2:0] = 000 */
/* DISPSEL 11 blends this layer over what is beneath it, 01 passes the lower
 * layer through, and 00, the reset value, paints the layer's base colour,
 * black, over everything beneath.  Layer 2 sits above layer 1, so with only
 * layer 1 in use, layer 2 is set to pass-through or the output is black. */
#define RA8P1_GLCDC_GR_AB1_DISPSEL_FB   (3UL << 0)
#define RA8P1_GLCDC_GR_AB1_DISPSEL_PASS (1UL << 0)

/* Output correction multiplies, and its reset value of zero scales every
 * pixel to black.  0x80 per channel is unity contrast; 512 is the brightness
 * midpoint. */
#define RA8P1_GLCDC_CONTRAST_UNITY  0x00808080UL
#define RA8P1_GLCDC_BRIGHT_MID      512UL

/* Timing-controller outputs.  The A pair carries the sync pulses and the B
 * pair the data-enable window; SEL in each x2 register routes the signal to a
 * physical TCON pin.  Unset, the panel gets pixels and a clock but no sync
 * or DE, and shows nothing. */
#define RA8P1_GLCDC_OUT_VLATCH  (RA8P1_GLCDC_BASE + 0x13C0UL)
#define RA8P1_GLCDC_OUT_SET     (RA8P1_GLCDC_BASE + 0x13C4UL)
#define RA8P1_GLCDC_TCON_TIM    (RA8P1_GLCDC_BASE + 0x1404UL)
#define RA8P1_GLCDC_TCON_STVA1  (RA8P1_GLCDC_BASE + 0x1408UL)
#define RA8P1_GLCDC_TCON_STVA2  (RA8P1_GLCDC_BASE + 0x140CUL)
#define RA8P1_GLCDC_TCON_STVB1  (RA8P1_GLCDC_BASE + 0x1410UL)
#define RA8P1_GLCDC_TCON_STVB2  (RA8P1_GLCDC_BASE + 0x1414UL)
#define RA8P1_GLCDC_TCON_STHA1  (RA8P1_GLCDC_BASE + 0x1418UL)
#define RA8P1_GLCDC_TCON_STHA2  (RA8P1_GLCDC_BASE + 0x141CUL)
#define RA8P1_GLCDC_TCON_STHB1  (RA8P1_GLCDC_BASE + 0x1420UL)
#define RA8P1_GLCDC_TCON_STHB2  (RA8P1_GLCDC_BASE + 0x1424UL)
#define RA8P1_GLCDC_TCON_DE     (RA8P1_GLCDC_BASE + 0x1428UL)

/* System control: detection arm, interrupt enable, status and its clear.
 * A status flag sets only while its DTCTEN bit is armed. */
#define RA8P1_GLCDC_SYS_DTCTEN  (RA8P1_GLCDC_BASE + 0x1440UL)
#define RA8P1_GLCDC_SYS_INTEN   (RA8P1_GLCDC_BASE + 0x1444UL)
#define RA8P1_GLCDC_SYS_STCLR   (RA8P1_GLCDC_BASE + 0x1448UL)
#define RA8P1_GLCDC_SYS_STMON   (RA8P1_GLCDC_BASE + 0x144CUL)
#define RA8P1_GLCDC_SYS_PANELCLK (RA8P1_GLCDC_BASE + 0x1450UL)
#define RA8P1_GLCDC_SYS_VPOS    (1UL << 0)
#define RA8P1_GLCDC_SYS_L1UNDF  (1UL << 1)
#define RA8P1_GLCDC_SYS_L2UNDF  (1UL << 2)
#define RA8P1_GLCDC_PANELCLK_EN     (1UL << 6)   /* clock output enable    */
#define RA8P1_GLCDC_PANELCLK_LCDCLK (1UL << 8)   /* 0 selects LCD_EXTCLK   */
/* DCDR is the panel-clock divider.  At its reset value of zero there is no
 * pixel clock and no frame, though every other write is accepted.  CLKEN
 * must be low while DCDR or CLKSEL change. */
#define RA8P1_GLCDC_PANELCLK_DCDR(n) ((uint32_t)(n) & 0x3FUL)

/* LCDCKCR source codes beyond MOCO.  PLL1P is the core's source and runs at
 * every operating point. */
#define RA8P1_LCDCKCR_SEL_PLL1P 0x5U
#define RA8P1_LCDCKDIV_4        0x2U

/*
 * LCDCLK (UM 9.2.53/9.2.58).  Changing source or divider is a handshake:
 * request, wait for ready, write, release.  A divider write outside it fails
 * with no error.  The reset source is MOCO.
 */
#define RA8P1_LCDCKDIVCR        (RA8P1_SYSC_BASE + 0x05EUL)   /* 8-bit */
#define RA8P1_LCDCKCR           (RA8P1_SYSC_BASE + 0x05FUL)   /* 8-bit */
#define RA8P1_LCDCKCR_SEL_MOCO  0x1U
#define RA8P1_LCDCKCR_SREQ      (1U << 6)
#define RA8P1_LCDCKCR_SRDY      (1U << 7)

/*---------------------------------------------------------------------------*/
/* IIC: I2C BUS INTERFACE (UM 40)                                            */
/*---------------------------------------------------------------------------*/
/*
 * Three channels 0x100 apart.  The camera and touch controller on the
 * expansion boards share channel 1: SCL1 on P512, SDA1 on P511, both at
 * peripheral select 00111b.
 */
#define RA8P1_IIC_BASE(n)       (0x4025E000UL + (0x100UL * (n)))
#define RA8P1_IIC_CCR1(n)       (RA8P1_IIC_BASE(n) + 0x00UL)
#define RA8P1_IIC_CCR2(n)       (RA8P1_IIC_BASE(n) + 0x01UL)
#define RA8P1_IIC_MR1(n)        (RA8P1_IIC_BASE(n) + 0x02UL)
#define RA8P1_IIC_MR2(n)        (RA8P1_IIC_BASE(n) + 0x03UL)
#define RA8P1_IIC_MR3(n)        (RA8P1_IIC_BASE(n) + 0x04UL)
#define RA8P1_IIC_FER(n)        (RA8P1_IIC_BASE(n) + 0x05UL)
#define RA8P1_IIC_SER(n)        (RA8P1_IIC_BASE(n) + 0x06UL)
#define RA8P1_IIC_IER(n)        (RA8P1_IIC_BASE(n) + 0x07UL)
#define RA8P1_IIC_SR1(n)        (RA8P1_IIC_BASE(n) + 0x08UL)
#define RA8P1_IIC_SR2(n)        (RA8P1_IIC_BASE(n) + 0x09UL)
#define RA8P1_IIC_BRL(n)        (RA8P1_IIC_BASE(n) + 0x10UL)
#define RA8P1_IIC_BRH(n)        (RA8P1_IIC_BASE(n) + 0x11UL)
#define RA8P1_IIC_DRT(n)        (RA8P1_IIC_BASE(n) + 0x12UL)
#define RA8P1_IIC_DRR(n)        (RA8P1_IIC_BASE(n) + 0x13UL)

/* ICCR1: ICE enables the unit, IICRST resets it; the two together choose
 * between a full reset and an internal one, so both are written explicitly. */
#define RA8P1_IIC_CCR1_ICE      (1U << 7)
#define RA8P1_IIC_CCR1_IICRST   (1U << 6)
#define RA8P1_IIC_CCR1_SOWP     (1U << 4)

/* ICCR2: the condition requests, and who drives the bus. */
#define RA8P1_IIC_CCR2_ST       (1U << 1)
#define RA8P1_IIC_CCR2_RS       (1U << 2)
#define RA8P1_IIC_CCR2_SP       (1U << 3)
#define RA8P1_IIC_CCR2_TRS      (1U << 5)
#define RA8P1_IIC_CCR2_MST      (1U << 6)
#define RA8P1_IIC_CCR2_BBSY     (1U << 7)

/* ICSR2 flags.  NACKF sets when the addressed device does not acknowledge,
 * the only sign of an absent device. */
#define RA8P1_IIC_SR2_TMOF      (1U << 0)
#define RA8P1_IIC_SR2_AL        (1U << 1)
#define RA8P1_IIC_SR2_START     (1U << 2)
#define RA8P1_IIC_SR2_STOP      (1U << 3)
#define RA8P1_IIC_SR2_NACKF     (1U << 4)
#define RA8P1_IIC_SR2_RDRF      (1U << 5)
#define RA8P1_IIC_SR2_TEND      (1U << 6)
#define RA8P1_IIC_SR2_TDRE      (1U << 7)

/* ICMR3: WAIT holds the clock before the last byte so the master can send a
 * NACK; RDRFS makes the receive flag rise in time for that. */
#define RA8P1_IIC_MR3_ACKBT     (1U << 3)
#define RA8P1_IIC_MR3_ACKWP     (1U << 4)
#define RA8P1_IIC_MR3_RDRFS     (1U << 5)
#define RA8P1_IIC_MR3_WAIT      (1U << 6)

/** @brief Module stop: MSTPCRB.MSTPB8 gates IIC1 (MSTPB9 is IIC0). */
#define RA8P1_MSTPCRB_IIC1      (1UL << 8)

/** @brief Pin function select for the IIC peripheral. */
#define RA8P1_PFS_PSEL_IIC      0x07UL

/*---------------------------------------------------------------------------*/
/* DRW: 2D DRAWING ENGINE (UM 63)                                            */
/*---------------------------------------------------------------------------*/
/*
 * A D/AVE-class rasteriser: up to six "limiters" each describe a half plane
 * as a decision value that steps by a per-x and per-y increment across a
 * bounding box; their intersection is the shape, and the clamped result is
 * the pixel alpha.  The CPU computes the corner value and the increments.
 *
 * Offsets 0x00 and 0x04 are different registers by direction: 0x00 is
 * CONTROL when written and STATUS when read, 0x04 CONTROL2 and HWREVISION.
 * A read-modify-write on either feeds status bits back as control, so both
 * are only ever written whole.  Writing ORIGIN starts the render (UM
 * 63.7.1), so it is written after every other register.
 */
#define RA8P1_DRW_BASE          0x40444000UL
#define RA8P1_DRW_CONTROL       (RA8P1_DRW_BASE + 0x00UL)  /* W */
#define RA8P1_DRW_STATUS        (RA8P1_DRW_BASE + 0x00UL)  /* R */
#define RA8P1_DRW_CONTROL2      (RA8P1_DRW_BASE + 0x04UL)  /* W */
#define RA8P1_DRW_HWREVISION    (RA8P1_DRW_BASE + 0x04UL)  /* R */
#define RA8P1_DRW_LSTART(n)     (RA8P1_DRW_BASE + 0x10UL + (4UL * (n)))
#define RA8P1_DRW_LXADD(n)      (RA8P1_DRW_BASE + 0x28UL + (4UL * (n)))
#define RA8P1_DRW_LYADD(n)      (RA8P1_DRW_BASE + 0x40UL + (4UL * (n)))
#define RA8P1_DRW_COLOR1        (RA8P1_DRW_BASE + 0x64UL)
#define RA8P1_DRW_COLOR2        (RA8P1_DRW_BASE + 0x68UL)
#define RA8P1_DRW_PATTERN       (RA8P1_DRW_BASE + 0x74UL)
#define RA8P1_DRW_SIZE          (RA8P1_DRW_BASE + 0x78UL)
#define RA8P1_DRW_PITCH         (RA8P1_DRW_BASE + 0x7CUL)
#define RA8P1_DRW_ORIGIN        (RA8P1_DRW_BASE + 0x80UL)  /* W: render start */
#define RA8P1_DRW_IRQCTL        (RA8P1_DRW_BASE + 0xC0UL)
#define RA8P1_DRW_CACHECTL      (RA8P1_DRW_BASE + 0xC4UL)
#define RA8P1_DRW_DLISTSTART    (RA8P1_DRW_BASE + 0xC8UL)
#define RA8P1_DRW_COLKEY        (RA8P1_DRW_BASE + 0xE8UL)

/* CONTROL: one enable per limiter, bits 0..5.  A QUAD bit couples a pair of
 * limiters into one quadratic, which is how the engine draws a circle: the
 * first of the pair carries the value and its x/y steps, the second carries
 * the steps' own steps (UM 63.6.2.2). */
#define RA8P1_DRW_CTL_LIMEN(n)  (1UL << (n))
#define RA8P1_DRW_CTL_QUAD1     (1UL << 6)   /* couples limiters 1 and 2 */

/* CONTROL2: the framebuffer format spans bit 8 and bits 21:20; 0b001
 * selects 16 bpp RGB565, the format this port renders in. */
#define RA8P1_DRW_CTL2_WRFMT_RGB565  (1UL << 20)

/*
 * Blending is src*SF + dst*DF, and both factors default to 1.0, so a fill
 * left at reset adds to what is already there; over a black destination
 * that looks the same as replacing it.
 *
 * Taking both factors from the limiters' alpha gives src*a + dst*(1-a), the
 * ordinary over-composite.  A rectangle's alpha is one across its whole
 * bounding box; any other shape needs these factors, or every pixel of the
 * bounding box is written and a circle comes out square.
 */
#define RA8P1_DRW_CTL2_BSF           (1UL << 9)   /* source factor = alpha  */
#define RA8P1_DRW_CTL2_BDF           (1UL << 10)  /* dest factor  = alpha   */
#define RA8P1_DRW_CTL2_BDI           (1UL << 12)  /* ...inverted, so 1-a    */
#define RA8P1_DRW_CTL2_OVER          (RA8P1_DRW_CTL2_BSF | \
                                      RA8P1_DRW_CTL2_BDF | \
                                      RA8P1_DRW_CTL2_BDI)

/* STATUS: the render is done when neither unit is busy.  DLISTACTIVE must
 * also be clear before a new register-mode setup (UM 63.7.1). */
#define RA8P1_DRW_ST_BUSYENUM   (1UL << 0)
#define RA8P1_DRW_ST_BUSYWRITE  (1UL << 1)

/** @brief Module stop: MSTPCRC.MSTPC6 gates the DRW; MSTPC4 gates the GLCDC. */
#define RA8P1_MSTPCRC_DRW       (1UL << 6)
#define RA8P1_MSTPCRC_GLCDC     (1UL << 4)
#define RA8P1_MSTPCRC_MIPI_CSI  (1UL << 17)  /* CSI receiver + VIN together */

/*---------------------------------------------------------------------------*/
/* MIPI D-PHY (UM 65), CSI-2 RECEIVER (UM 67), VIDEO INPUT (UM 68)           */
/*---------------------------------------------------------------------------*/

/** @brief D-PHY, shared between DSI and CSI; CSI uses it as a receiver. */
#define RA8P1_DPHY_BASE         0x40346C00UL
#define RA8P1_DPHY_REFCR        (RA8P1_DPHY_BASE + 0x00UL)  /* ref frequency */
#define RA8P1_DPHY_PLOCR        (RA8P1_DPHY_BASE + 0x08UL)  /* PLL stop      */
#define RA8P1_DPHY_PWRCR        (RA8P1_DPHY_BASE + 0x10UL)  /* power enable  */
#define RA8P1_DPHY_SFR          (RA8P1_DPHY_BASE + 0x1CUL)  /* status flags  */
#define RA8P1_DPHY_OCR          (RA8P1_DPHY_BASE + 0x20UL)  /* operation en  */
#define RA8P1_DPHY_TIM1         (RA8P1_DPHY_BASE + 0x24UL)  /* T_INIT        */
#define RA8P1_DPHY_TIM2         (RA8P1_DPHY_BASE + 0x28UL)  /* clk prep/sett */
#define RA8P1_DPHY_TIM3         (RA8P1_DPHY_BASE + 0x2CUL)  /* hs prep/sett  */
#define RA8P1_DPHY_TIM4         (RA8P1_DPHY_BASE + 0x30UL)  /* clk zero..trl */
#define RA8P1_DPHY_TIM5         (RA8P1_DPHY_BASE + 0x34UL)  /* hs zero..exit */
#define RA8P1_DPHY_TIM6         (RA8P1_DPHY_BASE + 0x38UL)  /* T_LPX         */
#define RA8P1_DPHY_MDC          (RA8P1_DPHY_BASE + 0x48UL)  /* master enable */

#define RA8P1_DPHY_PWRCR_PWRSEN (1UL << 0)
#define RA8P1_DPHY_SFR_PWRSF    (1UL << 0)
#define RA8P1_DPHY_OCR_DPHYEN   (1UL << 0)

/** @brief CSI-2 receiver; interrupt enables stay 0, this port polls. */
#define RA8P1_CSI_BASE          0x40347000UL
#define RA8P1_CSI_MCT0          (RA8P1_CSI_BASE + 0x010UL)
#define RA8P1_CSI_MCT2          (RA8P1_CSI_BASE + 0x018UL)
#define RA8P1_CSI_MCT3          (RA8P1_CSI_BASE + 0x01CUL)
#define RA8P1_CSI_RTCT          (RA8P1_CSI_BASE + 0x028UL)
#define RA8P1_CSI_RTST          (RA8P1_CSI_BASE + 0x02CUL)
#define RA8P1_CSI_EPCT          (RA8P1_CSI_BASE + 0x040UL)
#define RA8P1_CSI_EMCT          (RA8P1_CSI_BASE + 0x044UL)
#define RA8P1_CSI_DTEL          (RA8P1_CSI_BASE + 0x060UL)
#define RA8P1_CSI_DTEH          (RA8P1_CSI_BASE + 0x064UL)
#define RA8P1_CSI_RXST          (RA8P1_CSI_BASE + 0x070UL)
#define RA8P1_CSI_RXSC          (RA8P1_CSI_BASE + 0x074UL)
#define RA8P1_CSI_DLST0         (RA8P1_CSI_BASE + 0x080UL)
#define RA8P1_CSI_DLST1         (RA8P1_CSI_BASE + 0x090UL)
#define RA8P1_CSI_VCST0         (RA8P1_CSI_BASE + 0x100UL)
#define RA8P1_CSI_PMST          (RA8P1_CSI_BASE + 0x200UL)
#define RA8P1_CSI_GSCT          (RA8P1_CSI_BASE + 0x280UL)

#define RA8P1_CSI_MCT3_RXEN     (1UL << 0)
#define RA8P1_CSI_RTST_VSRSTS   (1UL << 0)
/* MCT0: two lanes, no zero-length output, error-frame notify, reserved-
 * packet pass, generic CSI-2 rule (the manual: "set to 1 for this LSI"),
 * 24-bit ECC check. */
#define RA8P1_CSI_MCT0_2LANE    (2UL | (1UL << 16) | (1UL << 17) | \
                                 (1UL << 19) | (1UL << 20) | (1UL << 24))

/** @brief Video input unit: preclip, colour conversion, memory writer. */
#define RA8P1_VIN_BASE          0x40347400UL
#define RA8P1_VIN_MC            (RA8P1_VIN_BASE + 0x000UL)
#define RA8P1_VIN_MS            (RA8P1_VIN_BASE + 0x004UL)
#define RA8P1_VIN_FC            (RA8P1_VIN_BASE + 0x008UL)
#define RA8P1_VIN_SLPRC         (RA8P1_VIN_BASE + 0x00CUL)
#define RA8P1_VIN_ELPRC         (RA8P1_VIN_BASE + 0x010UL)
#define RA8P1_VIN_SPPRC         (RA8P1_VIN_BASE + 0x014UL)
#define RA8P1_VIN_EPPRC         (RA8P1_VIN_BASE + 0x018UL)
#define RA8P1_VIN_CSI_IFMD      (RA8P1_VIN_BASE + 0x020UL)
#define RA8P1_VIN_CSIFLD        (RA8P1_VIN_BASE + 0x024UL)
#define RA8P1_VIN_IS            (RA8P1_VIN_BASE + 0x02CUL)
#define RA8P1_VIN_MB1           (RA8P1_VIN_BASE + 0x030UL)
#define RA8P1_VIN_MB2           (RA8P1_VIN_BASE + 0x034UL)
#define RA8P1_VIN_MB3           (RA8P1_VIN_BASE + 0x038UL)
#define RA8P1_VIN_LC            (RA8P1_VIN_BASE + 0x03CUL)
#define RA8P1_VIN_IE            (RA8P1_VIN_BASE + 0x040UL)
#define RA8P1_VIN_INTS          (RA8P1_VIN_BASE + 0x044UL)
#define RA8P1_VIN_DMR           (RA8P1_VIN_BASE + 0x058UL)
#define RA8P1_VIN_UVAOF         (RA8P1_VIN_BASE + 0x060UL)
#define RA8P1_VIN_UDS_CTRL      (RA8P1_VIN_BASE + 0x080UL)

#define RA8P1_VIN_MC_ME         (1UL << 0)
#define RA8P1_VIN_MC_ST         (1UL << 22)
#define RA8P1_VIN_FC_CC         (1UL << 1)
#define RA8P1_VIN_INTS_FIS      (1UL << 4)   /* frame write complete */
/* MC value for this pipeline: odd/even interlace handling, YCbCr422 8-bit
 * in, dithering direction on; conversion stays enabled (BPS clear). */
#define RA8P1_VIN_MC_CFG        ((1UL << 3) | (1UL << 16) | (1UL << 24))
/* CSI_IFMD: virtual channel 0, data type YUV422 8-bit, zero-extension. */
#define RA8P1_VIN_IFMD_CFG      ((0x1EUL << 8) | (1UL << 25))
/* DMR: no further conversion after the colour-space core, alpha bit one,
 * output byte swap on, spare alpha byte at the vendor's value. */
#define RA8P1_VIN_DMR_CFG       (0xAA000014UL)

/*---------------------------------------------------------------------------*/
/* CORTEX-M85 CORE PERIPHERALS (ARMV8.1-M)                                   */
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
/* SAU (Armv8-M ARM B8.3): readable only from the secure state, so a read
 * shows which security state the image runs in. */
#define RA8P1_SAU_CTRL          0xE000EDD0UL
#define RA8P1_SAU_TYPE          0xE000EDD4UL
#define RA8P1_NVIC_ISER(i)      (0xE000E100UL + (4UL * (i)))
#define RA8P1_NVIC_ICER(i)      (0xE000E180UL + (4UL * (i)))
#define RA8P1_NVIC_ICPR(i)      (0xE000E280UL + (4UL * (i)))
/*
 * PMSAv8 MPU (Armv8.1-M ARM B11).  The region attributes also set
 * cacheability, through MAIR, so the caches are enabled only after the
 * regions exist; until then the M85 uses the default memory map's
 * attributes.
 */
#define RA8P1_MPU_BASE          0xE000ED90UL
#define RA8P1_MPU_TYPE          (RA8P1_MPU_BASE + 0x00UL)  /* DREGION count */
#define RA8P1_MPU_CTRL          (RA8P1_MPU_BASE + 0x04UL)
#define RA8P1_MPU_RNR           (RA8P1_MPU_BASE + 0x08UL)
#define RA8P1_MPU_RBAR          (RA8P1_MPU_BASE + 0x0CUL)
#define RA8P1_MPU_RLAR          (RA8P1_MPU_BASE + 0x10UL)
#define RA8P1_MPU_MAIR0         (RA8P1_MPU_BASE + 0x30UL)
#define RA8P1_MPU_MAIR1         (RA8P1_MPU_BASE + 0x34UL)
#define RA8P1_MPU_TYPE_DREGION_SHIFT  8U
#define RA8P1_MPU_TYPE_DREGION_MASK   0xFFU

#define RA8P1_MPU_CTRL_ENABLE       (1UL << 0)
#define RA8P1_MPU_CTRL_HFNMIENA     (1UL << 1)
#define RA8P1_MPU_CTRL_PRIVDEFENA   (1UL << 2)

/* RBAR: base[31:5] | SH[4:3] | AP[2:1] | XN[0]  (ARM B11.2.9) */
#define RA8P1_MPU_RBAR_AP_RW        (0x1UL << 1)   /* RW at any privilege */
#define RA8P1_MPU_RBAR_AP_RO        (0x3UL << 1)   /* RO at any privilege */
#define RA8P1_MPU_RBAR_AP_MASK      (0x3UL << 1)
#define RA8P1_MPU_RBAR_XN           (1UL << 0)
/* RLAR: limit[31:5] | AttrIndx[3:1] | EN[0]  (ARM B11.2.10) */
#define RA8P1_MPU_RLAR_ATTR(i)      (((uint32_t)(i) & 0x7UL) << 1)
#define RA8P1_MPU_RLAR_EN           (1UL << 0)

/*
 * MAIR attribute bytes.  Index 0 is Normal write-back read/write-allocate,
 * which lets the D-cache cache SRAM; index 1 is Device nGnRE, for
 * peripherals, which are never cached or accessed speculatively.
 */
#define RA8P1_MPU_MAIR_NORMAL_WB    0xFFU
#define RA8P1_MPU_MAIR_DEVICE       0x04U
/* Normal, non-cacheable inner and outer (0b0100_0100), for the durable MRAM
 * carve.  A D-cache line is 32 bytes, one MRAM program granule: a cached
 * store would stay in the line instead of reaching the program buffer, and
 * its eviction would program the whole granule at an arbitrary time. */
#define RA8P1_MPU_MAIR_NORMAL_NC    0x44U
#define RA8P1_MPU_ATTR_NORMAL       0U
#define RA8P1_MPU_ATTR_DEVICE       1U
#define RA8P1_MPU_ATTR_NORMAL_NC    2U

/* Cortex-M85 cache control (SCB CCR / cache maintenance, ARM B) */
#define RA8P1_SCB_CCR           0xE000ED14UL
#define RA8P1_SCB_CCR_IC        (1UL << 17)
#define RA8P1_SCB_CCR_DC        (1UL << 16)
#define RA8P1_SCB_CLIDR         0xE000ED78UL
#define RA8P1_SCB_CTR           0xE000ED04UL
#define RA8P1_SCB_CCSIDR        0xE000ED80UL
#define RA8P1_SCB_CSSELR        0xE000ED84UL
#define RA8P1_SCB_ICIALLU       0xE000EF50UL
#define RA8P1_SCB_DCIMVAC       0xE000EF5CUL
#define RA8P1_SCB_DCISW         0xE000EF60UL
#define RA8P1_SCB_DCCMVAC       0xE000EF68UL
#define RA8P1_SCB_DCCSW         0xE000EF6CUL
#define RA8P1_SCB_DCCIMVAC      0xE000EF70UL
#define RA8P1_SCB_DCCISW        0xE000EF74UL

/** @brief Cortex-M85 cache line, in bytes; range ops round to it. */
#define RA8P1_CACHE_LINE        32UL

/* MemManage: SHCSR enables the fault, CFSR's low byte reports it. */
#define RA8P1_SCB_SHCSR         0xE000ED24UL
#define RA8P1_SCB_SHCSR_MEMFAULTENA (1UL << 16)
#define RA8P1_SCB_CFSR          0xE000ED28UL
#define RA8P1_SCB_MMFAR         0xE000ED34UL
#define RA8P1_CFSR_MMARVALID    (1UL << 7)
#define RA8P1_CFSR_DACCVIOL     (1UL << 1)
#define RA8P1_CFSR_IACCVIOL     (1UL << 0)

#define RA8P1_SCB_HFSR          0xE000ED2CUL
#define RA8P1_SCB_BFAR          0xE000ED38UL
#define RA8P1_CFSR_BFARVALID    (1UL << 15)
/* Any of the four stacking/unstacking errors means the exception frame push
 * itself faulted, so the words it points at are not the faulting context. */
#define RA8P1_CFSR_STKERR_MSK   ((1UL << 4) | (1UL << 3) | \
                                 (1UL << 12) | (1UL << 11))
#define RA8P1_SCB_SHCSR_BUSFAULTENA  (1UL << 17)
#define RA8P1_SCB_SHCSR_USGFAULTENA  (1UL << 18)

#define RA8P1_SCB_AIRCR         0xE000ED0CUL
#define RA8P1_AIRCR_VECTKEY     0x05FA0000UL
#define RA8P1_AIRCR_SYSRESETREQ (1UL << 2)

/*---------------------------------------------------------------------------*/
/* ETHOS-U55 NPU (UM CH.19 AND THE ARM TRM)                                  */
/*---------------------------------------------------------------------------*/

/*
 * UM Table 19.1 refers to the Arm TRM for the registers, so the offsets below
 * are the TRM's; ID and CONFIG are confirmed on the part.
 *
 * The domain is power-gated and module-stopped out of reset, and every
 * register here reads 0 until both are released, in the order UM 11.5.1
 * states: the module-stop bit must not move before the domain is powered
 * (UM 11.2.6, MSTPA16 note 3).
 */

/** @brief NPUCBI, the NPU's 4 KB register window (UM Table, peripheral map). */
#define RA8P1_NPU_BASE          0x40140000UL

#define RA8P1_NPU_ID            (RA8P1_NPU_BASE + 0x000UL)
#define RA8P1_NPU_STATUS        (RA8P1_NPU_BASE + 0x004UL)
#define RA8P1_NPU_CMD           (RA8P1_NPU_BASE + 0x008UL)
#define RA8P1_NPU_RESET         (RA8P1_NPU_BASE + 0x00CUL)
#define RA8P1_NPU_QBASE         (RA8P1_NPU_BASE + 0x010UL)
#define RA8P1_NPU_QREAD         (RA8P1_NPU_BASE + 0x018UL)
#define RA8P1_NPU_QCONFIG       (RA8P1_NPU_BASE + 0x01CUL)
#define RA8P1_NPU_QSIZE         (RA8P1_NPU_BASE + 0x020UL)
#define RA8P1_NPU_PROT          (RA8P1_NPU_BASE + 0x024UL)
#define RA8P1_NPU_CONFIG        (RA8P1_NPU_BASE + 0x028UL)

/** @brief What a released NPU answers on this die; 0 means still gated. */
#define RA8P1_NPU_ID_EXPECT     0x10104201UL

/*
 * CONFIG.macs_per_cc is log2 of the MAC count: 8 means 256 MACs per cycle,
 * the count UM Table 19.1 states.  Vela's accelerator configuration is
 * chosen by that count, and a stream compiled for another does not run.
 */
#define RA8P1_NPU_CONFIG_MACS_SHIFT  0U
#define RA8P1_NPU_CONFIG_MACS_MASK   0xFUL
#define RA8P1_NPU_CONFIG_SHRAM_SHIFT 8U
#define RA8P1_NPU_CONFIG_SHRAM_MASK  0xFFUL

/*
 * Graphics power domain (UM 11.2.14).  DRW, GLCDC, MIPI DSI and MIPI CSI sit
 * inside it, and it is powered off at reset: PDCTRGD reads 0x81, PDDE set
 * and PDPGSF confirming the gate.  While the domain is off, every register
 * in those blocks reads zero with no fault, whatever the module stop says.
 * Same bit layout as PDCTRNPU below.
 */
#define RA8P1_PDCTRGD           (RA8P1_SYSC_BASE + 0x110UL)   /* 8-bit */
#define RA8P1_PDCTRGD_PDDE      (1U << 0)   /* 1 = power the domain off    */
#define RA8P1_PDCTRGD_PDCSF     (1U << 6)   /* gating control in progress  */
#define RA8P1_PDCTRGD_PDPGSF    (1U << 7)   /* 1 = domain is gated off     */

/** @brief Power gating for the NPU domain (UM 11.2.15); PDDE is inverted:
 *         0 powers the domain on.  Resets to 0x81, gated and held off. */
#define RA8P1_PDCTRNPU          (RA8P1_SYSC_BASE + 0x114UL)   /* 8-bit */
#define RA8P1_PDCTRNPU_PDDE     (1U << 0)   /* 1 = power the domain off    */
#define RA8P1_PDCTRNPU_PDCSF    (1U << 6)   /* gating control in progress  */
#define RA8P1_PDCTRNPU_PDPGSF   (1U << 7)   /* 1 = domain is gated off     */

/** @brief MSTPCRA gates the NPU module itself (UM 11.2.6, MSTPA16).  Bits
 *         21:17 read as 1 and must be written back as 1. */
#define RA8P1_MSTPCRA           (RA8P1_MSTP_BASE + 0x00UL)
#define RA8P1_MSTPA_NPU         (1UL << 16)

/*
 * AXI limits and region configuration.  The block powers up with these at
 * zero, which allows one outstanding read and one outstanding write, and a
 * stream submitted against that faults on its first data access.
 * RA8P1_NPU_AXI_LIMIT sets 32 outstanding reads and 16 writes, each stored
 * minus one, the Arm core driver's defaults.
 */
#define RA8P1_NPU_AXI_LIMIT     0x0F1F0000UL
/* Every region and the command queue use limit set 0.  Sets 1-3 reach the
 * NPU's second AXI master, which is wired to external flash on this part;
 * a model in SRAM is reached through set 0. */
#define RA8P1_NPU_REGIONCFG_DEFAULT 0x00000000UL
#define RA8P1_NPU_QCONFIG_DEFAULT   0UL
#define RA8P1_NPU_AXI_LIMIT0    (RA8P1_NPU_BASE + 0x040UL)
#define RA8P1_NPU_AXI_LIMIT1    (RA8P1_NPU_BASE + 0x044UL)
#define RA8P1_NPU_AXI_LIMIT2    (RA8P1_NPU_BASE + 0x048UL)
#define RA8P1_NPU_AXI_LIMIT3    (RA8P1_NPU_BASE + 0x04CUL)

/** @brief ICU event for NPU_IRQ, to be routed to an NVIC line (UM ch.13). */
#define RA8P1_ICU_EVENT_NPU_IRQ 0x067U

/** @brief MOCO control (UM 9.2.18).  Power gating requires the MOCO
 *         running, and the NPU bring-up checks it first. */
#define RA8P1_MOCOCR            (RA8P1_SYSC_BASE + 0x038UL)   /* 8-bit */
#define RA8P1_MOCOCR_MCSTP      (1U << 0)   /* 1 = MOCO stopped */

/* The Ethos-U55 RESET, CMD and STATUS bits this port uses. */
#define RA8P1_NPU_RESET_CPL     (1UL << 0)   /* 1 = privileged        */
#define RA8P1_NPU_RESET_CSL     (1UL << 1)   /* 1 = non-secure        */
#define RA8P1_NPU_STATUS_RESET  (1UL << 3)   /* reset in progress     */

#define RA8P1_NPU_CMD_RUN       (1UL << 0)
#define RA8P1_NPU_CMD_CLEAR_IRQ (1UL << 1)
#define RA8P1_NPU_CMD_CLK_Q_EN  (1UL << 2)
#define RA8P1_NPU_CMD_PWR_Q_EN  (1UL << 3)
#define RA8P1_NPU_CMD_STOP_REQ  (1UL << 4)

#define RA8P1_NPU_STATUS_STATE  (1UL << 0)   /* 1 = running          */
#define RA8P1_NPU_STATUS_IRQ    (1UL << 1)
#define RA8P1_NPU_STATUS_BUSERR (1UL << 2)
#define RA8P1_NPU_STATUS_PARSE  (1UL << 4)   /* command parse error  */
#define RA8P1_NPU_STATUS_END    (1UL << 5)   /* command end reached  */

/* Queue and region base pointers; each base is a 64-bit pair. */
#define RA8P1_NPU_QBASE_HI      (RA8P1_NPU_BASE + 0x014UL)
#define RA8P1_NPU_QREGIONCFG    (RA8P1_NPU_BASE + 0x03CUL)
#define RA8P1_NPU_BASEP(n)      (RA8P1_NPU_BASE + 0x080UL + (8UL * (n)))

#endif /* TIKU_RA8P1_REGS_H_ */
