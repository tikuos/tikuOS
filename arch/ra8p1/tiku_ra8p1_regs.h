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
 * named beside each block so a reader can check it without guessing.  The
 * SECURE aliases are used throughout: the Security Extension is enabled on
 * this part and the port runs entirely in the secure world (see the R2 note
 * in kintsugi/ra8p1-port.md).
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

/*---------------------------------------------------------------------------*/
/* System control (UM 9, "Clock Generation Circuit")                         */
/*---------------------------------------------------------------------------*/
#define RA8P1_SYSC_BASE         0x4001E000UL
#define RA8P1_SCKDIVCR          (RA8P1_SYSC_BASE + 0x020UL)  /* UM 9.2.2  */
#define RA8P1_SCKSCR            (RA8P1_SYSC_BASE + 0x026UL)  /* UM 9.2.5  */

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
#define RA8P1_SCI_CCR0_RE       (1UL << 0)
#define RA8P1_SCI_CCR0_TE       (1UL << 4)
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

#endif /* TIKU_RA8P1_REGS_H_ */
