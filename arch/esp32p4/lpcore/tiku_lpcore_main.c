/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lpcore_main.c - P4 LP core payload: park, echo, LED, I2C, HP wake,
 * and the LP agent's jobs.
 *
 * Serves the shared page in tiku_lpcore_ipc.h by polling.  The HP core starts
 * it again after a fault by resetting the core, so a trap only records.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_lpcore_ipc.h"
#include "tiku_lpagent_jobs.h"
#include "tiku_lpcore_i2c.h"

void tiku_lpcore_main(void);
void tiku_lpcore_fault(void);

/** @brief Busy iterations between passes: about 5 ms a pass. */
#define LPCORE_PACE 2000u

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* LP GPIO, LP IOMUX and LP timer (lp_gpio_reg.h, lp_iomux_reg.h and
 * lp_timer_reg.h at the ESP-IDF pin). */
#define LP_GPIO_OUT_W1TS   0x5012A00Cu
#define LP_GPIO_OUT_W1TC   0x5012A010u
#define LP_GPIO_EN_W1TS    0x5012A018u
#define LP_GPIO_EN_W1TC    0x5012A01Cu
#define LP_GPIO_IN         0x5012A030u
#define LP_GPIO_PIN(n)     (0x5012A034u + 4u * (n))
#define LP_GPIO_OUT_SEL(n) (0x5012A0F4u + 4u * (n))
#define LP_IOMUX_PAD(n)    (0x5012B008u + 4u * (n))
#define PAD_RUE            (1u << 3)
#define PAD_MUX_LP         (1u << 4)    /* the pad follows the LP IO module */
#define PAD_FUN_SEL        (3u << 5)
#define PAD_FUN_GPIO       (1u << 5)    /* function 1: LP GPIO */
#define PAD_FUN_IE         (1u << 10)
#define PIN_OPEN_DRAIN     (1u << 4)
#define OUT_SEL_MASK       ((0x3Fu << 3) | (1u << 1))
#define OUT_SEL_GPIO       ((32u << 3) | (1u << 1)) /* GPIO_OUT, OE from
                                                       LP_GPIO_ENABLE */
#define LP_TIMER_UPDATE    0x50112010u
#define LP_TIMER_SNAPSHOT  (1u << 28)
#define LP_TIMER_BUF0_LO   0x50112014u
#define LP_TIMER_BUF0_HI   0x50112018u
#define PMU_HP_LP_COMM     0x5011519Cu
#define LP_TRIGGER_HP      (1u << 30)

/** @brief Bus delay iterations per I2C half bit. */
#define I2C_HALF  40u

/* LP ADC, ADC1's one-shot register (adc_ll.h): result 15:0, done 16, start
 * 17, start force 18, pad map 30:19, pad force 31.  The HP powers the unit
 * and sets its code and attenuations while ADC jobs exist. */
#define ADC1_ONESHOT       0x5012700Cu
#define ADC_DONE           (1u << 16)
#define ADC_START          (1u << 17)
#define ADC_START_FORCE    (1u << 18)
#define ADC_PAD_POS        19u
#define ADC_PAD_FORCE      (1u << 31)
#define ADC_SPINS          20000u

/** @brief LED mode set by TIKU_LPCORE_CMD_LED, and its blink state. */
static uint32_t led_mode, led_period, led_count;

/** @brief The armed wake's operands: the start and length in LP ticks, or
 *         the LP IO and its level. */
static uint32_t wake_start, wake_arg, wake_level;

/**
 * @brief Hand LP IO @p n to the LP GPIO module, input enabled and driven by
 *        the plain output signal, push-pull or open drain.
 */
static void lp_io_claim(uint32_t n, int open_drain, int pull_up)
{
    uint32_t pad = REG(LP_IOMUX_PAD(n)) & ~(PAD_FUN_SEL | PAD_RUE);

    REG(LP_IOMUX_PAD(n)) = pad | PAD_MUX_LP | PAD_FUN_GPIO | PAD_FUN_IE |
                           (pull_up ? PAD_RUE : 0u);
    REG(LP_GPIO_OUT_SEL(n)) = (REG(LP_GPIO_OUT_SEL(n)) & ~OUT_SEL_MASK) |
                              OUT_SEL_GPIO;
    REG(LP_GPIO_PIN(n)) = open_drain ? (REG(LP_GPIO_PIN(n)) | PIN_OPEN_DRAIN)
                                     : (REG(LP_GPIO_PIN(n)) & ~PIN_OPEN_DRAIN);
}

/** @brief Drive LP IO @p n high or low; open drain releases it for high. */
static void lp_io_set(uint32_t n, uint32_t high)
{
    REG(high ? LP_GPIO_OUT_W1TS : LP_GPIO_OUT_W1TC) = 1u << n;
}

/** @brief The pad level of LP IO @p n. */
static uint32_t lp_io_get(uint32_t n)
{
    return (REG(LP_GPIO_IN) >> n) & 1u;
}

/**
 * @brief Give the LED pad back to the HP IO MUX with the LP output off.
 *
 * LP GPIO and LP IOMUX keep their state across an HP reset and a relaunch,
 * so a launch starts from here and TIKU_LPCORE_CMD_LED claims the pad again.
 */
static void led_release(tiku_lpcore_shared_t *sh)
{
    REG(LP_GPIO_EN_W1TC) = 1u << TIKU_LPCORE_LED_IO;
    REG(LP_IOMUX_PAD(TIKU_LPCORE_LED_IO)) &= ~PAD_MUX_LP;
    sh->led_level = 0u;
}

uint32_t tiku_lpcore_io_get(uint32_t n)
{
    return lp_io_get(n);
}

void tiku_lpcore_io_set(uint32_t n, uint32_t high)
{
    lp_io_set(n, high);
}

void tiku_lpcore_i2c_delay(void)
{
    volatile uint32_t i;
    for (i = 0u; i < I2C_HALF; i++) {
    }
}

int32_t tiku_lpcore_adc_get(uint32_t pad)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    int32_t value = -1;
    uint32_t i;

    sh->sar_lp_busy = 1u;
    __asm__ volatile ("fence" ::: "memory");
    if (sh->sar_hp_req) {
        sh->sar_lp_busy = 0u;
        return TIKU_LPAGENT_NO_READING;
    }
    REG(ADC1_ONESHOT) = (REG(ADC1_ONESHOT) &
                         ~((0xFFFu << ADC_PAD_POS) | ADC_START |
                           ADC_START_FORCE | ADC_PAD_FORCE)) |
                        (1u << (ADC_PAD_POS + pad)) | ADC_START_FORCE |
                        ADC_PAD_FORCE;
    REG(ADC1_ONESHOT) |= ADC_START;
    for (i = 0u; i < ADC_SPINS; i++) {
        if (REG(ADC1_ONESHOT) & ADC_DONE) {
            value = (int32_t)(REG(ADC1_ONESHOT) & 0xFFFu);
            break;
        }
    }
    REG(ADC1_ONESHOT) &= ~ADC_START;
    __asm__ volatile ("fence" ::: "memory");
    sh->sar_lp_busy = 0u;
    return value;
}

void tiku_lpcore_wake_hp(void)
{
    REG(PMU_HP_LP_COMM) = LP_TRIGGER_HP;
}

/** @brief Set the LED pad to @p level, output enabled. */
static void led_drive(tiku_lpcore_shared_t *sh, uint32_t level)
{
    lp_io_set(TIKU_LPCORE_LED_IO, level);
    sh->led_level = level;
}

/** @brief Take TIKU_LPCORE_CMD_LED: arg[0] 0 low, 1 high, 2 blink every
 *         arg[1] passes. */
static uint32_t led_command(tiku_lpcore_shared_t *sh)
{
    uint32_t mode = sh->arg[0];

    if (mode > 2u || (mode == 2u && sh->arg[1] == 0u)) {
        return TIKU_LPCORE_RSP_BAD_ARG;
    }
    lp_io_claim(TIKU_LPCORE_LED_IO, 0, 0);
    REG(LP_GPIO_EN_W1TS) = 1u << TIKU_LPCORE_LED_IO;
    led_mode = mode;
    led_period = sh->arg[1];
    led_count = 0u;
    led_drive(sh, mode == 1u ? 1u : 0u);
    return TIKU_LPCORE_RSP_DONE;
}

/** @brief LP IO pad settings an I2C transaction changes. */
typedef struct {
    uint32_t pad[2], pin[2], sel[2];
} i2c_pads_t;

/** @brief Take LP IOs @p sda and @p scl as open-drain lines with their
 *         pull-ups, saving what they were in @p saved. */
static void i2c_bus_take(uint32_t sda, uint32_t scl, i2c_pads_t *saved)
{
    saved->pad[0] = REG(LP_IOMUX_PAD(sda));
    saved->pad[1] = REG(LP_IOMUX_PAD(scl));
    saved->pin[0] = REG(LP_GPIO_PIN(sda));
    saved->pin[1] = REG(LP_GPIO_PIN(scl));
    saved->sel[0] = REG(LP_GPIO_OUT_SEL(sda));
    saved->sel[1] = REG(LP_GPIO_OUT_SEL(scl));
    lp_io_set(sda, 1u);
    lp_io_set(scl, 1u);
    lp_io_claim(sda, 1, 1);
    lp_io_claim(scl, 1, 1);
    REG(LP_GPIO_EN_W1TS) = (1u << sda) | (1u << scl);
}

/** @brief Give LP IOs @p sda and @p scl back as @p saved holds them. */
static void i2c_bus_give(uint32_t sda, uint32_t scl, const i2c_pads_t *saved)
{
    REG(LP_GPIO_EN_W1TC) = (1u << sda) | (1u << scl);
    REG(LP_GPIO_OUT_SEL(sda)) = saved->sel[0];
    REG(LP_GPIO_OUT_SEL(scl)) = saved->sel[1];
    REG(LP_GPIO_PIN(sda)) = saved->pin[0];
    REG(LP_GPIO_PIN(scl)) = saved->pin[1];
    REG(LP_IOMUX_PAD(sda)) = saved->pad[0];
    REG(LP_IOMUX_PAD(scl)) = saved->pad[1];
}

int32_t tiku_lpcore_i2c_get(uint32_t addr, uint32_t arg)
{
    uint32_t sda = (arg >> 16) & 15u, scl = (arg >> 24) & 15u;
    i2c_pads_t saved;
    int32_t value;

    if (sda == scl || sda == TIKU_LPCORE_LED_IO || scl == TIKU_LPCORE_LED_IO) {
        return -1;
    }
    i2c_bus_take(sda, scl, &saved);
    value = tiku_lpcore_i2c_read(sda, scl, addr, arg & 0xFFu,
                                 ((arg >> 8) & 0xFFu) == 2u ? 2u : 1u);
    i2c_bus_give(sda, scl, &saved);
    return value;
}

/**
 * @brief Take TIKU_LPCORE_CMD_I2C_SCAN: probe 0x08..0x77 on SDA arg[0] and
 *        SCL arg[1] with the internal pull-ups, then give both pads back.
 */
static uint32_t i2c_scan_command(tiku_lpcore_shared_t *sh)
{
    uint32_t sda = sh->arg[0], scl = sh->arg[1], addr, i;
    i2c_pads_t saved;
    int r = 0;

    if (sda >= TIKU_LPCORE_LP_IO_COUNT || scl >= TIKU_LPCORE_LP_IO_COUNT ||
        sda == scl || sda == TIKU_LPCORE_LED_IO || scl == TIKU_LPCORE_LED_IO) {
        return TIKU_LPCORE_RSP_BAD_ARG;
    }
    for (i = 0u; i < sizeof(sh->i2c_found); i++) {
        sh->i2c_found[i] = 0u;
    }
    sh->i2c_count = 0u;
    sh->i2c_scl_stuck = 0u;
    i2c_bus_take(sda, scl, &saved);
    for (addr = 0x08u; addr <= 0x77u; addr++) {
        r = tiku_lpcore_i2c_probe(sda, scl, addr);
        if (r < 0) {
            sh->i2c_scl_stuck = 1u;
            break;
        }
        if (r) {
            sh->i2c_found[addr >> 3] =
                (uint8_t)(sh->i2c_found[addr >> 3] | (1u << (addr & 7u)));
            sh->i2c_count = sh->i2c_count + 1u;
        }
    }
    i2c_bus_give(sda, scl, &saved);
    return TIKU_LPCORE_RSP_DONE;
}

/**
 * @brief Take TIKU_LPCORE_CMD_WAKE: arm a one-shot HP wake on LP timer
 *        ticks or on an LP IO's level.
 *
 * An LP IO other than the LED's is claimed as an input with its pull-up;
 * the LED's is read as the payload drives it.
 */
static uint32_t wake_command(tiku_lpcore_shared_t *sh)
{
    uint32_t mode = sh->arg[0];

    if (mode == TIKU_LPCORE_WAKE_TICKS && sh->arg[1] != 0u) {
        wake_start = sh->lp_ticks_lo;
    } else if (mode == TIKU_LPCORE_WAKE_IO &&
               sh->arg[1] < TIKU_LPCORE_LP_IO_COUNT && sh->arg[2] <= 1u) {
        if (sh->arg[1] != TIKU_LPCORE_LED_IO) {
            REG(LP_GPIO_EN_W1TC) = 1u << sh->arg[1];
            lp_io_claim(sh->arg[1], 0, 1);
        }
        wake_level = sh->arg[2];
    } else {
        return TIKU_LPCORE_RSP_BAD_ARG;
    }
    wake_arg = sh->arg[1];
    sh->wake_mode = mode;
    return TIKU_LPCORE_RSP_DONE;
}

/** @brief Send the armed wake once its condition holds. */
static void wake_check(tiku_lpcore_shared_t *sh)
{
    uint32_t mode = sh->wake_mode;

    if ((mode == TIKU_LPCORE_WAKE_TICKS &&
         sh->lp_ticks_lo - wake_start >= wake_arg) ||
        (mode == TIKU_LPCORE_WAKE_IO && lp_io_get(wake_arg) == wake_level)) {
        sh->wake_mode = 0u;
        sh->wake_lp_ticks = sh->lp_ticks_lo;
        sh->wake_count = sh->wake_count + 1u;
        tiku_lpcore_wake_hp();
    }
}

/** @brief Publish the cycle counter and the LP timer's latched count. */
static void publish_counters(tiku_lpcore_shared_t *sh)
{
    uint32_t c;

    __asm__ volatile ("csrr %0, mcycle" : "=r"(c));
    sh->cycles = c;
    REG(LP_TIMER_UPDATE) = LP_TIMER_SNAPSHOT;
    sh->lp_ticks_lo = REG(LP_TIMER_BUF0_LO);
    sh->lp_ticks_hi = REG(LP_TIMER_BUF0_HI) & 0xFFFFu;
}

/**
 * @brief Echo a new HP-to-LP message back.
 *
 * The four-byte message FLT! executes an illegal instruction instead, so a
 * test can fault the payload.
 */
static void lpcore_echo(tiku_lpcore_shared_t *sh, uint32_t *last_seq)
{
    uint32_t seq = sh->h2l_seq;
    uint32_t len, i;

    if (seq == *last_seq) {
        return;
    }
    *last_seq = seq;
    /* The HP core writes buf and len before seq; the fence keeps these reads
     * after the seq read, so len belongs to this message. */
    __asm__ volatile ("fence" ::: "memory");
    len = sh->h2l_len;
    if (len > TIKU_LPCORE_MSG_CAP) {
        len = TIKU_LPCORE_MSG_CAP;
    }
    if (len == 4u &&
        sh->h2l_buf[0] == (uint8_t)'F' && sh->h2l_buf[1] == (uint8_t)'L' &&
        sh->h2l_buf[2] == (uint8_t)'T' && sh->h2l_buf[3] == (uint8_t)'!') {
        __asm__ volatile ("unimp");
    }
    for (i = 0u; i < len; i++) {
        sh->l2h_buf[i] = sh->h2l_buf[i];
    }
    sh->l2h_len = len;
    __asm__ volatile ("fence" ::: "memory");
    sh->l2h_seq = sh->l2h_seq + 1u;
}

/**
 * @brief Payload entry from crt0: count the boot, publish the magic, then
 *        serve the command word and the mailbox.
 */
void tiku_lpcore_main(void)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    /* Starts at the mailbox's current sequence: after a relaunch the FLT!
     * message is still in the slot, and taking it again would fault again. */
    uint32_t last_seq = sh->h2l_seq;
    volatile uint32_t pace;

    led_release(sh);
    sh->wake_mode = 0u;
    sh->agent_armed = 0u;
    sh->sar_lp_busy = 0u;
    sh->boots = sh->boots + 1u;
    sh->heartbeat = 0u;
    __asm__ volatile ("fence" ::: "memory");
    sh->magic = TIKU_LPCORE_MAGIC;

    for (;;) {
        /* The parked wait polls: no interrupt is enabled on this core. */
        if (sh->cmd == TIKU_LPCORE_CMD_PARK) {
            sh->rsp = TIKU_LPCORE_RSP_PARKED;
            while (sh->cmd != TIKU_LPCORE_CMD_RESUME) {
            }
            sh->cmd = 0u;
            sh->rsp = 0u;
        }
        if (sh->cmd == TIKU_LPCORE_CMD_LED ||
            sh->cmd == TIKU_LPCORE_CMD_I2C_SCAN ||
            sh->cmd == TIKU_LPCORE_CMD_WAKE) {
            uint32_t rsp = sh->cmd == TIKU_LPCORE_CMD_LED ? led_command(sh)
                         : sh->cmd == TIKU_LPCORE_CMD_WAKE ? wake_command(sh)
                         : i2c_scan_command(sh);
            sh->rsp = rsp;
            __asm__ volatile ("fence" ::: "memory");
            sh->cmd = 0u;
        }
        if (led_mode == 2u && ++led_count >= led_period) {
            led_count = 0u;
            led_drive(sh, sh->led_level ^ 1u);
        }
        lpcore_echo(sh, &last_seq);
        publish_counters(sh);
        wake_check(sh);
        tiku_lpagent_run(sh, sh->lp_ticks_lo);
        sh->heartbeat = sh->heartbeat + 1u;
        for (pace = 0u; pace < LPCORE_PACE; pace++) {
        }
    }
}

/**
 * @brief Trap landing: record mcause, mepc and mtval, set the fault magic,
 *        and spin until the HP core resets this core.
 */
__attribute__((used, noreturn))
void tiku_lpcore_fault(void)
{
    tiku_lpcore_shared_t *sh = TIKU_LPCORE_SHARED;
    uint32_t cause, epc, tval;

    __asm__ volatile ("csrr %0, mcause" : "=r"(cause));
    __asm__ volatile ("csrr %0, mepc"   : "=r"(epc));
    __asm__ volatile ("csrr %0, mtval"  : "=r"(tval));
    sh->fault_cause = cause;
    sh->fault_epc   = epc;
    sh->fault_tval  = tval;
    sh->fault_count = sh->fault_count + 1u;
    __asm__ volatile ("fence" ::: "memory");
    sh->magic = TIKU_LPCORE_MAGIC_FAULT;
    for (;;) {
    }
}
