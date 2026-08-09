/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.c - RA8P1 I2C master.
 *
 * Polled master on IIC channel 1, the bus the camera and touch controller
 * share on the expansion boards.  No interrupts and no slave mode: a bus
 * whose only traffic is register pokes does not need either.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_i2c_arch.h"

#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_common.h"
#include "tiku_gpio_arch.h"

/** @brief The channel wired to the expansion connectors. */
#define I2C_CH          1U

/** @brief SCL1 and SDA1 as port<<8 | pin, per the board manual. */
#define I2C_SCL_PORT    5U
#define I2C_SCL_PIN     12U
#define I2C_SDA_PORT    5U
#define I2C_SDA_PIN     11U

/*
 * Bit rate.  IICphi = PCLKB / 2^CKS feeds the counters and BRH/BRL set the
 * high and low periods.  PCLKB is 60 MHz at the boot rung, so CKS = 2 gives a
 * 15 MHz reference and these counts make ~370 kHz fast mode -- what the vendor
 * runs this bus at, and inside the OV5640's 400 kHz SCCB ceiling.  (With CKS
 * left 0 the same counts made 1.6 MHz: the GreenPAK tolerated it, the sensor
 * latched writes but its read path never drove a byte.)  BRH/BRL read back
 * with their top three bits set, so they are written whole.
 */
#define I2C_CKS         2U
#define I2C_BRH_FAST    0xEDU   /* 0xE0 | 13 high counts */
#define I2C_BRL_FAST    0xF4U   /* 0xE0 | 20 low counts  */

/** @brief Bounded spin for every flag wait; a stuck bus must not hang. */
#define I2C_SPINS       200000UL

static uint8_t i2c_up;

/**
 * @brief Whether the last byte went unacknowledged.
 *
 * @return Non-zero when the NACK flag is set
 */
static int
i2c_nacked(void)
{
    return (TIKU_REG8(RA8P1_IIC_SR2(I2C_CH)) & RA8P1_IIC_SR2_NACKF) != 0U;
}

/**
 * @brief Wait for a status flag, bounded.
 *
 * @param mask  Flag to wait for in ICSR2
 * @return Non-zero when it appeared, zero on timeout
 */
static int
i2c_wait(uint8_t mask)
{
    uint32_t spins;

    for (spins = 0U; spins < I2C_SPINS; spins++) {
        uint8_t sr = TIKU_REG8(RA8P1_IIC_SR2(I2C_CH));

        if ((sr & mask) != 0U) {
            return 1;
        }
        if ((sr & RA8P1_IIC_SR2_NACKF) != 0U) {
            return 0;               /* nobody answered; stop waiting */
        }
    }
    return 0;
}

/**
 * @brief Wait for the stop condition to reach the bus, bounded.
 *
 * Unlike i2c_wait(), this does not bail on NACKF: the last received byte is
 * deliberately NACKed, so NACKF is set here as a matter of course, and bailing
 * on it would return before BBSY clears and wedge the next transfer.
 */
static void
i2c_wait_stop(void)
{
    uint32_t spins;

    for (spins = 0U; spins < I2C_SPINS; spins++) {
        if ((TIKU_REG8(RA8P1_IIC_SR2(I2C_CH)) & RA8P1_IIC_SR2_STOP) != 0U) {
            return;
        }
    }
}

/** @brief Release the bus with a stop condition and clear the flags. */
static void
i2c_stop(void)
{
    TIKU_REG8(RA8P1_IIC_SR2(I2C_CH)) &= (uint8_t)~RA8P1_IIC_SR2_STOP;
    TIKU_REG8(RA8P1_IIC_CCR2(I2C_CH)) = (uint8_t)RA8P1_IIC_CCR2_SP;
    i2c_wait_stop();
    TIKU_REG8(RA8P1_IIC_SR2(I2C_CH)) = 0U;
}

/**
 * @brief Put a start condition on the bus and send the addressing byte.
 *
 * @param addr  7-bit device address
 * @param read  Non-zero for a read transfer
 * @return TIKU_I2C_OK, or an error
 */
static int
i2c_address(uint8_t addr, int read)
{
    if ((TIKU_REG8(RA8P1_IIC_CCR2(I2C_CH)) & RA8P1_IIC_CCR2_BBSY) != 0U) {
        return TIKU_I2C_ERR_BUSY;
    }
    TIKU_REG8(RA8P1_IIC_SR2(I2C_CH)) = 0U;
    TIKU_REG8(RA8P1_IIC_CCR2(I2C_CH)) |= (uint8_t)RA8P1_IIC_CCR2_ST;

    if (!i2c_wait(RA8P1_IIC_SR2_TDRE)) {
        i2c_stop();
        return TIKU_I2C_ERR_TIMEOUT;
    }
    TIKU_REG8(RA8P1_IIC_DRT(I2C_CH)) =
        (uint8_t)((addr << 1) | (read ? 1U : 0U));

    /*
     * TEND rises whether or not anyone answered, so the acknowledge has to
     * be read separately -- checking only TEND makes every address on the
     * bus look like a device.
     */
    if (!i2c_wait(RA8P1_IIC_SR2_TEND) || i2c_nacked()) {
        i2c_stop();
        return TIKU_I2C_ERR_NACK;
    }
    return TIKU_I2C_OK;
}

/** @brief Route SCL1/SDA1 to the IIC peripheral, open-drain. */
static void
i2c_pins_iic(void)
{
    TIKU_REG8(RA8P1_PWPR_S) = 0U;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_PFSWE;
    TIKU_REG32(RA8P1_PFS(I2C_SCL_PORT, I2C_SCL_PIN)) =
        (RA8P1_PFS_PSEL_IIC << RA8P1_PFS_PSEL_SHIFT) |
        RA8P1_PFS_PMR | RA8P1_PFS_NCODR;
    TIKU_REG32(RA8P1_PFS(I2C_SDA_PORT, I2C_SDA_PIN)) =
        (RA8P1_PFS_PSEL_IIC << RA8P1_PFS_PSEL_SHIFT) |
        RA8P1_PFS_PMR | RA8P1_PFS_NCODR;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_B0WI;
}

/**
 * @brief Read the SDA line level through the input register.
 *
 * @return Non-zero when SDA is high (released), zero when a slave holds it low
 */
static int
i2c_sda_high(void)
{
    return (int)((TIKU_REG32(RA8P1_PORT_PCNTR2(I2C_SDA_PORT))
                  >> I2C_SDA_PIN) & 1U);
}

/**
 * @brief Clock a slave off the bus and leave the pins as idle IIC.
 *
 * An unfinished read can leave the slave driving SDA low, which latches BBSY
 * and refuses every transfer.  Toggle SCL by hand until the slave releases
 * SDA, issue a manual stop, then hand the pins back to the IIC unit.
 */
static void
i2c_bus_recover(void)
{
    unsigned i;

    /* SDA is left as an input -- released -- so a slave holding it low can let
     * go as SCL is clocked.  Driving SDA high here instead would only fight the
     * slave, and the line would never come back. */
    tiku_ra8p1_gpio_init_output(I2C_SCL_PORT, I2C_SCL_PIN);
    tiku_ra8p1_gpio_init_input(I2C_SDA_PORT, I2C_SDA_PIN);
    tiku_ra8p1_gpio_set(I2C_SCL_PORT, I2C_SCL_PIN, 1);
    tiku_cpu_ra8p1_delay_us(5U);
    for (i = 0U; i < 16U && !i2c_sda_high(); i++) {
        tiku_ra8p1_gpio_set(I2C_SCL_PORT, I2C_SCL_PIN, 0);
        tiku_cpu_ra8p1_delay_us(5U);
        tiku_ra8p1_gpio_set(I2C_SCL_PORT, I2C_SCL_PIN, 1);
        tiku_cpu_ra8p1_delay_us(5U);
    }
    /* Stop condition: pull SDA low with SCL high, then release it high. */
    tiku_ra8p1_gpio_init_output(I2C_SDA_PORT, I2C_SDA_PIN);
    tiku_ra8p1_gpio_set(I2C_SDA_PORT, I2C_SDA_PIN, 0);
    tiku_cpu_ra8p1_delay_us(5U);
    tiku_ra8p1_gpio_set(I2C_SCL_PORT, I2C_SCL_PIN, 1);
    tiku_cpu_ra8p1_delay_us(5U);
    tiku_ra8p1_gpio_set(I2C_SDA_PORT, I2C_SDA_PIN, 1);
    tiku_cpu_ra8p1_delay_us(5U);
    i2c_pins_iic();
}

/**
 * @brief Write the mode/bit-rate registers while the unit is held in reset.
 *
 * Most of these only accept writes with IICRST asserted, so both the boot init
 * and the per-read reset go through here between asserting and releasing reset.
 */
static void
i2c_configure(void)
{
    TIKU_REG8(RA8P1_IIC_MR1(I2C_CH)) = (uint8_t)(0x08U | (I2C_CKS << 4));
    TIKU_REG8(RA8P1_IIC_BRH(I2C_CH)) = I2C_BRH_FAST;
    TIKU_REG8(RA8P1_IIC_BRL(I2C_CH)) = I2C_BRL_FAST;
    TIKU_REG8(RA8P1_IIC_SER(I2C_CH)) = 0U;       /* no slave addresses      */
    TIKU_REG8(RA8P1_IIC_MR2(I2C_CH)) = 0x04U;    /* match FSP: TMOH set     */
    TIKU_REG8(RA8P1_IIC_MR3(I2C_CH)) = 0U;
    /* FSP's 0x77: timeout, master + NACK arbitration-loss, NACK suspension,
     * noise filter, SCL sync. */
    TIKU_REG8(RA8P1_IIC_FER(I2C_CH)) = 0x77U;
    TIKU_REG8(RA8P1_IIC_IER(I2C_CH)) = 0U;       /* polled                  */
}

/**
 * @brief Force the peripheral back to an idle master through an internal reset.
 *
 * This RIIC will not retire the stop ending a WAIT-held receive: SP stays
 * requested and BBSY latched, wedging the next transfer.  Pulsing IICRST
 * after each read clears them, as the vendor driver's NACK path also does.
 */
static void
i2c_reset(void)
{
    TIKU_REG8(RA8P1_IIC_CCR1(I2C_CH)) = (uint8_t)(RA8P1_IIC_CCR1_ICE |
                                                  RA8P1_IIC_CCR1_IICRST);
    i2c_configure();
    TIKU_REG8(RA8P1_IIC_CCR1(I2C_CH)) = (uint8_t)RA8P1_IIC_CCR1_ICE;
}

int
tiku_i2c_arch_init(const tiku_i2c_config_t *config)
{
    (void)config;

    if (i2c_up) {
        return TIKU_I2C_OK;
    }
    TIKU_REG32(RA8P1_MSTPCRB) &= ~RA8P1_MSTPCRB_IIC1;
    (void)TIKU_REG32(RA8P1_MSTPCRB);
    tiku_cpu_ra8p1_delay_us(30U);

    /* Clock any mid-byte slave off the bus and leave the pins as open-drain
     * IIC.  Open drain matters or the pin fights the pull-up and the line
     * never reads low. */
    i2c_bus_recover();

    /* Reset with the unit disabled, configure, then enable and release --
     * the order the manual prescribes for the mode registers. */
    TIKU_REG8(RA8P1_IIC_CCR1(I2C_CH)) = (uint8_t)RA8P1_IIC_CCR1_IICRST;
    TIKU_REG8(RA8P1_IIC_CCR1(I2C_CH)) = (uint8_t)(RA8P1_IIC_CCR1_ICE |
                                                  RA8P1_IIC_CCR1_IICRST);
    i2c_configure();
    TIKU_REG8(RA8P1_IIC_CCR1(I2C_CH)) = (uint8_t)RA8P1_IIC_CCR1_ICE;

    i2c_up = 1U;
    return TIKU_I2C_OK;
}

void
tiku_i2c_arch_close(void)
{
    if (!i2c_up) {
        return;
    }
    TIKU_REG8(RA8P1_IIC_CCR1(I2C_CH)) = 0U;
    i2c_up = 0U;
}

int
tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    int rc;

    if (!i2c_up || buf == 0) {
        return TIKU_I2C_ERR_PARAM;
    }
    rc = i2c_address(addr, 0);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }
    for (i = 0U; i < len; i++) {
        TIKU_REG8(RA8P1_IIC_DRT(I2C_CH)) = buf[i];
        if (!i2c_wait(RA8P1_IIC_SR2_TEND) || i2c_nacked()) {
            i2c_stop();
            return TIKU_I2C_ERR_NACK;
        }
    }
    i2c_stop();
    return TIKU_I2C_OK;
}

int
tiku_i2c_arch_probe(uint8_t addr)
{
    int rc;

    if (!i2c_up) {
        return TIKU_I2C_ERR_PARAM;
    }
    /* Address and let go: whether the device answered is the whole result,
     * so nothing is transferred and a NACK is the negative answer rather
     * than a failure. */
    rc = i2c_address(addr, 0);
    if (rc == TIKU_I2C_OK) {
        i2c_stop();
    }
    return rc;
}

/**
 * @brief Clock in @p len bytes after the read address, per UM 40.3.4.
 *
 * The dummy read of ICDRR starts the data clock.  WAIT holds the last byte at
 * its ninth clock so the acknowledge can be forced to NACK and the stop armed
 * before the read that releases it; a single byte needs both set up front.
 *
 * @param buf  Destination
 * @param len  Byte count, at least one; RDRF for the dummy must already be up
 * @return TIKU_I2C_OK, or a timeout
 */
static int
i2c_receive(uint8_t *buf, uint16_t len)
{
    uint16_t i;

    /*
     * Receive per UM 40.3.4, Figure 40.10, with RDRFS = 0.  The read address
     * left RDRF up with a dummy in ICDRR; the dummy read is what starts the
     * SCL clock for the real data.  WAIT stalls the master at the ninth clock
     * of the final byte so its acknowledge can be forced to NACK and the stop
     * armed while the byte is still held -- the read that follows releases it.
     *
     * A single byte is special: it is itself the last byte, so both WAIT and
     * the NACK must be set before the dummy read clocks it in.
     */
    TIKU_REG8(RA8P1_IIC_MR3(I2C_CH)) |= (uint8_t)RA8P1_IIC_MR3_WAIT;
    if (len == 1U) {
        TIKU_REG8(RA8P1_IIC_MR3(I2C_CH)) |=
            (uint8_t)(RA8P1_IIC_MR3_ACKWP | RA8P1_IIC_MR3_ACKBT);
    }
    (void)TIKU_REG8(RA8P1_IIC_DRR(I2C_CH));   /* dummy read starts the data */

    for (i = 0U; i < len; i++) {
        if (!i2c_wait(RA8P1_IIC_SR2_RDRF)) {
            i2c_stop();
            return TIKU_I2C_ERR_TIMEOUT;
        }
        /* Arm the NACK after reading the second-to-last byte, so it applies
         * to the final byte's ninth clock. */
        if (len > 1U && i == (uint16_t)(len - 2U)) {
            TIKU_REG8(RA8P1_IIC_MR3(I2C_CH)) |=
                (uint8_t)(RA8P1_IIC_MR3_ACKWP | RA8P1_IIC_MR3_ACKBT);
        }
        if (i == (uint16_t)(len - 1U)) {
            /* Arm the stop BEFORE reading the last byte: on this double-buffered
             * IP the read that follows either issues the stop (SP pending) or
             * clocks a further byte the slave then drives (SDA stuck low).  The
             * SP bit is written directly (= SP) so MST/stale RS go to 0. */
            TIKU_REG8(RA8P1_IIC_SR2(I2C_CH)) &= (uint8_t)~RA8P1_IIC_SR2_STOP;
            TIKU_REG8(RA8P1_IIC_CCR2(I2C_CH)) = (uint8_t)RA8P1_IIC_CCR2_SP;
        }
        buf[i] = TIKU_REG8(RA8P1_IIC_DRR(I2C_CH));
    }
    /* Reading the last byte released the WAIT low-hold, letting the armed stop
     * reach the bus. */
    TIKU_REG8(RA8P1_IIC_MR3(I2C_CH)) &=
        (uint8_t)~(RA8P1_IIC_MR3_WAIT | RA8P1_IIC_MR3_ACKBT);
    i2c_wait_stop();
    TIKU_REG8(RA8P1_IIC_SR2(I2C_CH)) = 0U;
    /*
     * This RIIC will not retire the stop that ends a WAIT-held receive: SP
     * stays requested and, worse, the slave is often left mid-byte holding SDA
     * low.  An internal reset clears the peripheral's own latched state, and if
     * the slave is still holding the line, a bit-banged recovery clocks it off
     * and re-establishes an idle bus for the next transfer.
     */
    i2c_reset();
    if (!i2c_sda_high()) {
        i2c_bus_recover();
        i2c_reset();
    }
    return TIKU_I2C_OK;
}

int
tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    uint32_t spins;

    if (!i2c_up || buf == 0 || len == 0U) {
        return TIKU_I2C_ERR_PARAM;
    }
    if ((TIKU_REG8(RA8P1_IIC_CCR2(I2C_CH)) & RA8P1_IIC_CCR2_BBSY) != 0U) {
        return TIKU_I2C_ERR_BUSY;
    }

    /*
     * Master reception (UM 40.3.4).  The addressing byte is a transmit, so it
     * still waits on TDRE; but after a READ address the interface flips to
     * receive and it is RDRF, not TEND, that reports the byte -- which is why
     * a shared "wait for TEND" addressing helper cannot serve reads.
     */
    TIKU_REG8(RA8P1_IIC_SR2(I2C_CH)) = 0U;
    TIKU_REG8(RA8P1_IIC_CCR2(I2C_CH)) |= (uint8_t)RA8P1_IIC_CCR2_ST;

    if (!i2c_wait(RA8P1_IIC_SR2_TDRE)) {
        i2c_stop();
        return TIKU_I2C_ERR_TIMEOUT;
    }
    TIKU_REG8(RA8P1_IIC_DRT(I2C_CH)) = (uint8_t)((addr << 1) | 1U);

    /* Wait for the addressing to land as a received byte becoming available,
     * or a NACK if nobody answered. */
    for (spins = 0U; spins < I2C_SPINS; spins++) {
        uint8_t sr = TIKU_REG8(RA8P1_IIC_SR2(I2C_CH));

        if ((sr & RA8P1_IIC_SR2_NACKF) != 0U) {
            i2c_stop();
            return TIKU_I2C_ERR_NACK;
        }
        if ((sr & RA8P1_IIC_SR2_RDRF) != 0U) {
            break;
        }
    }
    if (spins == I2C_SPINS) {
        i2c_stop();
        return TIKU_I2C_ERR_TIMEOUT;
    }

    return i2c_receive(buf, len);
}
int
tiku_i2c_arch_write_read(uint8_t addr, const uint8_t *tx_buf, uint16_t tx_len,
                         uint8_t *rx_buf, uint16_t rx_len)
{
    uint16_t i;
    int rc;

    if (!i2c_up || tx_buf == 0 || rx_buf == 0 || rx_len == 0U) {
        return TIKU_I2C_ERR_PARAM;
    }
    rc = i2c_address(addr, 0);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }
    for (i = 0U; i < tx_len; i++) {
        TIKU_REG8(RA8P1_IIC_DRT(I2C_CH)) = tx_buf[i];
        if (!i2c_wait(RA8P1_IIC_SR2_TEND) || i2c_nacked()) {
            i2c_stop();
            return TIKU_I2C_ERR_NACK;
        }
    }
    /*
     * Restart rather than stop: releasing the bus between the register
     * address and the read lets another master in, and most devices reset
     * their pointer on a stop.  The register-byte transmit left TDRE set, so
     * waiting on TDRE here would pass on the STALE flag and load the read
     * address before the restart is even on the bus -- wait for the START
     * condition to be detected instead, which only rises once it is.
     */
    TIKU_REG8(RA8P1_IIC_SR2(I2C_CH)) &=
        (uint8_t)~(RA8P1_IIC_SR2_START | RA8P1_IIC_SR2_STOP);
    TIKU_REG8(RA8P1_IIC_CCR2(I2C_CH)) |= (uint8_t)RA8P1_IIC_CCR2_RS;
    if (!i2c_wait(RA8P1_IIC_SR2_START)) {
        i2c_stop();
        return TIKU_I2C_ERR_TIMEOUT;
    }
    TIKU_REG8(RA8P1_IIC_SR2(I2C_CH)) &= (uint8_t)~RA8P1_IIC_SR2_START;
    TIKU_REG8(RA8P1_IIC_DRT(I2C_CH)) = (uint8_t)((addr << 1) | 1U);
    /*
     * The read address is a transmit but flips the interface to receive, so
     * RDRF -- not TEND -- reports its completion.  Waiting on TEND here would
     * time out and read as a NACK on a device that answered fine.
     */
    {
        uint32_t spins;

        for (spins = 0U; spins < I2C_SPINS; spins++) {
            uint8_t sr = TIKU_REG8(RA8P1_IIC_SR2(I2C_CH));

            if ((sr & RA8P1_IIC_SR2_NACKF) != 0U) {
                i2c_stop();
                return TIKU_I2C_ERR_NACK;
            }
            if ((sr & RA8P1_IIC_SR2_RDRF) != 0U) {
                break;
            }
        }
        if (spins == I2C_SPINS) {
            i2c_stop();
            return TIKU_I2C_ERR_TIMEOUT;
        }
    }

    return i2c_receive(rx_buf, rx_len);
}
