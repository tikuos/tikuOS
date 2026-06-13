/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pio_arch.c - RP2350 PIO driver, bitbang state-machine flavour
 *
 * Drives one state machine on PIO0 as a hardware-offloaded GPIO
 * bitbang engine for kernel/timers/tiku_bitbang.c. Once the program
 * is loaded and the SM is configured, each tiku_pio_arch_bitbang_tx()
 * call:
 *
 *   1. Resets SM0 to instruction 0.
 *   2. Sets the SM clock divider so each bit takes the requested us.
 *   3. Configures pins (out_base = gpio_pin, out_count = 1) and
 *      shift direction (MSB-first or LSB-first).
 *   4. Forces 'set x, bit_count-1' via the EXEC register.
 *   5. Pushes the data word into the TX FIFO.
 *   6. Enables SM0. The SM runs the program at full clk_sys-relative
 *      rate, fires PIO IRQ 0 when done, and halts at the jmp-to-self
 *      instruction (no further FIFO pulls).
 *
 * The PIO0_IRQ_0 ISR clears the IRQ flag, sets the not-busy state,
 * and invokes the kernel completion callback.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pio_arch.h"
#include "tiku_rp2350_regs.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* PIO program (handwritten -- no pioasm toolchain dependency)               */
/*---------------------------------------------------------------------------*/

/*
 * Instruction encoding (RP2350 datasheet §11.4.4).
 *
 * The program bakes its own setup in (set pindirs, pull) rather than
 * relying on multiple SMx_INSTR force-execs from the driver: SMx_INSTR
 * is a single-slot queue, so back-to-back driver writes overwrite each
 * other and only the LAST one survives. With setup inside the program,
 * the driver only force-execs ONE instruction (set x, bit_count-1) and
 * the rest of the sequence runs naturally as the SM advances its PC.
 *
 *   SET pindirs, 1     opcode=111 dst=100 imm=00001 = 0xE081
 *                      Put the OUT pin into output mode.
 *
 *   PULL block         opcode=100 push=0 ifempty=0 block=1 = 0x80A0
 *                      Wait for a TX-FIFO word, copy it into OSR.
 *                      Blocking is what makes the SM safe to enable
 *                      *before* the CPU has written to TXF.
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
 *                      Self-jump halt; SM stops doing useful work
 *                      but stays "running" until the CPU disables it
 *                      from the IRQ handler.
 *
 * Completion = SM reaches the `irq nowait 0` instruction, which sets
 * PIO_IRQ flag 0; with PIO0_IRQ0_INTE.SM0_IRQ enabled, NVIC IRQ 15
 * (PIO0_IRQ_0) fires -> tiku_rp2350_pio0_irq0_handler runs.
 */
/**
 * @brief Six-instruction PIO bitbang program loaded into PIO0 at slot 0.
 *
 * Encodes: set pindirs/pull/out pins/jmp x--/irq nowait/jmp self.
 * See the encoding commentary above for full opcode derivations.
 */
static const uint16_t bitbang_program[] = {
    0xE081U,   /* 0: set pindirs, 1 -- pin to output mode */
    0x80A0U,   /* 1: pull block     -- wait for TXF word -> OSR */
    0x6001U,   /* 2: out pins, 1    -- shift one bit out */
    0x0042U,   /* 3: jmp x-- 2      -- loop while X != 0 */
    0xC000U,   /* 4: irq nowait 0   -- signal CPU */
    0x0005U,   /* 5: jmp 5          -- halt SM here */
};
/** @brief Program length, load offset, and owning SM index for the bitbang
 *         program. BITBANG_PROG_BASE is the first PIO instruction-memory slot
 *         used; BITBANG_SM is the state machine that executes the program. */
#define BITBANG_PROG_LEN \
    (sizeof(bitbang_program) / sizeof(bitbang_program[0]))

#define BITBANG_PROG_BASE   0U   /* loaded at slot 0 in PIO0 instr mem */
#define BITBANG_SM          0U   /* SM0 owns the bitbang stream */

/*---------------------------------------------------------------------------*/
/* PIO instruction builders (for runtime-exec via SM_INSTR)                  */
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
 * Not used in the current driver (bit_count is capped at 32 and handled via
 * set x), but retained for future longer-burst support where a 5-bit immediate
 * is insufficient.
 *
 * @return  Encoded 16-bit PIO instruction word.
 */
static inline uint16_t pio_instr_out_x_32(void) {
    /* 011 00000 011 (dst=x) 00000 (count=32) */
    return 0x6020U;
}

/*---------------------------------------------------------------------------*/
/* State                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief PIO driver state. */
static uint8_t g_pio_initialised;
static volatile uint8_t g_pio_busy;
static tiku_pio_done_cb_t g_pio_done_cb;
static void              *g_pio_done_ctx;
static uint8_t            g_pio_active_pin;
static uint8_t            g_pio_idle_level;  /* not used yet; future */

/*---------------------------------------------------------------------------*/
/* Helpers                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Read/write a PIO0 MMIO register at byte offset @p off. */
#define PIO0(off)   _RP2350_REG(RP2350_PIO0_BASE + (off))

/**
 * @brief Disable and restart a PIO state machine, clearing its internal state.
 *
 * After a completed transmission the SM sits at the jmp-to-self halt; this
 * resets it to the wrap-target so it is ready for the next transmission.
 *
 * @param sm  State machine index (0-3).
 */
static void pio_sm_disable_restart(uint8_t sm) {
    /* Clear SM_ENABLE in CTRL. */
    PIO0(RP2350_PIO_CTRL) &= ~RP2350_PIO_CTRL_SM_ENABLE(sm);

    /* Restart SM (clears internal state, returns PC to wrap-target). */
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
 * Achieves the drain by toggling SHIFTCTRL.FJOIN_RX twice — the same
 * technique used by the pico-sdk. The alternative of disabling and
 * re-initialising shift state is heavier and not needed here.
 *
 * @param sm  State machine index (0-3).
 */
static void pio_sm_drain_tx_fifo(uint8_t sm) {
    uint32_t sc = PIO0(RP2350_PIO_SM_SHIFTCTRL(sm));
    PIO0(RP2350_PIO_SM_SHIFTCTRL(sm)) = sc ^ RP2350_PIO_SHIFTCTRL_FJOIN_RX;
    PIO0(RP2350_PIO_SM_SHIFTCTRL(sm)) = sc;
}

/**
 * @brief Force a PIO state machine to execute one instruction immediately.
 *
 * The instruction runs out-of-band with respect to the program counter.
 * Used to preload the X scratch register with bit_count-1 before the SM
 * is enabled.
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
 * Derivation: divider = bit_period_us * clk_sys_hz / 1e6.
 * SM_CLKDIV is 16.8 fixed point ([31:16] integer, [15:8] fractional).
 * Example: clk_sys = 150 MHz, bit_period_us = 200 ->
 *   divider = 30000 -> 0x7530_0000.
 *
 * @param bit_period_us  Desired bit period in microseconds.
 * @return               SM_CLKDIV register value in 16.8 fixed-point format.
 */
static uint32_t bitperiod_us_to_clkdiv(uint16_t bit_period_us) {
    extern unsigned long tiku_cpu_rp2350_clock_get_hz(void);
    uint64_t clk_sys_hz = (uint64_t)tiku_cpu_rp2350_clock_get_hz();
    uint64_t div_x256 = ((uint64_t)bit_period_us * clk_sys_hz * 256ULL)
                        / 1000000ULL;
    /* div_x256 is the divider in 8.8 fixed-point inside a 32-bit space.
     * SM_CLKDIV uses 16.8 layout: integer part in [31:16], fraction in
     * [15:8], so shift left by 8. */
    return (uint32_t)(div_x256 << 8);
}

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the PIO0 bitbang driver (idempotent).
 *
 * Brings PIO0 out of reset, loads the bitbang program into instruction
 * memory slots 0-5, and enables NVIC IRQ 15 (PIO0_IRQ_0).  Safe to call
 * multiple times; subsequent calls return immediately.
 */
void tiku_pio_arch_init(void) {
    uint8_t i;

    if (g_pio_initialised) {
        return;
    }

    /* Bring PIO0 out of reset. */
    rp2350_unreset(RP2350_RESETS_PIO0);

    /* Disable all SMs on PIO0 (defensive). */
    PIO0(RP2350_PIO_CTRL) = 0U;

    /* Load the bitbang program at slot 0..3. */
    for (i = 0; i < BITBANG_PROG_LEN; i++) {
        PIO0(RP2350_PIO_INSTR_MEM(BITBANG_PROG_BASE + i)) =
            bitbang_program[i];
    }

    /* Enable PIO0_IRQ_0 in the NVIC. The PIO's own IRQ-enable mask
     * (IRQ0_INTE) is set per-transmission in bitbang_tx() so the IRQ
     * only fires when we expect it to. */
    rp2350_nvic_enable(RP2350_IRQ_PIO0_0);

    g_pio_initialised = 1U;
}

/**
 * @brief Start a non-blocking PIO bitbang transmission on a GPIO pin.
 *
 * Configures SM0 on PIO0 for the requested pin, bit-order, and bit period,
 * then starts the SM.  The SM runs to completion autonomously; when the
 * `irq nowait 0` instruction fires, the ISR clears the busy flag and
 * invokes @p on_done (if non-NULL) from interrupt context.
 *
 * Only one transmission may be in progress at a time.  Poll completion
 * with tiku_pio_arch_bitbang_busy() or cancel with
 * tiku_pio_arch_bitbang_abort().
 *
 * @param gpio_pin      GPIO pin number to drive (0-based, RP2350 bank 0).
 * @param data          Data word to shift out (up to 32 bits).
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

    /* 1. Route the GPIO to PIO0. PADS_IE keeps the input enable on
     * so the PIO can also observe the pin if needed; the OE comes
     * from the SM's PINDIRS settings below. */
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
     * supplies a full word; we'll only push one word per tx. */
    shiftctrl = (32U & 0x1FU) << RP2350_PIO_SHIFTCTRL_PULL_THRESH_SHIFT;
    if (msb_first) {
        shiftctrl |= RP2350_PIO_SHIFTCTRL_OUT_SHIFTDIR_LEFT;
    } else {
        shiftctrl |= RP2350_PIO_SHIFTCTRL_OUT_SHIFTDIR_RIGHT;
    }
    /* No autopull -- we push exactly one word per tx and the SM
     * stops at the jmp-to-self before trying to pull a second. */
    PIO0(RP2350_PIO_SM_SHIFTCTRL(BITBANG_SM)) = shiftctrl;

    /* 5. Configure pin assignment: SET base + OUT base both point at
     * the target pin so we can use `set pindirs, 1` to drive it. */
    pinctrl = ((uint32_t)gpio_pin
                  << RP2350_PIO_PINCTRL_OUT_BASE_SHIFT) |
              ((uint32_t)gpio_pin
                  << RP2350_PIO_PINCTRL_SET_BASE_SHIFT) |
              (1U << RP2350_PIO_PINCTRL_OUT_COUNT_SHIFT) |
              (1U << RP2350_PIO_PINCTRL_SET_COUNT_SHIFT);
    PIO0(RP2350_PIO_SM_PINCTRL(BITBANG_SM)) = pinctrl;

    /* 6. Pre-load X with bit_count - 1 via SMx_INSTR. The SM is
     * currently disabled; the write parks the instruction in the
     * 1-slot SMx_INSTR queue. On the FIRST tick after we enable
     * the SM below, this is the very first instruction that runs
     * (force-execs take priority over the program-counter fetch),
     * so X holds the right value before the program ever advances
     * to its own JMP x-- check.
     *
     * Critical: only ONE SMx_INSTR write between disable/enable.
     * SMx_INSTR is a single-element queue; back-to-back writes
     * overwrite each other and only the last one survives. The
     * earlier version of this driver did three writes (set
     * pindirs, jmp 0, set x) and lost the first two — the pin
     * stayed in input mode and the data was never PULL'd from
     * TXF into the OSR. set pindirs and pull are now inside the
     * PIO program (slots 0 and 1), so there is nothing to race. */
    set_x = pio_instr_set_x((uint8_t)(bit_count - 1U));
    pio_sm_exec(BITBANG_SM, set_x);

    /* 7. Push the data word. For MSB-first, the SM shifts the
     * top-most bit of the OSR first; we left-align our data into the
     * 32-bit word so the first bit on the wire is bit[31] of `data`.
     * The PULL inside the program will block on this FIFO entry until
     * we enable the SM, then copy it into OSR. */
    if (msb_first) {
        shifted_data = data << (32U - bit_count);
    } else {
        shifted_data = data;
    }
    PIO0(RP2350_PIO_TXF(BITBANG_SM)) = shifted_data;

    /* 8. Enable the SM0 IRQ to fire PIO0_IRQ_0 when the program
     * reaches the `irq nowait 0` instruction. Defensive: also
     * clear-pending and re-enable NVIC IRQ 15 here. The PIO arch
     * init only does this once at lazy first-tx; if any test path
     * disabled the NVIC entry (e.g. an aborted prior tx that the
     * crit-section mask didn't anticipate), this revives it. The
     * memory barriers ensure the NVIC + PIO state is visible
     * before the SM starts ticking. */
    PIO0(RP2350_PIO_IRQ) = 0xFFU;  /* clear any stale IRQ flags */
    PIO0(RP2350_PIO_IRQ0_INTE) = RP2350_PIO_INT_SM0_IRQ;
    rp2350_nvic_clear_pending(RP2350_IRQ_PIO0_0);
    rp2350_nvic_enable(RP2350_IRQ_PIO0_0);
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");

    /* 9. Go. SM tick 1 executes the queued set_x; tick 2 starts
     * the program at PC=0 (set pindirs, 1); tick 3 hits the PULL
     * which copies the TXF word into the OSR; ticks 4..N+3 shift
     * bits; tick N+4 fires the IRQ. */
    pio_sm_enable(BITBANG_SM);

    return TIKU_PIO_OK;
}

/**
 * @brief Query whether a PIO bitbang transmission is currently in progress.
 *
 * @return  Non-zero if a transmission is in progress; 0 if idle.
 */
int tiku_pio_arch_bitbang_busy(void) {
    return g_pio_busy != 0U;
}

/**
 * @brief Abort an in-progress PIO bitbang transmission immediately.
 *
 * Disables the PIO IRQ source, stops and drains SM0, and clears the busy
 * flag.  The registered completion callback is NOT invoked.
 *
 * @return  TIKU_PIO_OK on success; TIKU_PIO_ERR_NOT_READY if no
 *          transmission was in progress.
 */
int tiku_pio_arch_bitbang_abort(void) {
    if (!g_pio_busy) {
        return TIKU_PIO_ERR_NOT_READY;
    }

    /* Disable interrupts so the completion ISR can't race with us. */
    PIO0(RP2350_PIO_IRQ0_INTE) = 0U;

    /* Stop the SM and drain any pending FIFO contents. */
    pio_sm_disable_restart(BITBANG_SM);
    pio_sm_drain_tx_fifo(BITBANG_SM);

    /* Clear pending IRQ flag. */
    PIO0(RP2350_PIO_IRQ) = 0xFFU;

    g_pio_busy     = 0U;
    g_pio_done_cb  = NULL;
    g_pio_done_ctx = NULL;

    return TIKU_PIO_OK;
}

/**
 * @brief ISR for NVIC IRQ 15 (PIO0_IRQ_0) — bitbang transmission complete.
 *
 * Clears the SM0 IRQ flag, disables the PIO IRQ source, stops SM0, clears
 * the busy state, and invokes the registered completion callback (if any).
 * The callback is called with the driver already marked idle, so a
 * re-entrant tiku_pio_arch_bitbang_tx() from inside the callback is safe.
 */
void tiku_rp2350_pio0_irq0_handler(void) {
    /* Clear the SM0 IRQ flag (W1C). */
    PIO0(RP2350_PIO_IRQ) = 0x01U;

    /* Disable IRQ source so we don't fire again until the next tx. */
    PIO0(RP2350_PIO_IRQ0_INTE) = 0U;

    /* The SM has already entered the jmp-to-self halt instruction.
     * Disable it so it stops consuming clock cycles. */
    PIO0(RP2350_PIO_CTRL) &= ~RP2350_PIO_CTRL_SM_ENABLE(BITBANG_SM);

    /* Snapshot the callback locally so a re-entrant tx call from
     * inside the callback doesn't see stale state. */
    tiku_pio_done_cb_t cb = g_pio_done_cb;
    void *ctx             = g_pio_done_ctx;
    g_pio_busy            = 0U;
    g_pio_done_cb         = NULL;
    g_pio_done_ctx        = NULL;

    if (cb != NULL) {
        cb(ctx);
    }
}
