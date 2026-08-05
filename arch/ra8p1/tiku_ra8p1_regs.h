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

/*
 * Octal-SPI clock.  Another private clock register in the SCICKCR/GTCLKCR
 * family: the OSPI module has no divider of its own, so OCTACLK is the only
 * thing that sets the flash bus speed, and it comes out of reset on MOCO.
 * OM_SCLK is OCTACLK divided by two (UM Table 4.4), which is the whole
 * reason a 240 MHz source is the right one rather than an overclock.
 * PRCR.PRC0 gates writes here, as with every clock register.
 */
#define RA8P1_OCTACKDIVCR       (RA8P1_SYSC_BASE + 0x06DUL)  /* 8-bit */
#define RA8P1_OCTACKCR          (RA8P1_SYSC_BASE + 0x075UL)  /* 8-bit */
#define RA8P1_OCTACKCR_SEL_MASK 0x0FU
#define RA8P1_OCTACKCR_SEL_MOCO 0x01U
#define RA8P1_OCTACKCR_SEL_PLL1P 0x05U
#define RA8P1_OCTACKCR_SREQ     (1U << 6)
#define RA8P1_OCTACKCR_SRDY     (1U << 7)
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
/* Octal SPI controller (UM 45) -- EK-RA8P1 U3, MX25LW51245G, 64 MB          */
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

/* Manual command: the path that issues one transaction and reads its reply,
 * which is how a device is identified before any memory-map mode is set up. */
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

/* BMCTL0: two bits per (channel, chip select).  01 = read enable. */
#define RA8P1_BMCTL0_CH0CS1_RD    (1UL << 2)

/*
 * LIOCFGCSn protocol mode.  Only the listed encodings exist -- the manual
 * marks every other value "setting prohibited" -- and the list is the reason
 * this port runs the flash in DTR and not STR octal: there is a 8D-8D-8D
 * encoding and NO 8S-8S-8S one, so the simpler-looking STR OPI mode has no
 * way to be expressed on this controller at all.
 */
#define RA8P1_LIOCFG_PRTMD_MASK   0x3FFUL
#define RA8P1_LIOCFG_PRTMD_1S1S1S 0x000UL
#define RA8P1_LIOCFG_PRTMD_8D8D8D 0x3FFUL
/* Extends the DDR sampling window; how far depends on the memory's output
 * delay, so it is calibrated against real data rather than assumed. */
#define RA8P1_LIOCFG_DDRSMPEX(n)  (((uint32_t)(n) & 0xFUL) << 28)
#define RA8P1_LIOCFG_DDRSMPEX_MASK (0xFUL << 28)
#define RA8P1_LIOCFG_CSMIN(n)     (((uint32_t)(n) & 0xFUL) << 16)
#define RA8P1_LIOCFG_CSMIN_MASK   (0xFUL << 16)

/*
 * WRAPCFG.DSSFTCSn -- delay cells on the OM_DQS input, 0..31.  The
 * configuration flow calls this "drive/sample timing"; leaving it at its
 * reset 0 puts the strobe on the data transition, so one of the two DDR
 * edges captures garbage and every other byte comes back wrong.
 */
#define RA8P1_WRAPCFG_DSSFTCS0(n)   (((uint32_t)(n) & 0x1FUL) << 8)
#define RA8P1_WRAPCFG_DSSFTCS0_MASK (0x1FUL << 8)
#define RA8P1_WRAPCFG_DSSFTCS1(n)   (((uint32_t)(n) & 0x1FUL) << 24)
#define RA8P1_WRAPCFG_DSSFTCS1_MASK (0x1FUL << 24)
#define RA8P1_OSPI_COMSTT(n)    (RA8P1_OSPI_BASE(n) + 0x184UL)

/* LIOCTL.RSTCS0 drives the OM_RESET pin: 0 = LOW.  It RESETS TO 0, so the
 * instant the reset pin is muxed to the OSPI function the controller holds
 * the flash in hardware reset until software says otherwise. */
#define RA8P1_OSPI_LIOCTL_RSTCS0 (1UL << 16)

/* Mapped windows, from the address map: OSPI0 gets 256 MB per chip select,
 * OSPI1 gets 128 MB. */
#define RA8P1_OSPI0_CS0_ADDR    0x80000000UL
#define RA8P1_OSPI1_CS0_ADDR    0x70000000UL

/* Manual-command transaction descriptor (CDTBUFn).  Sizes are byte COUNTS,
 * not encodings, except that command size 10b means 2 bytes -- which is what
 * 8D-8D-8D octal needs, where opcodes are sent as a value and its complement. */
#define RA8P1_CDTBUF_CMDSIZE(n) (((uint32_t)(n) & 0x3UL) << 0)    /*  1:0 */
#define RA8P1_CDTBUF_ADDSIZE(n) (((uint32_t)(n) & 0x7UL) << 2)    /*  4:2 */
#define RA8P1_CDTBUF_DATASIZE(n) (((uint32_t)(n) & 0xFUL) << 5)   /*  8:5 */
#define RA8P1_CDTBUF_LATE(n)    (((uint32_t)(n) & 0x1FUL) << 9)   /* 13:9 */
#define RA8P1_CDTBUF_CMD(c)     (((uint32_t)(c) & 0xFFFFUL) << 16)
#define RA8P1_CDTBUF_TRTYPE_WRITE (1UL << 15)   /* 0 = read from the slave */

#define RA8P1_CDCTL0_TRREQ      (1UL << 0)   /* self-clears on completion */
#define RA8P1_CDCTL0_PERMD      (1UL << 1)
#define RA8P1_CDCTL0_CSSEL      (1UL << 3)   /* 0 = CS0, 1 = CS1 */

/** @brief Module stop: OSPI0 is MSTPB16, OSPI1 (the board's flash) MSTPB17. */
#define RA8P1_MSTPB_OSPI0       (1UL << 16)
#define RA8P1_MSTPB_OSPI1       (1UL << 17)

/** @brief PSEL that selects the OSPI function on any pin (OM_1_* here). */
#define RA8P1_PFS_PSEL_OSPI     0x1CUL

/** @brief Macronix RDID reply: C2 = manufacturer, then type and density. */
#define RA8P1_MX_MANUFACTURER   0xC2U
#define RA8P1_MX_CMD_RDID       0x9FU

/*---------------------------------------------------------------------------*/
/* External bus / SDRAM controller (UM 15.6)                                 */
/*                                                                           */
/* The SDRAM window is 0x6800_0000.  Registers are byte/half/word wide and    */
/* NOT adjacent -- the offsets below are read off the manual, not strided.    */
/*---------------------------------------------------------------------------*/
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
/* BSIZE[1:0] is at 5:4 and 32-bit is 01, NOT 10 -- the ordering is
 * 16 / 32 / 8, which reads like a typo in the manual and is not. */
#define RA8P1_SDCCR_BSIZE_16    (0U << 4)
#define RA8P1_SDCCR_BSIZE_32    (1U << 4)
#define RA8P1_SDCCR_BSIZE_8     (2U << 4)
/* MXC[1:0]: the column-address shift.  01 = 9 bits, which is what a 512
 * column part (A0-A8) needs. */
#define RA8P1_SDADR_MXC_9BIT    (1U << 0)
#define RA8P1_SDRFEN_RFEN       (1U << 0)      /* auto-refresh enable      */
/* Continuous access: hold the row open across consecutive accesses.  OFF at
 * reset, and writes are IGNORED once EXENB is set -- so it must be enabled in
 * the config window or every access pays a full ACT/CAS/PRE (~9 BCLK). */
#define RA8P1_SDAMOD_BE         (1U << 0)
#define RA8P1_SDICR_INIRQ       (1U << 0)      /* start the init sequencer */
#define RA8P1_SDSR_INIST        (1U << 3)      /* init sequence running    */
#define RA8P1_SDSR_MRSST        (1U << 2)
#define RA8P1_SDSR_SRFST        (1U << 4)

/* SDTR timing, all in SDCLK cycles.  Field encodings are value-minus-one for
 * CL/RP/RCD/RAI and a plain flag for WR (0 = 1 cycle, 1 = 2). */
#define RA8P1_SDTR_CL(c)        (((uint32_t)(c) & 0x7UL) << 0)    /*  2:0 */
#define RA8P1_SDTR_WR_2CYC      (1UL << 8)                        /*    8 */
#define RA8P1_SDTR_RP(c)        ((((uint32_t)(c) - 1UL) & 0x7UL) << 9)  /* 11:9 */
#define RA8P1_SDTR_RCD(c)       ((((uint32_t)(c) - 1UL) & 0x3UL) << 12) /* 13:12 */
#define RA8P1_SDTR_RAI(c)       ((((uint32_t)(c) - 1UL) & 0x7UL) << 16) /* 18:16 */

/* SDIR: initialisation sequencer timing. */
#define RA8P1_SDIR_ARFI(c)      (((uint16_t)(c) & 0xFU) << 0)     /*  3:0 */
#define RA8P1_SDIR_ARFC(c)      (((uint16_t)(c) & 0xFU) << 4)     /*  7:4 */
#define RA8P1_SDIR_PRC(c)       (((uint16_t)(c) & 0x7U) << 8)     /* 10:8 */

/** @brief SDCLK output enable, in the SYSC block rather than the bus one. */
#define RA8P1_SDCKOCR           (RA8P1_SYSC_BASE + 0x053UL)  /* 8-bit */
#define RA8P1_SDCKOCR_SDCKOEN   (1U << 0)

/** @brief PSEL value that selects the external-bus function on any pin. */
#define RA8P1_PFS_PSEL_BUS      0x0BUL

/* PmnPFS.DSCR[1:0] at 11:10.  00 low, 01 middle, 10 HIGH SPEED high drive,
 * 11 high drive -- note 10 and 11 are not ordered the way they read. */
#define RA8P1_PFS_DSCR_HS_HIGH  (2UL << 10)

/*---------------------------------------------------------------------------*/
/* DMA controller (UM 17)                                                    */
/*                                                                           */
/* Eight channels, 0x40 apart, plus one global activation register.  Unlike   */
/* the RSIP the DMAC is fully published, so it is driven directly rather      */
/* than through a vendor library.                                            */
/*---------------------------------------------------------------------------*/
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

/** @brief Global DMAC activation; without DMST no channel runs. */
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

/*
 * Slot allocation.  Any of the 96 slots can carry any event, so nothing stops
 * two drivers picking the same one and each silently stealing the other's
 * interrupt.  The map lives here, once, rather than as a private #define in
 * each driver -- it is a system-wide resource, and the wake-source reporter
 * has to read it too.
 */
#define RA8P1_ICU_SLOT_UART_RXI 0U
#define RA8P1_ICU_SLOT_UART_ERI 1U
#define RA8P1_ICU_SLOT_HTIMER   2U
#define RA8P1_ICU_SLOT_DMAC0    3U

/** @brief Event numbers this port links (UM Table 14.5). */
#define RA8P1_EVENT_SCI8_RXI    0x2FCUL
#define RA8P1_EVENT_SCI8_ERI    0x2FFUL
#define RA8P1_EVENT_GPT0_CCMPA  0x181UL
#define RA8P1_EVENT_DMAC0_INT   0x040UL

/*---------------------------------------------------------------------------*/
/* GPT32 (UM 23) -- the htimer's counter and compare                          */
/*                                                                            */
/* A 32-bit free-running counter on PCLKD with a compare that raises an event. */
/* Note GTINTAD carries NO per-compare interrupt enable (bits 7:0 reserved):   */
/* the compare always raises its event and the ICU decides whether it reaches  */
/* the NVIC, which is the event-link architecture doing the gating.            */
/*---------------------------------------------------------------------------*/
#define RA8P1_GPT_BASE(n)       (0x40322000UL + (0x100UL * (n)))
#define RA8P1_GPT_GTCR(n)       (RA8P1_GPT_BASE(n) + 0x2CUL)
#define RA8P1_GPT_GTST(n)       (RA8P1_GPT_BASE(n) + 0x3CUL)
#define RA8P1_GPT_GTCNT(n)      (RA8P1_GPT_BASE(n) + 0x48UL)
#define RA8P1_GPT_GTCCRA(n)     (RA8P1_GPT_BASE(n) + 0x4CUL)
#define RA8P1_GPT_GTPR(n)       (RA8P1_GPT_BASE(n) + 0x64UL)
#define RA8P1_GPT_GTCR_CST      (1UL << 0)     /* count start          */
/*
 * GTCLKCR must be written BEFORE the module-stop release and is locked once
 * MSTPE31 is 0 (UM 23.10.1).  BPEN=1 selects the synchronous PCLKD core
 * clock; the reset default is the ASYNC GPTCLK domain, and with GPTCLK at
 * the 8 MHz MOCO against a 120 MHz PCLKA bus the synchroniser drops every
 * register write -- the block reads as zeros and takes nothing, which cost
 * this port a full diagnostic pass to attribute.
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
/* Code MRAM programming (UM 60.4.2)                                          */
/*                                                                            */
/* No bootrom and no erase: an ordinary STR to an MRAM address enters a        */
/* 32-byte program buffer, which commits when it fills, when a write leaves    */
/* its 32-byte boundary, or on an explicit MRCFL flush.  Writes of 1..31 bytes */
/* are legal -- barrier, then flush.  Both control registers are key-gated and */
/* must be written 16 bits at a time or the write is dropped.                  */
/*---------------------------------------------------------------------------*/
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
 * Programming the SECURE alias needs MRCPSEN, and it outranks block
 * protection -- a store without it is not dropped, it BUS FAULTS.  Each
 * register carries its own key and must be written 16 bits at a time.
 * BPCN1 is ignored unless MRCPSEN is already 1, so the order is fixed.
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
#define RA8P1_NVIC_ICPR(i)      (0xE000E280UL + (4UL * (i)))
/*
 * PMSAv8 MPU (Armv8.1-M ARM B11).  Region attributes here are also what set
 * CACHEABILITY through MAIR, which is why the MPU and the caches are one
 * milestone: enabling the M85's caches without programmed regions inherits
 * the default memory map's attributes.
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
 * MAIR attribute bytes.  Index 0 is Normal write-back read/write-allocate --
 * the one that makes the D-cache actually cache SRAM; index 1 is Device
 * nGnRE for peripherals, which must never be cached or speculated into.
 */
#define RA8P1_MPU_MAIR_NORMAL_WB    0xFFU
#define RA8P1_MPU_MAIR_DEVICE       0x04U
/* Normal, non-cacheable inner and outer (0b0100_0100).  The durable MRAM
 * carve uses this: a D-cache line is 32 bytes, exactly the MRAM program
 * granule, so a cached store would sit in the line instead of reaching the
 * program buffer and an eviction would rewrite the whole granule at a time
 * the code never asked for. */
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

#endif /* TIKU_RA8P1_REGS_H_ */
