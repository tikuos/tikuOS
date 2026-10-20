/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_arch.c - P4 GPIO matrix and IO MUX, ESP-IDF 4d59230 register map.
 *
 * Reserved pads, pads the LP core holds and pads routed to a peripheral
 * cannot be changed through this API.  Every free pad can raise an edge
 * interrupt, broadcast as TIKU_EVENT_GPIO.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_gpio_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32p4_regs.h"
#include <hal/tiku_gpio_irq_hal.h>
#include <kernel/process/tiku_process.h>

/* gpio_reg.h and io_mux_reg.h (hw_ver1).  Pads 0..31 use the first register
 * of each pair, pads 32..54 the second. */
#define GPIO_OUT(n)          ((unsigned)(n) < 32u ? 0x500E0004u : 0x500E0010u)
#define GPIO_OUT_W1TS(n)     ((unsigned)(n) < 32u ? 0x500E0008u : 0x500E0014u)
#define GPIO_OUT_W1TC(n)     ((unsigned)(n) < 32u ? 0x500E000Cu : 0x500E0018u)
#define GPIO_ENABLE(n)       ((unsigned)(n) < 32u ? 0x500E0020u : 0x500E002Cu)
#define GPIO_ENABLE_W1TS(n)  ((unsigned)(n) < 32u ? 0x500E0024u : 0x500E0030u)
#define GPIO_ENABLE_W1TC(n)  ((unsigned)(n) < 32u ? 0x500E0028u : 0x500E0034u)
#define GPIO_IN(n)           ((unsigned)(n) < 32u ? 0x500E003Cu : 0x500E0040u)
#define GPIO_STATUS_W1TC(n)  ((unsigned)(n) < 32u ? 0x500E004Cu : 0x500E0058u)
#define GPIO_INTR0_STATUS(n) ((unsigned)(n) < 32u ? 0x500E005Cu : 0x500E0060u)
#define GPIO_PIN(n)          (0x500E0074u + 4u * (n))
#define GPIO_FUNC_IN(signal) (0x500E0158u + 4u * (signal))
#define GPIO_FUNC_OUT(n)     (0x500E0558u + 4u * (n))
#define IO_MUX(n)            (0x500E1004u + 4u * (n))
#define BIT(n)               (1u << ((n) & 31u))

#define PIN_PAD_DRIVER   (1u << 2)        /* open drain */
#define PIN_TYPE_POS     7u               /* 1 rising, 2 falling, 3 either */
#define PIN_TYPE_MSK     (7u << PIN_TYPE_POS)
#define PIN_ENA_MSK      (0x1Fu << 13)
#define PIN_ENA_INTR0    (1u << 13)       /* GPIO_INTR0, matrix source 74 */
#define MUX_FUN_IE       (1u << 9)
#define MUX_FUN_WPU      (1u << 8)
#define MUX_FUN_WPD      (1u << 7)
#define MUX_SEL_POS      12u
#define MUX_SEL_MSK      (7u << MUX_SEL_POS)
#define MUX_SEL_GPIO     (1u << MUX_SEL_POS)
#define OUT_SEL_MSK      511u
#define OUT_SEL_GPIO     256u
#define IN_SEL_MATRIX    (1u << 7)        /* the signal comes through the matrix */
#define IN_SEL_MSK       (IN_SEL_MATRIX | 63u)
#define IN_SIGNALS       256u

/* lp_iomux_reg.h: pads 0..15 follow the LP IO module while MUX_LP is set. */
#define LP_IOMUX_PAD(n)  (0x5012B008u + 4u * (n))
#define LP_PAD_MUX_LP    (1u << 4)
#define LP_PADS          16u

/* efuse_reg.h, EFUSE_RD_REPEAT_DATA0: either bit lets the JTAG controller
 * take its signals from GPIO2..5 instead of the USB Serial/JTAG. */
#define EFUSE_REPEAT0      0x5012D030u
#define EFUSE_PAD_JTAG     ((1u << 9) | (1u << 15))

#define GPIO_IRQ_LINE    3u
#define GPIO_IRQ_SOURCE  74u

/** @brief Convert the kernel's one-based eight-pin ports to GPIO0..54. */
static int number(uint8_t port, uint8_t pin)
{
    unsigned n;
    if (port < 1 || port > 7 || pin > 7) {
        return -1;
    }
    n = (port - 1u) * 8u + pin;
    return n < 55u ? (int)n : -1;
}

/** @brief A pad the board header gives to its own wiring. */
static int reserved(unsigned n)
{
    return (int)(((uint64_t)TIKU_BOARD_GPIO_RESERVED >> n) & 1u);
}

/**
 * @brief Whether IO MUX function 0 of pad @p n is a peripheral: UART0 on
 *        37/38, the SD slot 0 bus on 39..48, and JTAG on 2..5 when eFuse
 *        selects pad JTAG.  On every other pad function 0 is GPIO.
 */
static int function0_peripheral(unsigned n)
{
    if (n >= 37u && n <= 48u) {
        return 1;
    }
    return n >= 2u && n <= 5u &&
           (TIKU_P4_REG_READ(EFUSE_REPEAT0) & EFUSE_PAD_JTAG) != 0u;
}

int tiku_gpio_arch_is_peripheral(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    uint32_t mux;
    unsigned signal;

    if (n < 0) {
        return -1;
    }
    if (reserved((unsigned)n)) {
        return 1;
    }
    if ((unsigned)n < LP_PADS &&
        (TIKU_P4_REG_READ(LP_IOMUX_PAD(n)) & LP_PAD_MUX_LP) != 0u) {
        return 1;
    }
    mux = (TIKU_P4_REG_READ(IO_MUX(n)) & MUX_SEL_MSK) >> MUX_SEL_POS;
    if (mux > 1u || (mux == 0u && function0_peripheral((unsigned)n)) ||
        (TIKU_P4_REG_READ(GPIO_FUNC_OUT(n)) & OUT_SEL_MSK) != OUT_SEL_GPIO) {
        return 1;
    }
    for (signal = 1; signal < IN_SIGNALS; signal++) {
        if ((TIKU_P4_REG_READ(GPIO_FUNC_IN(signal)) & IN_SEL_MSK) ==
            (IN_SEL_MATRIX | (unsigned)n)) {
            return 1;
        }
    }
    return 0;
}

/** @brief Route a pad to the GPIO output signal through function 1, input
 *         buffer on and push-pull, keeping its pulls and drive strength. */
static void route_gpio(unsigned n)
{
    TIKU_P4_REG_WRITE(IO_MUX(n), (TIKU_P4_REG_READ(IO_MUX(n)) & ~MUX_SEL_MSK) |
                                     MUX_SEL_GPIO | MUX_FUN_IE);
    TIKU_P4_REG_WRITE(GPIO_FUNC_OUT(n), OUT_SEL_GPIO);
    TIKU_P4_REG_WRITE(GPIO_PIN(n),
                      TIKU_P4_REG_READ(GPIO_PIN(n)) & ~PIN_PAD_DRIVER);
}

int8_t tiku_gpio_arch_write(uint8_t port, uint8_t pin, uint8_t value)
{
    int n = number(port, pin);
    uint32_t state;
    if (n < 0) {
        return -1;
    }
    state = TIKU_P4_IRQ_SAVE();
    if (tiku_gpio_arch_is_peripheral(port, pin) != 0) {
        TIKU_P4_IRQ_RESTORE(state);
        return -1;
    }
    TIKU_P4_REG_WRITE(value ? GPIO_OUT_W1TS(n) : GPIO_OUT_W1TC(n), BIT(n));
    route_gpio((unsigned)n);
    TIKU_P4_REG_WRITE(GPIO_ENABLE_W1TS(n), BIT(n));
    TIKU_P4_IRQ_RESTORE(state);
    return 0;
}
int8_t tiku_gpio_arch_set_output(uint8_t port, uint8_t pin)
{
    return tiku_gpio_arch_write(port, pin, 0);
}
int8_t tiku_gpio_arch_set_input(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    uint32_t state;
    if (n < 0) {
        return -1;
    }
    state = TIKU_P4_IRQ_SAVE();
    if (tiku_gpio_arch_is_peripheral(port, pin) != 0) {
        TIKU_P4_IRQ_RESTORE(state);
        return -1;
    }
    TIKU_P4_REG_WRITE(GPIO_ENABLE_W1TC(n), BIT(n));
    route_gpio((unsigned)n);
    TIKU_P4_IRQ_RESTORE(state);
    return 0;
}
int8_t tiku_gpio_arch_read(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    if (n < 0) {
        return -1;
    }
    return (int8_t)((TIKU_P4_REG_READ(GPIO_IN(n)) >> (n & 31)) & 1u);
}
int8_t tiku_gpio_arch_get_dir(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    if (n < 0) {
        return -1;
    }
    return (int8_t)((TIKU_P4_REG_READ(GPIO_ENABLE(n)) >> (n & 31)) & 1u);
}
int8_t tiku_gpio_arch_toggle(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    int8_t result;
    uint32_t state;
    if (n < 0) {
        return -1;
    }
    state = TIKU_P4_IRQ_SAVE();
    result = tiku_gpio_arch_write(
        port, pin, !(TIKU_P4_REG_READ(GPIO_OUT(n)) & BIT(n)));
    TIKU_P4_IRQ_RESTORE(state);
    return result;
}

#if defined(TIKU_BOARD_LED_PIN)
/** @brief Whether the LP core's IO MUX holds the LED pad. */
static int led_lent(void)
{
    return (TIKU_P4_REG_READ(LP_IOMUX_PAD(TIKU_BOARD_LED_PIN)) &
            LP_PAD_MUX_LP) != 0u;
}

void tiku_p4_led_init(void)
{
    unsigned n = TIKU_BOARD_LED_PIN;
    uint32_t state = TIKU_P4_IRQ_SAVE();
    if (!led_lent()) {
        TIKU_P4_REG_WRITE(GPIO_OUT_W1TC(n), BIT(n));
        route_gpio(n);
        TIKU_P4_REG_WRITE(GPIO_ENABLE_W1TS(n), BIT(n));
    }
    TIKU_P4_IRQ_RESTORE(state);
}

void tiku_p4_led_set(int on)
{
    unsigned n = TIKU_BOARD_LED_PIN;
    uint32_t state = TIKU_P4_IRQ_SAVE();
    if (!led_lent()) {
        if (on < 0) {
            on = !(TIKU_P4_REG_READ(GPIO_OUT(n)) & BIT(n));
        }
        TIKU_P4_REG_WRITE(on ? GPIO_OUT_W1TS(n) : GPIO_OUT_W1TC(n), BIT(n));
    }
    TIKU_P4_IRQ_RESTORE(state);
}
#endif

#if defined(TIKU_BOARD_BTN_PIN)
void tiku_p4_button_init(void)
{
    unsigned n = TIKU_BOARD_BTN_PIN;
    uint32_t state = TIKU_P4_IRQ_SAVE();
    TIKU_P4_REG_WRITE(GPIO_ENABLE_W1TC(n), BIT(n));
    route_gpio(n);
    TIKU_P4_REG_WRITE(IO_MUX(n),
                      (TIKU_P4_REG_READ(IO_MUX(n)) & ~MUX_FUN_WPD) | MUX_FUN_WPU);
    TIKU_P4_IRQ_RESTORE(state);
}

uint8_t tiku_p4_button_pressed(void)
{
    unsigned n = TIKU_BOARD_BTN_PIN;
    return (uint8_t)!((TIKU_P4_REG_READ(GPIO_IN(n)) >> (n & 31u)) & 1u);
}
#endif

/* Pin edge interrupts: each pin picks its edge in its own PIN register and
 * all of them share GPIO_INTR0, matrix source 74, on one CLIC line. */
static volatile uint64_t gpio_armed;

/** @brief Every armed pin that fired: clear, then broadcast one event each. */
static void gpio_isr(void)
{
    uint64_t fired = ((uint64_t)TIKU_P4_REG_READ(GPIO_INTR0_STATUS(32u)) << 32 |
                      TIKU_P4_REG_READ(GPIO_INTR0_STATUS(0u))) &
                     gpio_armed;

    /* Cleared before the posts: an edge during them sets the bit again and
     * raises the interrupt again. */
    TIKU_P4_REG_WRITE(GPIO_STATUS_W1TC(0u), (uint32_t)fired);
    TIKU_P4_REG_WRITE(GPIO_STATUS_W1TC(32u), (uint32_t)(fired >> 32));
    while (fired != 0u) {
        unsigned n = (unsigned)__builtin_ctzll(fired);

        fired &= fired - 1u;
        tiku_process_post(TIKU_PROCESS_BROADCAST, TIKU_EVENT_GPIO,
                          (tiku_event_data_t)TIKU_GPIO_IRQ_PACK(n / 8u + 1u,
                                                                n % 8u));
    }
}

uint64_t tiku_p4_gpio_irq_armed(void)
{
    return gpio_armed;
}

int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin, tiku_gpio_edge_t edge)
{
    int n = number(port, pin);
    uint32_t type, reg, state;

    if (n < 0) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    switch (edge) {
    case TIKU_GPIO_EDGE_RISING:
        type = 1u;
        break;
    case TIKU_GPIO_EDGE_FALLING:
        type = 2u;
        break;
    case TIKU_GPIO_EDGE_BOTH:
        type = 3u;
        break;
    default:
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    /* The edge detector watches the pad: the pin must be a free input. */
    if (tiku_gpio_arch_set_input(port, pin) != 0) {
        return TIKU_GPIO_IRQ_ERR_UNSUP;
    }
    state = TIKU_P4_IRQ_SAVE();
    if (!tiku_p4_irq_owned(GPIO_IRQ_LINE, GPIO_IRQ_SOURCE, gpio_isr)) {
        if (tiku_p4_irq_attach(GPIO_IRQ_LINE, GPIO_IRQ_SOURCE, 1u, gpio_isr) !=
            0) {
            TIKU_P4_IRQ_RESTORE(state);
            return TIKU_GPIO_IRQ_ERR_UNSUP;
        }
        (void)tiku_p4_irq_enable(GPIO_IRQ_LINE, 1);
    }
    reg = TIKU_P4_REG_READ(GPIO_PIN(n)) & ~(PIN_TYPE_MSK | PIN_ENA_MSK);
    TIKU_P4_REG_WRITE(GPIO_PIN(n), reg | (type << PIN_TYPE_POS));
    /* An edge latched while the pin was being configured is not delivered. */
    TIKU_P4_REG_WRITE(GPIO_STATUS_W1TC(n), BIT(n));
    gpio_armed |= (uint64_t)1u << n;
    TIKU_P4_REG_WRITE(GPIO_PIN(n),
                      reg | (type << PIN_TYPE_POS) | PIN_ENA_INTR0);
    TIKU_P4_IRQ_RESTORE(state);
    return TIKU_GPIO_IRQ_OK;
}

int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin)
{
    int n = number(port, pin);
    uint32_t state;

    if (n < 0) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    state = TIKU_P4_IRQ_SAVE();
    TIKU_P4_REG_WRITE(GPIO_PIN(n), TIKU_P4_REG_READ(GPIO_PIN(n)) &
                                       ~(PIN_TYPE_MSK | PIN_ENA_MSK));
    TIKU_P4_REG_WRITE(GPIO_STATUS_W1TC(n), BIT(n));
    gpio_armed &= ~((uint64_t)1u << n);
    if (gpio_armed == 0u &&
        tiku_p4_irq_owned(GPIO_IRQ_LINE, GPIO_IRQ_SOURCE, gpio_isr)) {
        tiku_p4_irq_detach(GPIO_IRQ_LINE);
    }
    TIKU_P4_IRQ_RESTORE(state);
    return TIKU_GPIO_IRQ_OK;
}
