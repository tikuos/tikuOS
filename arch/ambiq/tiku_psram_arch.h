/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.h - Apollo510 MSPI0 and external octal-DDR PSRAM (64 MB).
 *
 * The board wires the die as x8 octal DDR, although the die is x16 capable.
 * The die's 1.2 V core rail is not on the SoC supply, so SoC power figures
 * cover only the controller domain, pads and PHY.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PSRAM_ARCH_H_
#define TIKU_PSRAM_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TABLE 1 -- BRING-UP SEQUENCE                                              */
/*---------------------------------------------------------------------------*/
/*
 * The vendor's bring-up steps in the vendor's order, with the register this
 * port writes for each.  Out of this order the controller can complete
 * commands and still read garbage.
 *
 *  #  what                        register(s) here              why
 * --  --------------------------  ----------------------------  --------------
 *  1  power the controller        PWRCTRL.DEVPWREN.PWRENMSPI0   domain is off
 *                                 wait DEVPWRSTATUS             at boot
 *  2  select + enable IO clock    CLKGEN.MSPIIOCLKCTRL          the PHY clock
 *                                 (MSPI0IOCLKSEL, MSPI0IOCLKEN) is separate
 *                                                               from the core
 *  3  SDR250 tap                  DEV0CFG1.SDR250EN0            bypasses the
 *                                                               /2 (see below)
 *  4  command format + clock div  DEV0CFG (ASIZE/ISIZE/CLKDIV/  4-byte addr,
 *                                  TURNAROUND/CPOL/CPHA/        2-byte instr,
 *                                  TXNEG/RXNEG/RXCAP/           latency counts
 *                                  WRITELATENCY)
 *  5  bus width                   DEV0CFG.DEVCFG0 = OCTAL0,     x8, shared IO
 *                                 DEV0CFG.SEPIO0 = 0
 *  6  DDR emulation               DEV0DDR.EMULATEDDR0 = 1       double-rate
 *  7  pad output enables          PADOUTEN = 1023 (8 data +     controller
 *                                  clock + DQS)                 owns the pads
 *  8  read/write opcodes          DEV0INSTR (READINSTR0/        0x2020/0xA0A0
 *                                  WRITEINSTR0)
 *  9  XIP mixed mode              DEV0XIP.XIPMIXED0 = NORMAL    octal DDR
 * 10  XIP framing                 DEV0XIP (XIPACK/XIPSENDA/     used by the
 *                                  XIPSENDI/XIPENTURN/          XIP aperture
 *                                  XIPTURNAROUND/XIPENWLAT/
 *                                  XIPWRITELATENCY)
 * 11  DMA boundary + time limit   DEV0BOUNDARY (DMABOUND0=      1 KB row
 *                                  BREAK1K, DMATIMELIMIT0=40)   boundary
 * 12  DQS receive                 DEV0DDR (ENABLEDQS0=1,        the RX strobe;
 *                                  RXDQSDELAY0=16, TXDQSDELAY0  the timing
 *                                  =0, ...), DEV0DDRDLYEXT      scan moves it
 * 13  RX sampling                 DEV0CFG1 (DQSTURN0=2,         vendor values
 *                                  RXSMP0=1, TAFOURTH0=1,
 *                                  SFTURN0=10, rest 0)
 * 14  FIFO thresholds             THRESHOLD.RXTHRESH=30,        vendor values
 *                                 DMABCOUNT=32, DMATHRESH
 * 15  unlink IOM                  MSPICFG.IOMSEL = DISABLED     no IOM is
 *                                                               bridged here
 * 16  configure the pads          GPIO.PINCFG[64..73, CE]       after the
 *                                                               controller
 * 17  settle                      150 us delay                  vendor
 * 18  device global reset         cmd 0xFFFF, then 2 us         known state
 * 19  read MR1 / MR2 / MR3        cmd 0x4040 at addr 1 / 2      the identity
 *                                                               gate
 * 20  program MR0/MR4 latencies   cmd 0xC0C0 at addr 0 / 4      match the
 *                                                               target clock
 * 21  raise the clock             back to step 2/4 at target    after the
 *                                                               latencies
 *
 * tiku_psram_init() runs steps 1-18, tiku_psram_read_id() step 19 and
 * tiku_psram_set_speed() steps 20-21.
 *
 * The clock model, derived from the HAL's frequency tables:
 *
 *   IO clock output = source / (2 * CLKDIVn),  unless SDR250EN0 = 1,
 *                                             which bypasses the /2.
 *
 * It gives every entry of the vendor's frequency table: 48 MHz = HFRC_192 /
 * (2*2); 96 MHz = HFRC_192 / (2*1); 192 MHz = HFRC_192 with SDR250EN0;
 * 125 MHz = HFRC2_250 / (2*1); 250 MHz = HFRC2_250 with SDR250EN0.  Bring-up
 * runs at 48 MHz, the vendor's MSPI_BASE_FREQUENCY for register access.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 2 -- DEVICE COMMANDS AND MODE REGISTERS                             */
/*---------------------------------------------------------------------------*/
/*
 * Octal-DDR opcodes are 2 bytes, the opcode byte sent on both edges of one
 * clock: READ is 0x2020.  All take a 4-byte address; register access puts the
 * MR number in the address.
 *
 *   opcode  name              addr        data      notes
 *   ------  ----------------  ----------  --------  -------------------------
 *   0xFFFF  GLOBAL_RESET      0           2 B dummy  wait 2 us after
 *   0x2020  READ (linear)     byte addr   n          turnaround + read latency
 *   0xA0A0  WRITE (linear)    byte addr   n          write latency
 *   0x4040  READ_REGISTER     MR number   4 B        turnaround + WR latency
 *   0xC0C0  WRITE_REGISTER    MR number   4 B        no turnaround
 *
 * A register read returns the MR at the requested address in byte 0 and the
 * next MR in byte 1, so address 2 yields MR2 and MR3 in one transfer.
 *
 * Mode registers used here (volatile unless marked NV):
 *   MR0  [1:0] DS drive strength (0=full/25R, 1=half/50R default)
 *        [4:2] RLC read latency code (2=LC5/109MHz default on 25616BA,
 *                                     3=LC6/133MHz default on 51216BA)
 *        [5]   LT latency type (0=variable default, 1=fixed)
 *   MR1  [4:0] VID vendor id: 0x0D (5'b01101) for AP Memory              NV
 *        [7]   ULP half-sleep supported                                  NV
 *   MR2  [2:0] DENSITY: 1=32Mb 3=64Mb 5=128Mb 7=256Mb  6=512Mb (U14)     NV
 *        [4:3] GENERATION (0 reads as gen 5)                             NV
 *        [7:5] GB good/bad die: 3'b110 = pass                            NV
 *   MR3  [7]   RBXen row-boundary-crossing supported                     NV
 *   MR4  [7:5] WLC write latency code (6=LC6/109MHz; resets to 2=LC5)
 *        [4:3] RFS refresh frequency
 *   MR6  [7:0] ULPM: 0xF0 = half sleep, 0xC0 = deep power down
 *   MR8  [1:0] BL burst length, [3] RBX, [6] IOM (0=octal, 1=hex)
 *
 * The identity gate in tiku_psram_read_id():
 *   MR1.VID    == 0x0D          (AP Memory)
 *   MR2.DENSITY == 0x6          (512 Mbit = 64 MB, the U14 part)
 *   MR2.GB     == 0x6           (die passed test)
 * Any other value fails, however plausible: a mis-timed bus returns garbage
 * that can look valid.
 */

/*---------------------------------------------------------------------------*/
/* PUBLIC CONSTANTS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief MSPI0 XIP aperture base, where the 64 MB device is mapped. */
#define TIKU_PSRAM_XIP_BASE   0x60000000UL
/** @brief Aperture span (MSPI apertures are 64 MB apart; the die fills one). */
#define TIKU_PSRAM_XIP_SPAN   0x04000000UL
/** @brief Expected device size in bytes, from MR2.DENSITY = 512 Mbit. */
#define TIKU_PSRAM_SIZE_BYTES 0x04000000UL

/** @brief IO clock rows for tiku_psram_init() and tiku_psram_set_speed(). */
#define TIKU_PSRAM_CLK_48MHZ   0u   /**< bring-up and register access      */
#define TIKU_PSRAM_CLK_96MHZ   1u
#define TIKU_PSRAM_CLK_125MHZ  2u
#define TIKU_PSRAM_CLK_192MHZ  3u
#define TIKU_PSRAM_CLK_250MHZ  4u   /**< above the die's 200 MHz rating    */

/** @brief Result codes of the PSRAM driver. */
typedef enum {
    TIKU_PSRAM_OK = 0,
    TIKU_PSRAM_ERR_POWER,     /**< domain never came up, or driver not up   */
    TIKU_PSRAM_ERR_CLOCK,     /**< IO clock select/enable did not stick     */
    TIKU_PSRAM_ERR_TIMEOUT,   /**< a transfer did not complete or reported
                                   an error; no passing tap in a scan       */
    TIKU_PSRAM_ERR_ID,        /**< device answered, with the wrong identity */
    TIKU_PSRAM_ERR_ARG,       /**< bad argument, or refused in this state:
                                   XIP on, DMA armed, live allocations      */
} tiku_psram_err_t;

/** @brief Identity read out of the device's mode registers. */
typedef struct {
    uint8_t mr0;          /**< drive strength + read latency               */
    uint8_t mr1;          /**< vendor id + half-sleep capability           */
    uint8_t mr2;          /**< density + generation + good/bad             */
    uint8_t mr3;          /**< row-boundary-crossing capability            */
    uint8_t mr4;          /**< write latency + refresh                     */
    uint8_t mr8;          /**< burst length + IO mode                      */
    uint8_t vendor_id;    /**< mr1[4:0] -- 0x0D expected                   */
    uint8_t density_code; /**< mr2[2:0] -- 0x6 expected (512 Mbit)         */
    uint8_t generation;   /**< mr2[4:3] as read (0 means generation 5)     */
    uint8_t good_die;     /**< 1 if mr2[7:5] == 0x6                        */
    uint32_t size_bytes;  /**< decoded from density_code, 0 if unknown     */
} tiku_psram_id_t;

/*---------------------------------------------------------------------------*/
/* BRING-UP AND DIAGNOSTICS                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Power MSPI0, configure it for octal DDR, reset the device.
 *
 * Performs table-1 steps 1-18 at @p clk.  Every wait is spin-bounded.  An
 * IO clock that does not start powers the domain off again; a failed power-up
 * or device reset leaves the domain enabled and the driver down.
 *
 * @param clk  TIKU_PSRAM_CLK_* -- use 48 MHz for bring-up
 * @return TIKU_PSRAM_OK, or the error of the step that failed
 */
tiku_psram_err_t tiku_psram_init(unsigned clk);

/** @brief Stop the IO clock and power the domain off; pads stay as set. */
void tiku_psram_deinit(void);

/** @brief 1 if the MSPI0 controller domain is powered. */
int tiku_psram_powered(void);

/**
 * @brief Read the device's mode registers and check identity.
 *
 * Table-1 step 19 plus the identity gate.  @p out is filled on every return
 * but ERR_POWER, a wrong identity included; fields after a failed read are 0.
 *
 * @param out  filled with mode registers and decoded fields (may be NULL)
 * @return TIKU_PSRAM_OK if identity matches U14; ERR_ID if it answered with
 *         something else; ERR_TIMEOUT if it did not answer at all;
 *         ERR_POWER if the driver is not up
 */
tiku_psram_err_t tiku_psram_read_id(tiku_psram_id_t *out);

/** @brief Raw mode-register read (address = MR number), for diagnostics. */
tiku_psram_err_t tiku_psram_reg_read(uint32_t mr, uint32_t *out);

/** @brief Raw mode-register write (address = MR number). */
tiku_psram_err_t tiku_psram_reg_write(uint32_t mr, uint32_t val);

/** @brief Issue the device global reset (0xFFFF) and settle. */
tiku_psram_err_t tiku_psram_device_reset(void);

/** @brief Nominal IO clock in Hz for the configured setting, 0 if not up. */
unsigned long tiku_psram_clock_hz(void);

/** @brief Register snapshot for diagnosing a transfer that does not finish. */
typedef struct {
    uint32_t devpwrstatus;   /**< is the MSPI0 domain up                     */
    uint32_t clkgen_misc;    /**< FRCHFRC / FRCHFRC2 -- oscillator forced on */
    uint32_t mspiioclkctrl;  /**< IO clock select + enable                   */
    uint32_t dev0cfg;        /**< width, divider, latencies, address/instr   */
    uint32_t dev0cfg1;       /**< SDR250, RX sampling                        */
    uint32_t dev0ddr;        /**< DDR emulation + DQS delays                 */
    uint32_t dev0xip;        /**< aperture framing                           */
    uint32_t dev0instr;      /**< read/write opcodes                         */
    uint32_t padouten;       /**< which pads the controller drives           */
    uint32_t mspicfg;        /**< IOM link, APB clock                        */
    uint32_t ctrl;           /**< last command: START/STATUS/BUSY            */
    uint32_t intstat;        /**< error bits from the last command           */
    uint32_t rxentries;      /**< FIFO occupancy, RX                         */
    uint32_t txentries;      /**< FIFO occupancy, TX                         */
    /* Captured inside PIO transfers: whether the first TX word is consumed
     * separates a controller with no clock from one waiting on the device. */
    uint32_t dbg_ctrl_after_start; /**< CTRL right after START             */
    uint32_t dbg_tx_after_write;   /**< TXENTRIES after the first TX word  */
    uint32_t dbg_tx_settled;       /**< TXENTRIES 20 us after the last one */
    uint32_t dbg_ctrl_settled;     /**< CTRL at that point or at a timeout */
    uint32_t dbg_intstat;          /**< INTSTAT at completion or timeout   */
} tiku_psram_regs_t;

/**
 * @brief Snapshot the registers that decide whether a transfer can complete.
 *
 * Reads the live registers back, with the values captured during PIO
 * transfers.
 */
void tiku_psram_regs(tiku_psram_regs_t *out);

/**
 * @brief Override the read TURNAROUND count before init (0 = use the default).
 *
 * Sets the read window's position from the next controller configuration on.
 * A wrong value shifts the bytes read back, or, too small, leaves the read
 * waiting until it returns TIKU_PSRAM_ERR_TIMEOUT.  Masked to 6 bits.
 */
void tiku_psram_set_turnaround(unsigned ta);

/** @brief Override the RX capture knobs before init (RXNEG, RXCAP, RXSMP). */
void tiku_psram_set_rx(unsigned rxneg, unsigned rxcap, unsigned rxsmp);

/** @brief Enable or disable the DQS strobe before init (default on). */
void tiku_psram_set_dqs(int enable);

/**
 * @brief Issue a global reset with no data phase; report CTRL afterwards.
 *
 * If it completes, the bus clocks and a failing transfer fails in its data
 * phase; if it times out, the controller is not driving the bus.
 */
tiku_psram_err_t tiku_psram_cmd_probe(uint32_t *ctrl_out);

/**
 * @brief Install a tracer that tiku_psram_init() calls before each step;
 *        NULL removes it.
 *
 * A mis-clocked bus can stall the CPU with no fault and no output; the last
 * step traced names where it stopped.
 */
void tiku_psram_set_trace(void (*fn)(const char *step));

/*---------------------------------------------------------------------------*/
/* MEMORY ACCESS, SPEED, TIMING SCAN, XIP AND DMA                            */
/*---------------------------------------------------------------------------*/

/** @brief PIO read of device memory (chunked; any n).  XIP must be off. */
tiku_psram_err_t tiku_psram_mem_read(uint32_t addr, void *buf, uint32_t n);
/** @brief PIO write of device memory (chunked; any n).  XIP must be off. */
tiku_psram_err_t tiku_psram_mem_write(uint32_t addr, const void *buf,
                                      uint32_t n);

/**
 * @brief Move to clock row @p clk: program device MR0/MR4 latencies to match,
 *        then reconfigure the controller -- no device reset in between.
 *
 * Runs tiku_psram_init() at 48 MHz first if the driver is not up.
 */
tiku_psram_err_t tiku_psram_set_speed(unsigned clk);

/**
 * @brief RXDQSDELAY scan at the live clock: pattern-verify all 32 taps.
 *
 * Address-derived patterns, compared bit-exact per tap in two regions, one
 * past 32 MB.  Applies the centre of the widest passing window; with no
 * passing tap RXDQSDELAY is left at tap 31.
 *
 * @note Overwrites 2 KB at device offsets 0x1000 and 0x2001000.
 * @param pass_mask  bit N set = tap N passed (may be NULL)
 * @param center     applied tap, 0 if none passed (may be NULL)
 * @return width of the widest passing window (0 = nothing passed)
 */
uint32_t tiku_psram_timing_scan(uint32_t *pass_mask, unsigned *center);

/**
 * @brief Map/unmap the 64 MB aperture at 0x60000000.  PIO refuses while on.
 *
 * Unmapping cleans and invalidates the whole D-cache first.
 */
tiku_psram_err_t tiku_psram_xip_enable(int enable);
/** @brief 1 if the aperture is live. */
int tiku_psram_xip_enabled(void);

/**
 * @brief Arm a transfer and return; collect it with tiku_psram_dma_wait().
 *
 * Takes the arguments of tiku_psram_dma() and also refuses (ERR_ARG) while
 * a transfer is armed.
 */
tiku_psram_err_t tiku_psram_dma_start(uint32_t dev_addr, void *sram,
                                      uint32_t n, int to_device);

/** @brief Wait for the transfer armed by tiku_psram_dma_start(); returns
 *         TIKU_PSRAM_OK at once if none is armed. */
tiku_psram_err_t tiku_psram_dma_wait(void);

/**
 * @brief Blocking DMA between SRAM and the device (word-aligned, n%4==0).
 *        Cache coherency is the caller's job.  XIP must be off.
 */
tiku_psram_err_t tiku_psram_dma(uint32_t dev_addr, void *sram, uint32_t n,
                                int to_device);

/**
 * @brief Hardware-chained DMA via the command queue.
 *
 * Splits @p total into @p seg_bytes segments, builds the descriptor list of
 * table 3 (tiku_psram_arch.c), and lets the CQ engine run every segment with
 * no CPU between them.  Cache coherency of @p sram is the caller's job.
 *
 * @note XIP must be off.  @p total must be a multiple of @p seg_bytes, which
 *       must be a multiple of 4, and total/seg_bytes at most 66 segments.
 */
tiku_psram_err_t tiku_psram_cq_xfer(uint32_t dev_addr, void *sram,
                                    uint32_t total, uint32_t seg_bytes,
                                    int to_device);

/*---------------------------------------------------------------------------*/
/* LIFECYCLE AND THE TIKU_MEM_PSRAM TIER                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Full bring-up: speed, identity, optional timing scan, XIP map,
 *        and attach the aperture as the TIKU_MEM_PSRAM tier.
 *
 * A failed tier attach is ignored; on a re-up the tier is already attached.
 *
 * @param clk   TIKU_PSRAM_CLK_* row
 * @param scan  non-zero: run the RXDQSDELAY scan first
 * @return TIKU_PSRAM_OK, the failing step's error, or ERR_TIMEOUT if a scan
 *         finds no passing tap
 */
tiku_psram_err_t tiku_psram_up(unsigned clk, int scan);

/**
 * @brief Orderly power-down: detach the tier, unmap, release the domain.
 *        Contents are lost.  Refused (ERR_ARG) if tier allocations are
 *        outstanding and @p force is 0.
 */
tiku_psram_err_t tiku_psram_down(int force);

/**
 * @brief Half sleep: contents retained on self-refresh at uA-class device
 *        current; XIP is unmapped and every access path refuses until wake.
 */
tiku_psram_err_t tiku_psram_halfsleep(void);

/** @brief Wake from half sleep; succeeds only if the identity reads back.
 *         XIP stays unmapped. */
tiku_psram_err_t tiku_psram_wake(void);

/** @brief 1 while in half sleep. */
int tiku_psram_asleep(void);

/** @brief RXDQSDELAY tap set by the last scan that passed (0xFF if none). */
unsigned tiku_psram_tap(void);

/**
 * @brief Controller-free identity read: bit-bang the octal waveform on GPIO.
 *
 * Fills @p edges with the D0-7 byte sampled after each of @p n_edges clock
 * edges following the READ_REGISTER(MR1) command.
 *
 * @note Leaves the pads as GPIO; the next tiku_psram_init() reclaims them.
 */
void tiku_psram_bitbang_id(uint8_t *edges, uint32_t n_edges);

/** @brief Same, for an arbitrary mode register. */
void tiku_psram_bitbang_reg(uint32_t mr, uint8_t *edges, uint32_t n_edges);
/** @brief Same, for an array read from device address @p addr. */
void tiku_psram_bitbang_mem(uint32_t addr, uint8_t *edges, uint32_t n_edges);
/** @brief The bit-banged command under the three functions above. */
void tiku_psram_bitbang_cmd(uint32_t opcode, uint32_t addr,
                            uint8_t *edges, uint32_t n_edges);

/**
 * @brief Break or restore the bus, to exercise the error paths.
 *
 * Non-zero @p enable sets D0's pad function to plain GPIO, so the next
 * transfer fails; 0 restores the MSPI pad configuration.
 */
void tiku_psram_fault_inject(int enable);

#endif /* TIKU_PSRAM_ARCH_H_ */
