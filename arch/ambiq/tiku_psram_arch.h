/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_psram_arch.h - Apollo510 MSPI0 and external octal-DDR PSRAM (64 MB).
 *
 * The board wires the die as x8 octal DDR even though it is x16 capable, so the
 * board decides the width.  Its 1.2 V core rail is outside the measurement loop,
 * so only the controller domain, pads and PHY show on the SoC rail.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PSRAM_ARCH_H_
#define TIKU_PSRAM_ARCH_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* TABLE 1 -- BRING-UP SEQUENCE (transcribed, in order, with reasons)        */
/*---------------------------------------------------------------------------*/
/*
 * Every step the vendor performs, in the order it performs it, with the
 * register this port writes and why the step exists.  Deviating from this
 * order is how a bus comes up looking alive and reading garbage.
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
 *                                  XIPSENDI/XIPENTURN/          aperture in M3
 *                                  XIPTURNAROUND/XIPENWLAT/
 *                                  XIPWRITELATENCY)
 * 11  DMA boundary + time limit   DEV0BOUNDARY (DMABOUND0=      1 KB row
 *                                  BREAK1K, DMATIMELIMIT0=40)   boundary
 * 12  DQS receive                 DEV0DDR (ENABLEDQS0=1,        the RX strobe;
 *                                  RXDQSDELAY0=16, TXDQSDELAY0  M2 scans the
 *                                  =0, ...), DEV0DDRDLYEXT      delay
 * 13  RX sampling                 DEV0CFG1 (DQSTURN0=2,         vendor values,
 *                                  RXSMP0=1, TAFOURTH0=1,       do not guess
 *                                  SFTURN0=10, rest 0)
 * 14  FIFO thresholds             THRESHOLD.RXTHRESH=30,        speed-class
 *                                 DMATHRESH (only with a TCB)   dependent
 * 15  unlink IOM                  MSPICFG.IOMSEL = NONE         no IOM is
 *                                                               bridged here
 * 16  configure the pads          GPIO.PINCFG[64..73, 199]      AFTER the
 *                                                               controller
 * 17  settle                      150 us delay                  vendor
 * 18  device global reset         cmd 0xFFFF, then 2 us         known state
 * 19  read MR1 / MR2 / MR3        cmd 0x4040 at addr 1 / 2      IDENTITY --
 *                                                               the M1 gate
 * 20  program MR0/MR4 latencies   cmd 0xC0C0 at addr 0 / 4      match the
 *                                                               target clock
 * 21  raise the clock             back to step 2/4 at target    only after the
 *                                                               device agrees
 *
 * Steps 1-19 are M1 (this file's first milestone).  20-21 are M2.
 *
 * The clock model, derived from the HAL's own tables and to be confirmed by
 * measurement in M3 (`psrambench`), not asserted here:
 *
 *   IO clock output = source / (2 * CLKDIVn),  unless SDR250EN0 = 1,
 *                                             which bypasses the /2.
 *
 * That single rule reproduces every entry in the vendor's frequency table:
 * 48 MHz = HFRC_192 / (2*2); 96 MHz = HFRC_192 / (2*1); 192 MHz = HFRC_192
 * with SDR250EN0; 125 MHz = HFRC2_250 / (2*1); 250 MHz = HFRC2_250 with
 * SDR250EN0.  M1 uses 48 MHz -- the vendor's own MSPI_BASE_FREQUENCY for
 * register access -- because a bus that cannot talk slowly cannot be
 * debugged when it fails fast.
 */

/*---------------------------------------------------------------------------*/
/* TABLE 2 -- DEVICE COMMANDS AND MODE REGISTERS (transcribed)              */
/*---------------------------------------------------------------------------*/
/*
 * Octal-DDR opcodes are 2 bytes with the byte duplicated (the DDR bus sends
 * one nibble-pair per edge), which is why they look like 0x2020 rather than
 * 0x20.  All take a 4-byte address; register access puts the MR number in
 * the address.
 *
 *   opcode  name              addr        data      notes
 *   ------  ----------------  ----------  --------  -------------------------
 *   0xFFFF  GLOBAL_RESET      0           2 B dummy  wait 2 us after
 *   0x2020  READ (linear)     byte addr   n          turnaround + read latency
 *   0xA0A0  WRITE (linear)    byte addr   n          write latency
 *   0x4040  READ_REGISTER     MR number   4 B        turnaround + WR latency
 *   0xC0C0  WRITE_REGISTER    MR number   4 B        no turnaround
 *
 * A register READ returns the MR at the requested address in byte 0 and the
 * NEXT MR in byte 1 -- so address 2 yields MR2 and MR3 in one transfer.
 *
 * MODE REGISTERS used here (volatile unless marked):
 *   MR0  [1:0] DS drive strength (0=full/25R, 1=half/50R default)
 *        [4:2] RLC read latency code (2=LC5/109MHz default on 25616BA,
 *                                     3=LC6/133MHz default on 51216BA)
 *        [5]   LT latency type (0=variable default, 1=fixed)
 *   MR1  [4:0] VID vendor id -- MUST read 0x0D (5'b01101) for AP Memory  NV
 *        [7]   ULP half-sleep supported                                  NV
 *   MR2  [2:0] DENSITY: 1=32Mb 3=64Mb 5=128Mb 7=256Mb  6=512Mb  <-- ours  NV
 *        [4:3] GENERATION (0 reads as gen 5)                             NV
 *        [7:5] GB good/bad die: 3'b110 = pass                            NV
 *   MR3  [7]   RBXen row-boundary-crossing supported                     NV
 *   MR4  [7:5] WLC write latency code (6=LC6/109MHz default)
 *        [4:3] RFS refresh frequency
 *   MR6  [7:0] ULPM: 0xF0 = half sleep, 0xC0 = deep power down
 *   MR8  [1:0] BL burst length, [3] RBX, [6] IOM (0=octal, 1=hex)
 *
 * The M1 identity gate, stated as numbers so it cannot be fudged:
 *   MR1.VID    == 0x0D          (AP Memory)
 *   MR2.DENSITY == 0x6          (512 Mbit = 64 MB -- matches U14)
 *   MR2.GB     == 0x6           (die passed test)
 * Anything else is a failure, including a plausible-looking value: the whole
 * point of reading identity first is that a mis-timed bus returns garbage
 * that is easy to rationalise.
 */

/*---------------------------------------------------------------------------*/
/* PUBLIC CONSTANTS                                                          */
/*---------------------------------------------------------------------------*/

/** @brief MSPI0 XIP aperture base -- where the 64 MB will appear in M3. */
#define TIKU_PSRAM_XIP_BASE   0x60000000UL
/** @brief Aperture span (MSPI apertures are 64 MB apart; the die fills one). */
#define TIKU_PSRAM_XIP_SPAN   0x04000000UL
/** @brief Expected device size in bytes, from MR2.DENSITY = 512 Mbit. */
#define TIKU_PSRAM_SIZE_BYTES 0x04000000UL

/** @brief Bring-up clocks.  M1 runs at the low one on purpose. */
#define TIKU_PSRAM_CLK_48MHZ   0u
#define TIKU_PSRAM_CLK_96MHZ   1u
#define TIKU_PSRAM_CLK_125MHZ  2u
#define TIKU_PSRAM_CLK_192MHZ  3u
#define TIKU_PSRAM_CLK_250MHZ  4u

/** @brief Result codes.  Distinct causes stay distinct -- a timeout and a
 *  wrong ID are different bugs and must not share a return value. */
typedef enum {
    TIKU_PSRAM_OK = 0,
    TIKU_PSRAM_ERR_POWER,     /**< controller domain never came up          */
    TIKU_PSRAM_ERR_CLOCK,     /**< IO clock select/enable did not stick     */
    TIKU_PSRAM_ERR_TIMEOUT,   /**< a PIO transfer never completed           */
    TIKU_PSRAM_ERR_ID,        /**< device answered, with the wrong identity */
    TIKU_PSRAM_ERR_ARG,       /**< bad argument from the caller             */
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
    uint8_t generation;   /**< mr2[4:3] decoded (0 means generation 5)     */
    uint8_t good_die;     /**< 1 if mr2[7:5] == 0x6                        */
    uint32_t size_bytes;  /**< decoded from density_code, 0 if unknown     */
} tiku_psram_id_t;

/*---------------------------------------------------------------------------*/
/* API -- M1 surface only.  XIP/DMA/tier land in M3/M4.                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Power MSPI0, configure it for octal DDR, reset the device.
 *
 * Performs table-1 steps 1-18 at @p clk.  Fails closed: every wait is
 * spin-bounded, and a failure leaves the controller domain powered off
 * rather than half-configured.
 *
 * @param clk  TIKU_PSRAM_CLK_* -- use 48 MHz for bring-up
 * @return TIKU_PSRAM_OK, or a distinct error
 */
tiku_psram_err_t tiku_psram_init(unsigned clk);

/** @brief Power the controller domain back off (pads left as configured). */
void tiku_psram_deinit(void);

/** @brief 1 if the MSPI0 controller domain is powered. */
int tiku_psram_powered(void);

/**
 * @brief Read the device's mode registers and check identity.
 *
 * Table-1 step 19 plus the identity gate.  @p out is filled whenever the
 * transfers succeeded, EVEN IF the identity is wrong -- the caller needs the raw
 * bytes to diagnose a mis-timed bus, so the verdict is returned separately.
 *
 * @param out  filled with mode registers and decoded fields (may be NULL)
 * @return TIKU_PSRAM_OK if identity matches U14; ERR_ID if it answered with
 *         something else; ERR_TIMEOUT if it did not answer at all
 */
tiku_psram_err_t tiku_psram_read_id(tiku_psram_id_t *out);

/** @brief Raw mode-register read (address = MR number), for diagnostics. */
tiku_psram_err_t tiku_psram_reg_read(uint32_t mr, uint32_t *out);

/** @brief Raw mode-register write, for M2's latency programming. */
tiku_psram_err_t tiku_psram_reg_write(uint32_t mr, uint32_t val);

/** @brief Issue the device global reset (0xFFFF) and settle. */
tiku_psram_err_t tiku_psram_device_reset(void);

/** @brief Nominal IO clock in Hz for the configured setting, 0 if not up. */
unsigned long tiku_psram_clock_hz(void);

/** @brief Snapshot of everything that decides whether a transfer can work. */
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
    /* Captured DURING the last transfer -- see the .c for why before-state
     * alone cannot distinguish "no clock" from "waiting on the device". */
    uint32_t dbg_ctrl_after_start;
    uint32_t dbg_tx_after_write;
    uint32_t dbg_tx_settled;
    uint32_t dbg_ctrl_settled;
    uint32_t dbg_intstat;
} tiku_psram_regs_t;

/**
 * @brief Snapshot the registers that decide whether a transfer can complete.
 *
 * For diagnosis: a bring-up that fails with everything "configured" needs the
 * configuration READ BACK, not re-read from the source that wrote it.
 */
void tiku_psram_regs(tiku_psram_regs_t *out);

/**
 * @brief Override the read TURNAROUND count before init (0 = use the default).
 *
 * The read window's position.  Exposed because the correct value has to be
 * FOUND on hardware -- the vendor's arithmetic gives a starting point, not an
 * answer, and a wrong window returns shifted bytes rather than an error.
 */
void tiku_psram_set_turnaround(unsigned ta);

/** @brief Override the RX capture knobs before init (RXNEG, RXCAP, RXSMP). */
void tiku_psram_set_rx(unsigned rxneg, unsigned rxcap, unsigned rxsmp);

/** @brief Enable/disable the DQS strobe before init (diagnostic; default on). */
void tiku_psram_set_dqs(int enable);

/**
 * @brief Issue a command with NO data phase; report CTRL afterwards.
 *
 * The most primitive question a bring-up can ask: can this controller
 * complete ANY transaction?  A data-phase failure and a dead bus look
 * identical from outside, and this separates them.
 */
tiku_psram_err_t tiku_psram_cmd_probe(uint32_t *ctrl_out);

/**
 * @brief Install a step tracer, so a bring-up hang names its own step.
 *
 * Bring-up on a mis-clocked bus can stall the CPU with no fault and no
 * output.  With a tracer installed each risky step announces itself first,
 * so the last line printed identifies where it died.  Pass NULL to remove.
 */
void tiku_psram_set_trace(void (*fn)(const char *step));

/*---------------------------------------------------------------------------*/
/* M2/M3 -- memory access, speed, timing scan, XIP, DMA                      */
/*---------------------------------------------------------------------------*/

/** @brief PIO read of device memory (chunked; any n).  XIP must be off. */
tiku_psram_err_t tiku_psram_mem_read(uint32_t addr, void *buf, uint32_t n);
/** @brief PIO write of device memory (chunked; any n).  XIP must be off. */
tiku_psram_err_t tiku_psram_mem_write(uint32_t addr, const void *buf,
                                      uint32_t n);

/**
 * @brief Move to clock row @p clk: program device MR0/MR4 latencies to match,
 *        then reconfigure the controller -- no device reset in between.
 */
tiku_psram_err_t tiku_psram_set_speed(unsigned clk);

/**
 * @brief RXDQSDELAY scan at the live clock: pattern-verify all 32 taps.
 *
 * Two regions (one past 32 MB, proving high address bits), address-derived
 * patterns, bit-exact compare per tap.  Ships the centre of the widest
 * passing window and leaves it live.
 *
 * @param pass_mask  bit N set = tap N passed (may be NULL)
 * @param center     shipped tap (may be NULL)
 * @return width of the widest passing window (0 = nothing passed)
 */
uint32_t tiku_psram_timing_scan(uint32_t *pass_mask, unsigned *center);

/** @brief Map/unmap the 64 MB aperture at 0x60000000.  PIO refuses while on. */
tiku_psram_err_t tiku_psram_xip_enable(int enable);
/** @brief 1 if the aperture is live. */
int tiku_psram_xip_enabled(void);

/**
 * @brief Blocking DMA between SRAM and the device (word-aligned, n%4==0).
 *        Cache coherency is the caller's job.  XIP must be off.
 */
/** @brief Arm a transfer and return; collect it with tiku_psram_dma_wait(). */
tiku_psram_err_t tiku_psram_dma_start(uint32_t dev_addr, void *sram,
                                      uint32_t n, int to_device);

/** @brief Wait for the transfer armed by tiku_psram_dma_start(). */
tiku_psram_err_t tiku_psram_dma_wait(void);

tiku_psram_err_t tiku_psram_dma(uint32_t dev_addr, void *sram, uint32_t n,
                                int to_device);

/**
 * @brief Hardware-chained DMA via the command queue (M3.5).
 *
 * Splits @p total into @p seg_bytes segments, builds the vendor-shaped
 * descriptor list, and lets the CQ engine run every segment with no CPU in the
 * seams.  Cache coherency of @p sram is the caller's job; XIP must be off.
 *
 * @note total/seg_bytes must be <= 66 segments per call.
 */
tiku_psram_err_t tiku_psram_cq_xfer(uint32_t dev_addr, void *sram,
                                    uint32_t total, uint32_t seg_bytes,
                                    int to_device);

/*---------------------------------------------------------------------------*/
/* M4 -- lifecycle + the TIKU_MEM_PSRAM tier                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Full bring-up: speed, identity, optional timing scan, XIP map,
 *        and attach the aperture as the TIKU_MEM_PSRAM tier.
 * @param clk   TIKU_PSRAM_CLK_* row
 * @param scan  non-zero: run the RXDQSDELAY scan and refuse to ship if no
 *              tap passes
 */
tiku_psram_err_t tiku_psram_up(unsigned clk, int scan);

/**
 * @brief Orderly power-down: detach the tier, unmap, release the domain.
 *        Contents are LOST.  Refused (ERR_ARG) if tier allocations are
 *        outstanding and @p force is 0.
 */
tiku_psram_err_t tiku_psram_down(int force);

/**
 * @brief Half sleep: contents RETAINED on self-refresh at uA-class device
 *        current; XIP is unmapped and every access path refuses until wake.
 */
tiku_psram_err_t tiku_psram_halfsleep(void);

/** @brief Wake from half sleep; trusted only after identity answers again. */
tiku_psram_err_t tiku_psram_wake(void);

/** @brief 1 while in half sleep. */
int tiku_psram_asleep(void);

/** @brief Shipped RXDQSDELAY tap (0xFF until a scan has run). */
unsigned tiku_psram_tap(void);

/**
 * @brief Controller-free identity read: bit-bang the octal waveform on GPIO.
 *
 * Fills @p edges with the D0-7 byte sampled after each of @p n_edges clock
 * edges following the READ_REGISTER(MR1) command.  The ground-truth probe:
 * no MSPI controller, no timing configuration, no latency assumption.
 */
void tiku_psram_bitbang_id(uint8_t *edges, uint32_t n_edges);

/** @brief Same, for an arbitrary mode register. */
void tiku_psram_bitbang_reg(uint32_t mr, uint8_t *edges, uint32_t n_edges);
/** @brief Bit-bang ARRAY read: where does data physically live? */
void tiku_psram_bitbang_mem(uint32_t addr, uint8_t *edges, uint32_t n_edges);
/** @brief The generic bit-banged command underneath both. */
void tiku_psram_bitbang_cmd(uint32_t opcode, uint32_t addr,
                            uint8_t *edges, uint32_t n_edges);

/**
 * @brief Deliberately break the bus, for proving the guards fire.
 *
 * Sets the pad function of D0 back to plain GPIO, so the next transfer must
 * fail: a driver whose error paths have never been seen to trigger is a
 * driver with untested error paths.  @p restore puts it back.
 */
void tiku_psram_fault_inject(int enable);

#endif /* TIKU_PSRAM_ARCH_H_ */
