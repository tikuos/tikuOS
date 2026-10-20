/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pwm_arch.c - ESP32-P4 PWM on the LED PWM controller (LEDC).
 *
 * Register fields and the order of writes follow ESP-IDF 4d59230
 * ledc_ll.h, ledc_reg.h, hp_sys_clkrst_reg.h (hw_ver1) and ledc.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <tiku.h>
#include "tiku_pwm_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"
#include <interfaces/gpio/tiku_gpio_owner.h>

#define LEDC_BASE         0x500D3000u
#define CH_CONF0(c)       (LEDC_BASE + 0x14u * (c))
#define CH_HPOINT(c)      (LEDC_BASE + 0x14u * (c) + 0x04u)
#define CH_DUTY(c)        (LEDC_BASE + 0x14u * (c) + 0x08u)
#define CH_CONF1(c)       (LEDC_BASE + 0x14u * (c) + 0x0Cu)
#define TIMER_CONF(t)     (LEDC_BASE + 0xA0u + 8u * (t))
#define GAMMA_CONF(c)     (LEDC_BASE + 0x100u + 4u * (c))
#define GAMMA_RANGE(c, r) (LEDC_BASE + 0x400u + 0x40u * (c) + 4u * (r))
#define CONF0_TIMER       3u
#define CONF0_SIG_OUT_EN  (1u << 2)
#define CONF0_IDLE_LV     (1u << 3)
#define CONF0_PARA_UP     (1u << 4)
#define CONF1_DUTY_START  (1u << 31)
#define DUTY_MASK         0x1FFFFFFu    /* 4 fractional bits */
#define TIMER_RES         0x1Fu
#define TIMER_DIV_POS     5
#define TIMER_PAUSE       (1u << 23)
#define TIMER_RST         (1u << 24)
#define TIMER_PARA_UP     (1u << 26)
/* The fade range ledc_duty_config() writes for a plain duty: one step of
 * one cycle at scale 0. */
#define GAMMA_HOLD        ((1u << 21) | (1u << 1) | 1u)
#define GAMMA_RANGES      16u
/* HP_SYS_CLKRST: the register clock, the function clock with its source,
 * and the reset. */
#define CLK_APB           0x500E6020u   /* SOC_CLK_CTRL3, bit 0 */
#define CLK_FUNC          0x500E609Cu   /* PERI_CLK_CTRL22, bits 2:0 */
#define RESET_PERI        0x500E60C4u   /* HP_RST_EN1, bit 29 */
#define CLK_APB_LEDC      (1u << 0)
#define CLK_FUNC_MASK     7u
#define CLK_FUNC_XTAL     (1u << 2)     /* source 0, the crystal; gate on */
#define RESET_LEDC        (1u << 29)
#define XTAL_HZ           40000000u
#define RES_MAX           16u
#define RES_MIN           8u
#define CHANNELS          8u
#define TIMERS            4u
#define SIG_LEDC          126u          /* LEDC_LS_SIG_OUT_PAD_OUT0_IDX */
#define PIN_MUX(n)        (0x500E1004u + 4u * (n))
#define PIN_CONFIG(n)     (0x500E0074u + 4u * (n))
#define PIN_ROUTE(n)      (0x500E0558u + 4u * (n))
#define PIN_ENABLE(n)     ((n) < 32u ? 0x500E0020u : 0x500E002Cu)
#define PIN_BIT(n)        (1u << ((n) % 32u))
#define PINS              55u
#define LP_PADS           16u
#define LP_IOMUX_PAD(n)   (0x5012B008u + 4u * (n))
#define LP_PAD_MUX_LP     (1u << 4)

/** @brief A channel in use: its pad, its timer and the pad as it was. */
typedef struct {
    uint8_t  used;
    uint8_t  pin;
    uint8_t  timer;
    uint32_t mux, route, config, enable;
} pwm_channel_t;

static pwm_channel_t chans[CHANNELS];
static uint32_t timer_div[TIMERS];
static uint8_t timer_res[TIMERS], timer_users[TIMERS];
static uint8_t open_channels;
static uint32_t saved_apb, saved_func, saved_reset;
static const char owner[] = "pwm";

/** @brief Set @p bits of register @p reg to their values in @p value. */
static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

/** @brief The channel driving @p pin, or -1. */
static int find(unsigned pin)
{
    unsigned c;

    for (c = 0; c < CHANNELS; c++) {
        if (chans[c].used && chans[c].pin == pin) {
            return (int)c;
        }
    }
    return -1;
}

/**
 * @brief The divider, 10.8 fixed point, for @p freq_hz from the crystal at
 *        the most bits (at most 16) that keep it at or above 1.0.
 *
 * At 1 Hz and 16 bits it is 610.35, inside the 18-bit field.
 *
 * @return The divider with *@p res set, or 0 for a frequency needing fewer
 *         than RES_MIN bits
 */
static uint32_t timing(uint32_t freq_hz, unsigned *res)
{
    unsigned r;

    for (r = RES_MAX; r >= RES_MIN; r--) {
        uint64_t den = (uint64_t)freq_hz << r;
        if (den <= XTAL_HZ) {
            uint64_t div = (((uint64_t)XTAL_HZ << 8) + den / 2u) / den;
            *res = r;
            return (uint32_t)div;
        }
    }
    return 0u;
}

/** @brief Clock the controller from the crystal and reset it. */
static void ledc_open(void)
{
    unsigned t;

    saved_apb = TIKU_P4_REG_READ(CLK_APB) & CLK_APB_LEDC;
    saved_func = TIKU_P4_REG_READ(CLK_FUNC) & CLK_FUNC_MASK;
    saved_reset = TIKU_P4_REG_READ(RESET_PERI) & RESET_LEDC;
    put_bits(CLK_APB, CLK_APB_LEDC, CLK_APB_LEDC);
    put_bits(RESET_PERI, RESET_LEDC, RESET_LEDC);
    put_bits(RESET_PERI, RESET_LEDC, 0u);
    /* The fade ranges are written through the function clock. */
    put_bits(CLK_FUNC, CLK_FUNC_MASK, CLK_FUNC_XTAL);
    for (t = 0; t < TIMERS; t++) {
        timer_users[t] = 0u;
    }
}

/** @brief Reset the controller and put its clocks back as they were. */
static void ledc_close(void)
{
    put_bits(RESET_PERI, RESET_LEDC, RESET_LEDC);
    put_bits(CLK_FUNC, CLK_FUNC_MASK, saved_func);
    put_bits(CLK_APB, CLK_APB_LEDC, saved_apb);
    put_bits(RESET_PERI, RESET_LEDC, saved_reset);
}

/** @brief Drop one user of timer @p t, pausing it when none is left. */
static void timer_drop(unsigned t)
{
    if (timer_users[t] != 0u && --timer_users[t] == 0u) {
        put_bits(TIMER_CONF(t), TIMER_PAUSE | TIMER_RST,
                 TIMER_PAUSE | TIMER_RST);
    }
}

/**
 * @brief A timer at @p div and @p res for one more user: one already there,
 *        a free one, or @p own when its caller is its only user.
 *
 * @return The timer, or -1 when all four run at other rates
 */
static int timer_take(uint32_t div, unsigned res, int own)
{
    unsigned t;

    for (t = 0; t < TIMERS; t++) {
        if (timer_users[t] != 0u && timer_div[t] == div &&
            timer_res[t] == res) {
            if ((int)t != own) {
                timer_users[t]++;
            }
            return (int)t;
        }
    }
    for (t = 0; t < TIMERS; t++) {
        if (timer_users[t] == 0u || ((int)t == own && timer_users[t] == 1u)) {
            break;
        }
    }
    if (t == TIMERS) {
        return -1;
    }
    if ((int)t != own) {
        timer_users[t]++;
    }
    timer_div[t] = div;
    timer_res[t] = (uint8_t)res;
    TIKU_P4_REG_WRITE(TIMER_CONF(t), res | (div << TIMER_DIV_POS));
    put_bits(TIMER_CONF(t), TIMER_PARA_UP, TIMER_PARA_UP);
    put_bits(TIMER_CONF(t), TIMER_RST, TIMER_RST);
    put_bits(TIMER_CONF(t), TIMER_RST, 0u);
    return (int)t;
}

/** @brief Load the duty of channel @p c and apply it from the next period. */
static void duty_load(unsigned c, uint16_t duty_u16)
{
    uint32_t d = (uint32_t)duty_u16 >> (RES_MAX - timer_res[chans[c].timer]);

    TIKU_P4_REG_WRITE(CH_DUTY(c), d << 4);
    TIKU_P4_REG_WRITE(GAMMA_RANGE(c, 0u), GAMMA_HOLD);
    TIKU_P4_REG_WRITE(GAMMA_CONF(c), 1u);
    put_bits(CH_CONF1(c), CONF1_DUTY_START, CONF1_DUTY_START);
    put_bits(CH_CONF0(c), CONF0_PARA_UP, CONF0_PARA_UP);
}

/** @brief Whether @p pin is the board LED, which the board reserves. */
static int led_pin(unsigned pin)
{
#if defined(TIKU_BOARD_LED_PIN)
    return pin == TIKU_BOARD_LED_PIN;
#else
    (void)pin;
    return 0;
#endif
}

/** @brief Claim @p pin; the LED pin only while the HP side holds its pad. */
static int pin_take(unsigned pin)
{
    if (led_pin(pin)) {
        return pin < LP_PADS &&
                       (TIKU_P4_REG_READ(LP_IOMUX_PAD(pin)) & LP_PAD_MUX_LP)
                   ? -1 : 0;
    }
    return tiku_gpio_claim((uint8_t)(pin / 8u + 1u), (uint8_t)(pin % 8u),
                           owner);
}

/** @brief Release what pin_take() claimed. */
static void pin_give(unsigned pin)
{
    if (!led_pin(pin)) {
        (void)tiku_gpio_release((uint8_t)(pin / 8u + 1u), (uint8_t)(pin % 8u),
                                owner);
    }
}

int tiku_pwm_arch_init(uint8_t gpio_pin, uint32_t freq_hz, uint16_t duty_u16)
{
    unsigned res = 0u, n = gpio_pin, r;
    uint32_t div, state;
    int c, t, own = -1;

    if (n >= PINS || freq_hz == 0u) {
        return TIKU_PWM_ERR_INVALID;
    }
    div = timing(freq_hz, &res);
    if (div == 0u) {
        return TIKU_PWM_ERR_FREQ;
    }
    c = find(n);
    if (c < 0) {
        for (c = 0; c < (int)CHANNELS && chans[c].used; c++) {
        }
        if (c == (int)CHANNELS) {
            return TIKU_PWM_ERR_BUSY;
        }
        if (pin_take(n) != 0) {
            return TIKU_PWM_ERR_INVALID;
        }
    } else {
        own = chans[c].timer;
    }
    state = TIKU_P4_IRQ_SAVE();
    if (open_channels == 0u) {
        ledc_open();
    }
    t = timer_take(div, res, own);
    if (t < 0) {
        TIKU_P4_IRQ_RESTORE(state);
        if (own < 0) {
            pin_give(n);
        }
        return TIKU_PWM_ERR_BUSY;
    }
    if (own >= 0 && own != t) {
        timer_drop((unsigned)own);
    }
    chans[c].timer = (uint8_t)t;
    TIKU_P4_REG_WRITE(CH_HPOINT(c), 0u);
    for (r = 1u; r < GAMMA_RANGES; r++) {
        TIKU_P4_REG_WRITE(GAMMA_RANGE(c, r), 0u);
    }
    TIKU_P4_REG_WRITE(CH_CONF0(c), (uint32_t)t | CONF0_SIG_OUT_EN);
    duty_load((unsigned)c, duty_u16);
    if (own < 0) {
        chans[c].used = 1u;
        chans[c].pin = (uint8_t)n;
        chans[c].mux = TIKU_P4_REG_READ(PIN_MUX(n));
        chans[c].route = TIKU_P4_REG_READ(PIN_ROUTE(n));
        chans[c].config = TIKU_P4_REG_READ(PIN_CONFIG(n));
        chans[c].enable = TIKU_P4_REG_READ(PIN_ENABLE(n)) & PIN_BIT(n);
        open_channels++;
        TIKU_P4_REG_WRITE(PIN_ENABLE(n) + 8u, PIN_BIT(n));
        /* GPIO function, pulls off, input on to read the pad back. */
        TIKU_P4_REG_WRITE(PIN_MUX(n), (chans[c].mux & ~((7u << 12) | (3u << 7))) |
                                          (1u << 12) | (1u << 9));
        TIKU_P4_REG_WRITE(PIN_CONFIG(n), chans[c].config & ~(1u << 2));
        TIKU_P4_REG_WRITE(PIN_ROUTE(n), SIG_LEDC + (uint32_t)c);
        TIKU_P4_REG_WRITE(PIN_ENABLE(n) + 4u, PIN_BIT(n));
    }
    TIKU_P4_IRQ_RESTORE(state);
    return TIKU_PWM_OK;
}

int tiku_pwm_arch_set_duty(uint8_t gpio_pin, uint16_t duty_u16)
{
    int c = find(gpio_pin);
    uint32_t state;

    if (c < 0) {
        return TIKU_PWM_ERR_INVALID;
    }
    state = TIKU_P4_IRQ_SAVE();
    duty_load((unsigned)c, duty_u16);
    TIKU_P4_IRQ_RESTORE(state);
    return TIKU_PWM_OK;
}

int tiku_pwm_arch_close(uint8_t gpio_pin)
{
    int c = find(gpio_pin);
    unsigned n = gpio_pin;
    uint32_t state;

    if (c < 0) {
        return TIKU_PWM_ERR_INVALID;
    }
    state = TIKU_P4_IRQ_SAVE();
    TIKU_P4_REG_WRITE(CH_CONF0(c), (TIKU_P4_REG_READ(CH_CONF0(c)) &
                                    ~(CONF0_SIG_OUT_EN | CONF0_IDLE_LV)) |
                                       CONF0_PARA_UP);
    TIKU_P4_REG_WRITE(PIN_ENABLE(n) + 8u, PIN_BIT(n));
    TIKU_P4_REG_WRITE(PIN_ROUTE(n), chans[c].route);
    TIKU_P4_REG_WRITE(PIN_CONFIG(n), chans[c].config);
    TIKU_P4_REG_WRITE(PIN_MUX(n), chans[c].mux);
    if (chans[c].enable) {
        TIKU_P4_REG_WRITE(PIN_ENABLE(n) + 4u, PIN_BIT(n));
    }
    timer_drop(chans[c].timer);
    chans[c].used = 0u;
    if (--open_channels == 0u) {
        ledc_close();
    }
    TIKU_P4_IRQ_RESTORE(state);
    pin_give(n);
    return TIKU_PWM_OK;
}

uint16_t tiku_pwm_arch_get_duty(uint8_t gpio_pin)
{
    int c = find(gpio_pin);

    if (c < 0) {
        return 0u;
    }
    return (uint16_t)(((TIKU_P4_REG_READ(CH_DUTY(c)) & DUTY_MASK) >> 4)
                      << (RES_MAX - timer_res[chans[c].timer]));
}

uint16_t tiku_pwm_arch_get_top(uint8_t gpio_pin)
{
    int c = find(gpio_pin);

    if (c < 0) {
        return 0u;
    }
    return (uint16_t)((1u << timer_res[chans[c].timer]) - 1u);
}

int tiku_pwm_arch_is_enabled(uint8_t gpio_pin)
{
    int c = find(gpio_pin);

    if (c < 0) {
        return 0;
    }
    return (TIKU_P4_REG_READ(CH_CONF0(c)) & CONF0_SIG_OUT_EN) != 0u &&
           (TIKU_P4_REG_READ(TIMER_CONF(chans[c].timer)) &
            (TIMER_PAUSE | TIMER_RST)) == 0u;
}
