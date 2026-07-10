/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trng_arch.c - nRF54L15 True Random Number Generator (CRACEN RNG)
 *
 * Hardware: the nRF54L15 has no classic RNG peripheral.  Its entropy
 * source is the ring-oscillator TRNG inside CRACEN (the Crypto
 * Accelerator Engine).  Two register views are involved, both through
 * their secure (_S) aliases since the app runs All-Secure:
 *   - NRF_CRACEN_S      -- the Nordic wrapper; ENABLE.RNG gates power
 *                          and clock to the RNG sub-module.
 *   - NRF_CRACENCORE_S  -- the crypto core; RNGCONTROL.* holds the TRNG
 *                          control / status / FIFO registers.
 *
 * Entropy path (mirrors Nordic's nrfx_cracen driver -- the vendor's
 * validated polled sequence):
 *   1. Enable NRF_CRACEN_S->ENABLE.RNG for the duration of the request.
 *   2. Configure RNGCONTROL: pulse SOFTRST, program the warm-up and
 *      sample-clock counters, then enable with AES conditioning over
 *      four 128-bit blocks (NB128BITBLOCKS = 4).  The AES conditioner
 *      is this silicon's entropy / bias-correction stage -- there is no
 *      Von-Neumann blending field on the nRF54L15, so the conditioning
 *      function is the debiasing.
 *   3. Wait for the FSM to leave RESET/STARTUP, then seed the AES
 *      conditioning key KEY[0..3] from the first four FIFO words.
 *   4. Poll RNGCONTROL.FIFOLEVEL and drain conditioned words from
 *      RNGCONTROL.FIFO[0] (little-endian byte order) until the caller's
 *      buffer is full.
 *   5. Disable NRF_CRACEN_S->ENABLE.RNG so the entropy path draws no
 *      power while idle (the FSM also switches the ring oscillators off
 *      once the FIFO is full).
 *
 * No pseudo-random fallback: if the hardware never delivers within the
 * spin budget the driver returns TIKU_TRNG_ERR_TIMEOUT and writes
 * nothing fabricated into the caller's buffer.  An FSM ERROR (startup /
 * health-test failure) triggers a reset-and-retry inside the same
 * budget rather than emitting suspect bytes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_trng_arch.h>
#include <arch/nordic/mdk/nrf54l15.h>

/**
 * @defgroup trng_config CRACEN RNG private configuration
 * @brief Tuning constants for the nRF54L15 CRACEN TRNG driver.
 *
 * The values match Nordic's nrfx_cracen defaults for this silicon.
 * @{
 */
/** @brief SWOFFTMRVAL: switch the rings off immediately when idle. */
#define TRNG_OFF_TIMER_VAL      0U
/** @brief CLKDIV: sample rate Fs = Fpclk / (CLKDIV + 1) (MDK L15 fixup). */
#define TRNG_CLK_DIV            0U
/** @brief INITWAITVAL: ring-oscillator start-up warm-up (sample clocks). */
#define TRNG_INIT_WAIT_VAL      512U
/** @brief NB128BITBLOCKS: 128-bit blocks folded by the AES conditioner. */
#define TRNG_NB_128BIT_BLOCKS   4U
/** @brief Words drawn from the FIFO to seed the AES conditioning key. */
#define TRNG_COND_KEY_WORDS     4U
/** @brief Bytes produced per drain pass; <= 64 B FIFO, kept conservative. */
#define TRNG_CHUNK_BYTES        32U
/** @brief Spin budget while polling the FSM / FIFO before giving up. */
#define TRNG_SPIN_LIMIT         2000000UL
/** @} */

/**
 * @defgroup trng_get_result Internal drain-attempt result codes
 * @{
 */
#define TRNG_GET_OK       0   /**< Requested bytes drained into the buffer. */
#define TRNG_GET_PENDING  1   /**< Hardware still producing; poll again.    */
#define TRNG_GET_RESET    2   /**< FSM in ERROR; reset and reconfigure.     */
/** @} */

/** @brief Non-zero once tiku_trng_arch_init() has run. */
static uint8_t trng_initialised;

/**
 * @brief Read the RNG finite-state-machine state field.
 *
 * @return One of CRACENCORE_RNGCONTROL_STATUS_STATE_* (RESET, STARTUP,
 *         IDLERON, IDLEROFF, FILLFIFO, ERROR).
 */
static uint32_t
trng_fsm_state(void)
{
    return (NRF_CRACENCORE_S->RNGCONTROL.STATUS
            & CRACENCORE_RNGCONTROL_STATUS_STATE_Msk)
           >> CRACENCORE_RNGCONTROL_STATUS_STATE_Pos;
}

/**
 * @brief Number of 32-bit words currently available in the entropy FIFO.
 */
static uint32_t
trng_fifo_level(void)
{
    return NRF_CRACENCORE_S->RNGCONTROL.FIFOLEVEL;
}

/**
 * @brief Soft-reset and (re-)enable the CRACEN RNG core.
 *
 * Pulses SOFTRST (disabling the FSM and clearing the continuous test,
 * conditioning function and FIFO), programs the warm-up / sample-clock
 * counters, then re-enables the core with AES conditioning active over
 * TRNG_NB_128BIT_BLOCKS blocks.  Leaves the FSM restarting; the caller
 * polls trng_fsm_state() until it leaves RESET.
 */
static void
trng_configure(void)
{
    /* Disable + soft reset: SOFTRST alone (ENABLE bit clear). */
    NRF_CRACENCORE_S->RNGCONTROL.CONTROL =
        CRACENCORE_RNGCONTROL_CONTROL_SOFTRST_Msk;

    /* Tuning counters (written while held in soft reset). */
    NRF_CRACENCORE_S->RNGCONTROL.SWOFFTMRVAL = TRNG_OFF_TIMER_VAL;
    NRF_CRACENCORE_S->RNGCONTROL.CLKDIV      = TRNG_CLK_DIV;
    NRF_CRACENCORE_S->RNGCONTROL.INITWAITVAL = TRNG_INIT_WAIT_VAL;

    /* Enable with AES conditioning; SOFTRST deasserts on this write. */
    NRF_CRACENCORE_S->RNGCONTROL.CONTROL =
        CRACENCORE_RNGCONTROL_CONTROL_ENABLE_Msk
        | ((TRNG_NB_128BIT_BLOCKS
              << CRACENCORE_RNGCONTROL_CONTROL_NB128BITBLOCKS_Pos)
           & CRACENCORE_RNGCONTROL_CONTROL_NB128BITBLOCKS_Msk);
}

/**
 * @brief One drain attempt: try to fill @p size bytes from the FIFO.
 *
 * Checks the FSM, seeds the AES conditioning key from the first four
 * FIFO words if not done yet (@p p_key_set), then -- only if enough
 * conditioned words are already queued -- drains @p size bytes in
 * little-endian order.  Never blocks: returns TRNG_GET_PENDING when the
 * hardware needs more time so the caller can poll.
 *
 * @param dst        Destination buffer (caller guarantees @p size room).
 * @param size       Bytes to produce this pass (<= TRNG_CHUNK_BYTES).
 * @param p_key_set  In/out flag tracking whether KEY[] is programmed.
 * @return TRNG_GET_OK when @p size bytes were written, TRNG_GET_PENDING
 *         if the FIFO is not ready, TRNG_GET_RESET if the FSM faulted.
 */
static int
trng_get(uint8_t *dst, size_t size, int *p_key_set)
{
    uint32_t state = trng_fsm_state();
    uint32_t level;
    unsigned i;

    if (state == CRACENCORE_RNGCONTROL_STATUS_STATE_ERROR) {
        return TRNG_GET_RESET;
    }
    if (state == CRACENCORE_RNGCONTROL_STATUS_STATE_RESET) {
        return TRNG_GET_PENDING;   /* still starting up */
    }

    /* Seed the AES conditioning key once, from raw FIFO entropy. */
    if (!*p_key_set) {
        if (trng_fifo_level() < TRNG_COND_KEY_WORDS) {
            return TRNG_GET_PENDING;
        }
        for (i = 0; i < TRNG_COND_KEY_WORDS; i++) {
            NRF_CRACENCORE_S->RNGCONTROL.KEY[i] =
                NRF_CRACENCORE_S->RNGCONTROL.FIFO[0];
        }
        *p_key_set = 1;
    }

    /* Only drain once enough conditioned words are queued. */
    level = trng_fifo_level();
    if (size > (size_t)level * 4U) {
        return TRNG_GET_PENDING;
    }

    while (size != 0U) {
        uint32_t data = NRF_CRACENCORE_S->RNGCONTROL.FIFO[0];
        for (i = 0; i < 4U && size != 0U; i++) {
            *dst++ = (uint8_t)(data & 0xFFU);
            data >>= 8;
            size--;
        }
    }
    return TRNG_GET_OK;
}

/**
 * @brief Produce @p len random bytes with the RNG module already enabled.
 *
 * Configures the core once, then loops draining chunks via trng_get(),
 * spinning on TRNG_GET_PENDING and reconfiguring on TRNG_GET_RESET.  The
 * spin budget is reset on every byte of progress, so a healthy but slow
 * FIFO cannot trip the timeout -- only a genuinely stuck FSM does.
 *
 * @param buf  Destination buffer (non-NULL, @p len bytes).
 * @param len  Number of bytes to produce (> 0).
 * @return TIKU_TRNG_OK on success, TIKU_TRNG_ERR_TIMEOUT if the hardware
 *         stalled past the spin budget.
 */
static int
trng_request(uint8_t *buf, size_t len)
{
    unsigned long spin = 0;
    int           key_set = 0;
    size_t        done = 0;

    trng_configure();

    while (done < len) {
        size_t want = len - done;
        int    r;

        if (want > TRNG_CHUNK_BYTES) {
            want = TRNG_CHUNK_BYTES;
        }

        r = trng_get(buf + done, want, &key_set);
        if (r == TRNG_GET_OK) {
            done += want;
            spin = 0;              /* progress: refresh the stall budget */
            continue;
        }
        if (r == TRNG_GET_RESET) {
            trng_configure();      /* health-test tripped: reset + retry */
            key_set = 0;
        }
        if (++spin >= TRNG_SPIN_LIMIT) {
            return TIKU_TRNG_ERR_TIMEOUT;
        }
    }
    return TIKU_TRNG_OK;
}

/**
 * @brief Initialise the CRACEN TRNG driver.
 *
 * The RNG sub-module is powered per request (enabled in read paths,
 * disabled afterwards) so the entropy path draws no power while idle;
 * there is nothing to bring up until the first read.  Idempotent.
 */
void
tiku_trng_arch_init(void)
{
    if (trng_initialised) {
        return;
    }
    trng_initialised = 1;
}

/**
 * @brief Fill a byte buffer with hardware random data.
 *
 * Enables the CRACEN RNG module, drives the polled entropy path to
 * produce @p len conditioned bytes, then disables the module.  On any
 * hardware stall @p buf may be partially written and an error is
 * returned; no pseudo-random data is ever substituted.
 *
 * @param buf  Destination buffer (must not be NULL).
 * @param len  Number of random bytes requested.
 * @return TIKU_TRNG_OK on success, TIKU_TRNG_ERR_INVALID if @p buf is
 *         NULL, or TIKU_TRNG_ERR_TIMEOUT if the RNG did not deliver.
 */
int
tiku_trng_arch_read_bytes(uint8_t *buf, size_t len)
{
    int rc;

    if (buf == (uint8_t *)0) {
        return TIKU_TRNG_ERR_INVALID;
    }
    if (!trng_initialised) {
        tiku_trng_arch_init();
    }
    if (len == 0U) {
        return TIKU_TRNG_OK;
    }

    /* Power the RNG sub-module up for the request, down afterwards. */
    NRF_CRACEN_S->ENABLE |= CRACEN_ENABLE_RNG_Msk;
    rc = trng_request(buf, len);
    NRF_CRACEN_S->ENABLE &= ~CRACEN_ENABLE_RNG_Msk;

    return rc;
}

/**
 * @brief Read one 32-bit random word from the TRNG.
 *
 * Draws four fresh bytes via tiku_trng_arch_read_bytes() and packs them
 * little-endian, matching the byte order of the buffer path.
 *
 * @param out  Destination for the random word (must not be NULL).
 * @return TIKU_TRNG_OK on success, TIKU_TRNG_ERR_INVALID if @p out is
 *         NULL, or TIKU_TRNG_ERR_TIMEOUT if the RNG did not deliver;
 *         *out is left untouched on error.
 */
int
tiku_trng_arch_read_u32(uint32_t *out)
{
    uint8_t b[4];
    int     rc;

    if (out == (uint32_t *)0) {
        return TIKU_TRNG_ERR_INVALID;
    }

    rc = tiku_trng_arch_read_bytes(b, sizeof b);
    if (rc != TIKU_TRNG_OK) {
        return rc;
    }

    *out = (uint32_t)b[0]
         | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16)
         | ((uint32_t)b[3] << 24);
    return TIKU_TRNG_OK;
}
