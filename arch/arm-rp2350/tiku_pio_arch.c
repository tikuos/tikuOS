/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pio_arch.c - RP2350 PIO driver, bit-bang state-machine flavour.
 *
 * Runs one PIO0 state machine as the kernel bit-bang engine: each call resets
 * the SM, sets its clock divider for the bit period, pushes the data word and
 * takes completion from PIO0_IRQ_0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pio_arch.h"
#include "tiku_rp2350_regs.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* PIO PROGRAM                                                               */
/*---------------------------------------------------------------------------*/

/*
 * Instruction encoding (RP2350 datasheet §11.4.4).
 *
 * The program does its own setup (set pindirs, pull).  The driver forces one
 * instruction, set x, bit_count - 1, through SMx_INSTR, which holds a single
 * instruction: a second write before the first has run replaces it.
 *
 *   SET pindirs, 1     opcode=111 dst=100 imm=00001 = 0xE081
 *                      Put the OUT pin into output mode.
 *
 *   PULL block         opcode=100 push=0 ifempty=0 block=1 = 0x80A0
 *                      Wait for a TX-FIFO word, copy it into OSR.
 *
 *   OUT pins, 1        opcode=011 dst=pins(000) count=00001 = 0x6001
 *                      Shift one bit out of OSR onto the pin.
 *
 *   JMP x-- 2          opcode=000 cond=010(x--) addr=00010 = 0x0042
 *                      Loop while X != 0 (jump target = OUT slot).
 *
 *   IRQ nowait 0       opcode=110 ...           = 0xC000
 *                      Raise PIO IRQ flag 0 -> NVIC via INTE.
 *
 *   JMP 5              opcode=000 cond=000 addr=00101 = 0x0005
 *                      Jump to itself: the SM spins here until the IRQ
 *                      handler disables it.
 *
 * Reaching `irq nowait 0` sets PIO IRQ flag 0; with PIO0_IRQ0_INTE.SM0_IRQ
 * enabled, NVIC IRQ 15 (PIO0_IRQ_0) runs tiku_rp2350_pio0_irq0_handler().
 */

/**
 * @brief The six-instruction bit-bang program, loaded into PIO0 instruction
 *        memory at address 0.
 */
static const uint16_t bitbang_program[] = {
    0xE081U,   /* 0: set pindirs, 1 -- pin to output mode */
    0x80A0U,   /* 1: pull block     -- wait for TXF word -> OSR */
    0x6001U,   /* 2: out pins, 1    -- shift one bit out */
    0x0042U,   /* 3: jmp x-- 2      -- loop while X != 0 */
    0xC000U,   /* 4: irq nowait 0   -- signal CPU */
    0x0005U,   /* 5: jmp 5          -- halt SM here */
};
/** @brief Number of instructions in bitbang_program. */
#define BITBANG_PROG_LEN \
    (sizeof(bitbang_program) / sizeof(bitbang_program[0]))

#define BITBANG_PROG_BASE   0U   /* first PIO0 instruction-memory slot used */
#define BITBANG_SM          0U   /* SM0 runs the program */

/*---------------------------------------------------------------------------*/
/* PIO INSTRUCTION BUILDERS                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Build a "set x, value" PIO instruction (opcode=111 dst=001 imm=value).
 *
 * @param value  5-bit immediate to load into the X scratch register (0-31).
 * @return       Encoded 16-bit PIO instruction word.
 */
static inline uint16_t pio_instr_set_x(uint8_t value) {
    /* 111 00000 001 vvvvv */
    return (uint16_t)(0xE020U | (uint16_t)(value & 0x1FU));
}

/**
 * @brief Build an "out x, 32" PIO instruction (shift 32 bits from OSR into X).
 *
 * Nothing in this driver calls it: set x covers bit_count - 1 up to 31.
 *
 * @return  Encoded 16-bit PIO instruction word.
 */
static inline uint16_t pio_instr_out_x_32(void) {
    /* 011 00000 001 (dst=x) 00000 (count=32) */
    return 0x6020U;
}

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

static uint8_t g_pio_initialised;           /**< set by tiku_pio_arch_init() */
static volatile uint8_t g_pio_busy;         /**< a transmission is running */
static tiku_pio_done_cb_t g_pio_done_cb;    /**< completion callback or NULL */
static void              *g_pio_done_ctx;   /**< argument for g_pio_done_cb */
static uint8_t            g_pio_active_pin; /**< pin of the last transmission */
static uint8_t            g_pio_idle_level; /**< unused */

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Read/write a PIO0 MMIO register at byte offset @p off. */
#define PIO0(off)   _RP2350_REG(RP2350_PIO0_BASE + (off))

/**
 * @brief Disable a PIO state machine and restart its internal state.
 *
 * tiku_pio_arch_bitbang_tx() calls it before each transmission, which needs
 * the SM to start at address 0.  The restart does not move the program
 * counter: after a completed transmission the SM is still on the jmp-to-self.
 *
 * @param sm  State machine index (0-3).
 */
static void pio_sm_disable_restart(uint8_t sm) {
    /* Clear SM_ENABLE in CTRL. */
    PIO0(RP2350_PIO_CTRL) &= ~RP2350_PIO_CTRL_SM_ENABLE(sm);

    /* SM_RESTART clears the shift counters, the ISR, the delay counter and a
     * stalled forced instruction; the PC, OSR, X and Y keep their values.
     * CLKDIV_RESTART restarts the clock divider. */
    PIO0(RP2350_PIO_CTRL) |= RP2350_PIO_CTRL_SM_RESTART(sm)
                          |  RP2350_PIO_CTRL_CLKDIV_RESTART(sm);
}

/**
 * @brief Enable a PIO state machine so it begins executing instructions.
 *
 * @param sm  State machine index (0-3).
 */
static void pio_sm_enable(uint8_t sm) {
    PIO0(RP2350_PIO_CTRL) |= RP2350_PIO_CTRL_SM_ENABLE(sm);
}

/**
 * @brief Drain the TX FIFO of a PIO state machine.
 *
 * Changing SHIFTCTRL.FJOIN_RX flushes both FIFOs; the second write restores
 * the original value.
 *
 * @param sm  State machine index (0-3).
 */
static void pio_sm_drain_tx_fifo(uint8_t sm) {
    uint32_t sc = PIO0(RP2350_PIO_SM_SHIFTCTRL(sm));
    PIO0(RP2350_PIO_SM_SHIFTCTRL(sm)) = sc ^ RP2350_PIO_SHIFTCTRL_FJOIN_RX;
    PIO0(RP2350_PIO_SM_SHIFTCTRL(sm)) = sc;
}

/**
 * @brief Force a PIO state machine to execute one instruction.
 *
 * Writes @p instr to SMx_INSTR; the SM runs it ahead of its program, even
 * while disabled.  A second write before it has run replaces it.
 *
 * @param sm     State machine index (0-3).
 * @param instr  Encoded 16-bit PIO instruction to execute.
 */
static void pio_sm_exec(uint8_t sm, uint16_t instr) {
    PIO0(RP2350_PIO_SM_INSTR(sm)) = instr;
}

/**
 * @brief Convert a microseconds-per-bit period to the SM_CLKDIV register value.
 *
 * divider = bit_period_us * clk_sys_hz / 1e6, formatted as the 16.8 fixed point
 * SM_CLKDIV expects ([31:16] integer, [15:8] fractional).  At clk_sys = 150 MHz
 * and bit_period_us = 200 that is 30000 -> 0x7530_0000.
 *
 * @param bit_period_us  Desired bit period in microseconds.
 * @return               SM_CLKDIV register value in 16.8 fixed-point format.
 */
static uint32_t bitperiod_us_to_clkdiv(uint16_t bit_period_us) {
    extern unsigned long tiku_cpu_rp2350_clock_get_hz(void);
    uint64_t clk_sys_hz = (uint64_t)tiku_cpu_rp2350_clock_get_hz();
    uint64_t div_x256 = ((uint64_t)bit_period_us * clk_sys_hz * 256ULL)
                        / 1000000ULL;
    /* div_x256 is the divider with 8 fraction bits; the shift by 8 moves
     * the integer part to [31:16] and the fraction to [15:8].  An integer
     * part above 0xFFFF does not fit and the 32-bit cast drops it. */
    return (uint32_t)(div_x256 << 8);
}

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the PIO0 bit-bang driver.
 *
 * Brings PIO0 out of reset, loads the program into instruction memory slots
 * 0-5 and enables NVIC IRQ 15 (PIO0_IRQ_0).  A second call returns at once.
 */
void tiku_pio_arch_init(void) {
    uint8_t i;

    if (g_pio_initialised) {
        return;
    }

    /* Bring PIO0 out of reset. */
    rp2350_unreset(RP2350_RESETS_PIO0);

    /* Disable all four state machines. */
    PIO0(RP2350_PIO_CTRL) = 0U;

    /* Load the program into slots 0-5. */
    for (i = 0; i < BITBANG_PROG_LEN; i++) {
        PIO0(RP2350_PIO_INSTR_MEM(BITBANG_PROG_BASE + i)) =
            bitbang_program[i];
    }

    /* Enable PIO0_IRQ_0 in the NVIC.  IRQ0_INTE is set for each transmission
     * by tiku_pio_arch_bitbang_tx() and cleared on completion or abort. */
    rp2350_nvic_enable(RP2350_IRQ_PIO0_0);

    g_pio_initialised = 1U;
}

/**
 * @brief Start a non-blocking PIO bit-bang transmission on a GPIO pin.
 *
 * Configures SM0 on PIO0 for the requested pin, bit order and bit period, then
 * starts it.  When the program reaches `irq nowait 0` the ISR clears the busy
 * flag and calls @p on_done in interrupt context.
 *
 * @note One transmission at a time.  Poll with tiku_pio_arch_bitbang_busy()
 *       or cancel with tiku_pio_arch_bitbang_abort().
 * @param gpio_pin      GPIO pin number to drive (0-based, RP2350 bank 0).
 * @param data          Data word: MSB-first data starts at bit 31, LSB-first
 *                      data at bit 0.
 * @param bit_count     Number of bits to transmit (1-32).
 * @param msb_first     Non-zero for MSB-first shift order; 0 for LSB-first.
 * @param bit_period_us Desired bit period in microseconds (must be > 0).
 * @param on_done       Completion callback invoked from ISR context, or NULL.
 * @param ctx           Opaque pointer forwarded to @p on_done.
 * @return              TIKU_PIO_OK on success; TIKU_PIO_ERR_NOT_READY if the
 *                      driver is uninitialised; TIKU_PIO_ERR_BUSY if a
 *                      transmission is already in progress;
 *                      TIKU_PIO_ERR_INVALID for out-of-range parameters.
 */
int tiku_pio_arch_bitbang_tx(uint8_t  gpio_pin,
                             uint32_t data,
                             uint8_t  bit_count,
                             uint8_t  msb_first,
                             uint16_t bit_period_us,
                             tiku_pio_done_cb_t on_done,
                             void   *ctx) {
    uint32_t shiftctrl;
    uint32_t pinctrl;
    uint16_t set_x;
    uint32_t shifted_data;

    if (!g_pio_initialised) {
        return TIKU_PIO_ERR_NOT_READY;
    }
    if (g_pio_busy) {
        return TIKU_PIO_ERR_BUSY;
    }
    if (bit_count == 0U || bit_count > 32U || bit_period_us == 0U) {
        return TIKU_PIO_ERR_INVALID;
    }

    g_pio_busy       = 1U;
    g_pio_done_cb    = on_done;
    g_pio_done_ctx   = ctx;
    g_pio_active_pin = gpio_pin;

    /* 1. Route the GPIO to PIO0.  The pad keeps its input buffer on; the
     * output enable comes from the SM's pindirs. */
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(gpio_pin)) =
        RP2350_PADS_DRIVE_4MA | RP2350_PADS_IE;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(gpio_pin)) =
        RP2350_IO_FUNC_PIO0;

    /* 2. Disable + restart SM. Drain the TX FIFO of any stale words. */
    pio_sm_disable_restart(BITBANG_SM);
    pio_sm_drain_tx_fifo(BITBANG_SM);

    /* 3. Configure clock divider for the requested bit period. Each
     * `out pins, 1` instruction takes 1 SM clock; `jmp x--` takes 1
     * more, so the actual loop period is 2 SM clocks per bit. Halve
     * the divider so the wall-clock bit period matches. */
    uint32_t clkdiv = bitperiod_us_to_clkdiv(bit_period_us) / 2U;
    if (clkdiv < 0x00010000U) {
        /* Less than divisor 1.0 -- saturate at minimum (clk_sys). */
        clkdiv = 0x00010000U;
    }
    PIO0(RP2350_PIO_SM_CLKDIV(BITBANG_SM)) = clkdiv;

    /* 4. Configure shift direction. Pull threshold = 32 so each pull
     * supplies a full word; only one word is pushed per tx. */
    shiftctrl = (32U & 0x1FU) << RP2350_PIO_SHIFTCTRL_PULL_THRESH_SHIFT;
    if (msb_first) {
        shiftctrl |= RP2350_PIO_SHIFTCTRL_OUT_SHIFTDIR_LEFT;
    } else {
        shiftctrl |= RP2350_PIO_SHIFTCTRL_OUT_SHIFTDIR_RIGHT;
    }
    /* No autopull -- exactly one word is pushed per tx and the SM
     * stops at the jmp-to-self before trying to pull a second. */
    PIO0(RP2350_PIO_SM_SHIFTCTRL(BITBANG_SM)) = shiftctrl;

    /* 5. Configure pin assignment: SET base + OUT base both point at
     * the target pin so `set pindirs, 1` can drive it. */
    pinctrl = ((uint32_t)gpio_pin
                  << RP2350_PIO_PINCTRL_OUT_BASE_SHIFT) |
              ((uint32_t)gpio_pin
                  << RP2350_PIO_PINCTRL_SET_BASE_SHIFT) |
              (1U << RP2350_PIO_PINCTRL_OUT_COUNT_SHIFT) |
              (1U << RP2350_PIO_PINCTRL_SET_COUNT_SHIFT);
    PIO0(RP2350_PIO_SM_PINCTRL(BITBANG_SM)) = pinctrl;

    /* 6. Preload X with bit_count - 1 through SMx_INSTR.  The forced
     * instruction runs before the program's first fetch, so X is set
     * before the JMP x-- loop.  SMx_INSTR holds one instruction and a
     * second write replaces one that has not run, so this is the only forced
     * instruction; set pindirs and pull are in the program (slots 0, 1). */
    set_x = pio_instr_set_x((uint8_t)(bit_count - 1U));
    pio_sm_exec(BITBANG_SM, set_x);

    /* 7. Push the data word.  MSB-first, the SM shifts from bit 31 of
     * OSR, so the first bit on the wire is bit 31 of data.  The program's
     * PULL copies the word into OSR. */
    if (msb_first) {
        shifted_data = data << (32U - bit_count);
    } else {
        shifted_data = data;
    }
    PIO0(RP2350_PIO_TXF(BITBANG_SM)) = shifted_data;

    /* 8. Clear stale PIO IRQ flags, route SM IRQ flag 0 to PIO0_IRQ_0,
     * and clear-pend and re-enable that NVIC line, whatever state it was
     * left in since init.  The barriers complete these writes before the
     * SM starts. */
    PIO0(RP2350_PIO_IRQ) = 0xFFU;  /* clear any stale IRQ flags */
    PIO0(RP2350_PIO_IRQ0_INTE) = RP2350_PIO_INT_SM0_IRQ;
    rp2350_nvic_clear_pending(RP2350_IRQ_PIO0_0);
    rp2350_nvic_enable(RP2350_IRQ_PIO0_0);
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");

    /* 9. Start the SM.  From address 0 it runs set pindirs, then PULL
     * copies the TX FIFO word into OSR, then each bit takes an OUT and a
     * JMP (two SM clocks), and irq nowait 0 raises the IRQ. */
    pio_sm_enable(BITBANG_SM);

    return TIKU_PIO_OK;
}

/**
 * @brief Query whether a PIO bit-bang transmission is in progress.
 *
 * @return  Non-zero if a transmission is in progress; 0 if idle.
 */
int tiku_pio_arch_bitbang_busy(void) {
    return g_pio_busy != 0U;
}

/**
 * @brief Abort an in-progress PIO bit-bang transmission immediately.
 *
 * Disables the PIO IRQ source, stops and drains SM0, and clears the busy
 * flag.  The registered completion callback is not called.
 *
 * @return  TIKU_PIO_OK on success; TIKU_PIO_ERR_NOT_READY if no
 *          transmission was in progress.
 */
int tiku_pio_arch_bitbang_abort(void) {
    if (!g_pio_busy) {
        return TIKU_PIO_ERR_NOT_READY;
    }

    /* Stop the PIO raising the completion interrupt. */
    PIO0(RP2350_PIO_IRQ0_INTE) = 0U;

    /* Stop the SM and drain any pending FIFO contents. */
    pio_sm_disable_restart(BITBANG_SM);
    pio_sm_drain_tx_fifo(BITBANG_SM);

    /* Clear all PIO IRQ flags. */
    PIO0(RP2350_PIO_IRQ) = 0xFFU;

    g_pio_busy     = 0U;
    g_pio_done_cb  = NULL;
    g_pio_done_ctx = NULL;

    return TIKU_PIO_OK;
}

/**
 * @brief ISR for NVIC IRQ 15 (PIO0_IRQ_0): bit-bang transmission complete.
 *
 * Clears the SM0 IRQ flag, disables the PIO IRQ source, stops SM0, clears the
 * busy state and calls the completion callback.  The callback runs with the
 * driver idle, so it may start the next transmission.
 */
void tiku_rp2350_pio0_irq0_handler(void) {
    /* Clear the SM0 IRQ flag (W1C). */
    PIO0(RP2350_PIO_IRQ) = 0x01U;

    /* Disable IRQ source so it does not fire again until the next tx. */
    PIO0(RP2350_PIO_IRQ0_INTE) = 0U;

    /* The SM is spinning on its jmp-to-self; disable it. */
    PIO0(RP2350_PIO_CTRL) &= ~RP2350_PIO_CTRL_SM_ENABLE(BITBANG_SM);

    /* Copy the callback out and clear the state first, so a transmission
     * started from the callback finds the driver idle. */
    tiku_pio_done_cb_t cb = g_pio_done_cb;
    void *ctx             = g_pio_done_ctx;
    g_pio_busy            = 0U;
    g_pio_done_cb         = NULL;
    g_pio_done_ctx        = NULL;

    if (cb != NULL) {
        cb(ctx);
    }
}
