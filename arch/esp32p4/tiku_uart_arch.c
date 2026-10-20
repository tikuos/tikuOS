/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - P4 UART0, bounded register synchronization and RX drain.
 *
 * Register fields follow ESP-IDF 4d59230 uart_reg.h, hp_sys_clkrst_reg.h,
 * io_mux_reg.h and uart_ll.h (hw_ver1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <tiku.h>
#include "tiku_uart_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32p4_regs.h"

#if TIKU_BOARD_UART_BAUD < 300 || TIKU_BOARD_UART_BAUD > 3000000
#error "P4 UART_BAUD must be between 300 and 3000000"
#endif
#if TIKU_UART_RXBUF_SIZE < 2 || TIKU_UART_RXBUF_SIZE > 32768 ||                \
    (TIKU_UART_RXBUF_SIZE & (TIKU_UART_RXBUF_SIZE - 1))
#error "P4 UART RX buffer must be a power of two between 2 and 32768"
#endif
#if TIKU_BOARD_UART_TX_PIN != 37 || TIKU_BOARD_UART_RX_PIN != 38
#error "P4 UART0 drives its IO MUX pads, GPIO37 (TX) and GPIO38 (RX)"
#endif

#define UART_FIFO        0x500CA000u
#define UART_RAW         0x500CA004u
#define UART_ENABLE      TIKU_P4_UART0_INT_ENABLE
#define UART_CLEAR       0x500CA010u
#define UART_DIVIDER     0x500CA014u
#define UART_STATUS      0x500CA01Cu
#define UART_CONFIG      0x500CA020u
#define UART_THRESHOLD   0x500CA024u
#define UART_MEMORY      0x500CA060u
#define UART_TIMEOUT     0x500CA064u
#define UART_UPDATE      0x500CA098u
/* HP_SYS_CLKRST: bus clocks, the core clock's source and divider, reset. */
#define CLK_SYS          0x500E6018u   /* SOC_CLK_CTRL1, bit 18 */
#define CLK_APB          0x500E601Cu   /* SOC_CLK_CTRL2, bit 7 */
#define CLK_CORE         0x500E6068u   /* PERI_CLK_CTRL110: 25:24 source, 26 on */
#define CLK_DIVIDER      0x500E606Cu   /* PERI_CLK_CTRL111: 7:0 divider - 1 */
#define RESET_UART       0x500E60C4u   /* HP_RST_EN1, bit 13: APB reset */
#define CLK_SYS_UART0    (1u << 18)
#define CLK_APB_UART0    (1u << 7)
#define CLK_CORE_MASK    (7u << 24)
#define CLK_CORE_XTAL_ON (1u << 26)    /* source 0, the 40 MHz crystal */
#define RESET_UART0_APB  (1u << 13)
#define XTAL_HZ          40000000ULL
#define UART_IRQ_LINE    TIKU_P4_UART0_IRQ_LINE
#define UART_IRQ_SOURCE  TIKU_P4_UART0_IRQ_SOURCE
#define UART_RX_ERRORS   ((1u << 2) | (1u << 3))
#define UART_RX_OVERFLOW (1u << 4)
#define UART_INTERRUPTS  TIKU_P4_UART0_RX_INTERRUPTS
#define UART_SYNC_SPINS  4096u
#define UART_TX_SPINS    200000u
#define UART_FIFO_SIZE   128u
#define UART_RING_MASK   (TIKU_UART_RXBUF_SIZE - 1u)
/* Pads and the GPIO matrix (io_mux_reg.h, gpio_reg.h, gpio_sig_map.h). */
#define IO_MUX(n)        (0x500E1004u + 4u * (n))
#define GPIO_FUNC_OUT(n) (0x500E0558u + 4u * (n))
#define GPIO_ENABLE1      0x500E002Cu
#define GPIO_ENABLE1_W1TS 0x500E0030u
#define GPIO_ENABLE1_W1TC 0x500E0034u
#define TX_ENABLE_BIT     (1u << (UART_TX_PIN - 32u))
#define UART_RX_SIGNAL   0x500E0180u   /* FUNC10_IN_SEL_CFG, UART0 RXD */
#define MUX_SEL_MSK      (7u << 12)
#define MUX_SEL_GPIO     (1u << 12)
#define MUX_FUN_IE       (1u << 9)
#define MUX_FUN_WPU      (1u << 8)
#define MUX_FUN_WPD      (1u << 7)
#define IN_SEL_MATRIX    (1u << 7)
#define UART_TX_SIGNAL   10u           /* UART0_TXD_PAD_OUT_IDX */
#define UART_TX_PIN      TIKU_BOARD_UART_TX_PIN
#define UART_RX_PIN      TIKU_BOARD_UART_RX_PIN

static uint8_t bytes[TIKU_UART_RXBUF_SIZE];
static volatile uint16_t head, tail;
static volatile uint8_t ready;
static tiku_p4_uart_stats_t counters;
static uint8_t pins_changed;
static uint32_t saved_tx_mux, saved_tx_route, saved_tx_enable;
static uint32_t saved_rx_mux, saved_rx_signal;

/** @brief Wait at most UART_SYNC_SPINS reads for a register update to finish.
 */
static int synchronize(void)
{
    unsigned n;
    TIKU_P4_REG_WRITE(UART_UPDATE, 1u);
    for (n = 0; n < UART_SYNC_SPINS; n++) {
        if (!(TIKU_P4_REG_READ(UART_UPDATE) & 1u)) {
            return 0;
        }
    }
    return -1;
}

/** @brief Increment a 16-bit event counter without wrapping. */
static void increment(uint16_t *value)
{
    if (*value != UINT16_MAX) {
        (*value)++;
    }
}

/** @brief Drain one FIFO snapshot with interrupts masked; preserve earlier ring
 * bytes. */
static void receive(void)
{
    uint32_t raw, length;
    if (!ready) {
        return;
    }
    raw = TIKU_P4_REG_READ(UART_RAW) & UART_INTERRUPTS;
    TIKU_P4_REG_WRITE(UART_CLEAR, raw);
    if (raw & UART_RX_OVERFLOW) {
        increment(&counters.overruns);
    }
    if (raw & UART_RX_ERRORS) {
        increment(&counters.rx_errors);
    }
    length = TIKU_P4_REG_READ(UART_STATUS) & 255u;
    if (length > UART_FIFO_SIZE) {
        length = UART_FIFO_SIZE;
    }
    while (length--) {
        uint8_t byte = (uint8_t)TIKU_P4_REG_READ(UART_FIFO);
        uint16_t next = (uint16_t)((head + 1u) & UART_RING_MASK);
        if (next == tail) {
            increment(&counters.overruns);
        } else {
            bytes[head] = byte;
            head = next;
        }
    }
}

/** @brief Account for an RX interrupt and transfer its FIFO snapshot. */
static void uart_interrupt(void)
{
    if (counters.interrupts != UINT32_MAX) {
        counters.interrupts++;
    }
    receive();
}

/** @brief Restore the pads and the RX signal route this driver changed. */
static void restore_pins(void)
{
    if (pins_changed) {
        TIKU_P4_REG_WRITE(GPIO_FUNC_OUT(UART_TX_PIN), saved_tx_route);
        TIKU_P4_REG_WRITE(saved_tx_enable ? GPIO_ENABLE1_W1TS
                                          : GPIO_ENABLE1_W1TC, TX_ENABLE_BIT);
        TIKU_P4_REG_WRITE(IO_MUX(UART_TX_PIN), saved_tx_mux);
        TIKU_P4_REG_WRITE(IO_MUX(UART_RX_PIN), saved_rx_mux);
        TIKU_P4_REG_WRITE(UART_RX_SIGNAL, saved_rx_signal);
    }
    pins_changed = 0;
}

void tiku_p4_uart_stop(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    ready = 0;
    if (tiku_p4_irq_owned(UART_IRQ_LINE, UART_IRQ_SOURCE, uart_interrupt)) {
        TIKU_P4_REG_WRITE(UART_ENABLE, 0);
        tiku_p4_irq_detach(UART_IRQ_LINE);
        /* Hold the UART in reset before reconnecting an earlier pad route. */
        TIKU_P4_REG_WRITE(RESET_UART,
                          TIKU_P4_REG_READ(RESET_UART) | RESET_UART0_APB);
        restore_pins();
    }
    head = tail = 0;
    TIKU_P4_IRQ_RESTORE(state);
}

int tiku_p4_uart_start(uint32_t baud, int loopback)
{
    uint32_t state, pre, divider, config;
    unsigned pin;
    if (baud < 300u || baud > 3000000u || (loopback != 0 && loopback != 1)) {
        return -1;
    }
    state = TIKU_P4_IRQ_SAVE();
    if (tiku_p4_irq_owned(UART_IRQ_LINE, UART_IRQ_SOURCE, uart_interrupt)) {
        tiku_p4_uart_stop();
    }
    /* A matrix route of UART0 TXD would expose loopback traffic on a pad. */
    for (pin = 0; pin < 55; pin++) {
        if ((TIKU_P4_REG_READ(GPIO_FUNC_OUT(pin)) & 511u) == UART_TX_SIGNAL) {
            TIKU_P4_IRQ_RESTORE(state);
            return -1;
        }
    }
    if (tiku_p4_irq_attach(UART_IRQ_LINE, UART_IRQ_SOURCE, 2u,
                           uart_interrupt) != 0) {
        ready = 0;
        TIKU_P4_IRQ_RESTORE(state);
        return -1;
    }
    saved_tx_mux = TIKU_P4_REG_READ(IO_MUX(UART_TX_PIN));
    saved_tx_route = TIKU_P4_REG_READ(GPIO_FUNC_OUT(UART_TX_PIN));
    saved_tx_enable = TIKU_P4_REG_READ(GPIO_ENABLE1) & TX_ENABLE_BIT;
    saved_rx_mux = TIKU_P4_REG_READ(IO_MUX(UART_RX_PIN));
    saved_rx_signal = TIKU_P4_REG_READ(UART_RX_SIGNAL);
    pins_changed = 1;
    /* GPIO function with its output disabled disconnects UART0 TX from the
     * pad; IO MUX function 0 connects it. */
    TIKU_P4_REG_WRITE(GPIO_ENABLE1_W1TC, TX_ENABLE_BIT);
    TIKU_P4_REG_WRITE(GPIO_FUNC_OUT(UART_TX_PIN), 256u);
    TIKU_P4_REG_WRITE(IO_MUX(UART_TX_PIN),
                      (saved_tx_mux & ~MUX_SEL_MSK) | MUX_SEL_GPIO);
    TIKU_P4_REG_WRITE(CLK_SYS, TIKU_P4_REG_READ(CLK_SYS) | CLK_SYS_UART0);
    TIKU_P4_REG_WRITE(CLK_APB, TIKU_P4_REG_READ(CLK_APB) | CLK_APB_UART0);
    TIKU_P4_REG_WRITE(RESET_UART,
                      TIKU_P4_REG_READ(RESET_UART) | RESET_UART0_APB);
    TIKU_P4_REG_WRITE(RESET_UART,
                      TIKU_P4_REG_READ(RESET_UART) & ~RESET_UART0_APB);
    TIKU_P4_REG_WRITE(UART_ENABLE, 0);
    /* The crystal divided by pre, then by divider/16: the integer part of
     * divider has 12 bits (uart_ll.h _uart_ll_set_baudrate). */
    pre = (uint32_t)((XTAL_HZ + 4095ULL * baud - 1u) / (4095ULL * baud));
    divider = (uint32_t)((XTAL_HZ * 16u + (uint64_t)baud * pre / 2u) /
                         ((uint64_t)baud * pre));
    TIKU_P4_REG_WRITE(CLK_CORE, (TIKU_P4_REG_READ(CLK_CORE) & ~CLK_CORE_MASK) |
                                    CLK_CORE_XTAL_ON);
    TIKU_P4_REG_WRITE(CLK_DIVIDER,
                      (TIKU_P4_REG_READ(CLK_DIVIDER) & ~0xFFFFFFu) | (pre - 1u));
    TIKU_P4_REG_WRITE(UART_DIVIDER, (divider >> 4) | ((divider & 15u) << 20));
    TIKU_P4_REG_WRITE(UART_MEMORY,
                      (TIKU_P4_REG_READ(UART_MEMORY) & ~(1u << 25)) |
                          (1u << 26));
    config = (3u << 2) | (1u << 4) | (1u << 18) | (loopback ? 1u << 12 : 0);
    TIKU_P4_REG_WRITE(UART_CONFIG, config | (3u << 22));
    TIKU_P4_REG_WRITE(UART_THRESHOLD, 32u | (1u << 21));
    TIKU_P4_REG_WRITE(UART_TIMEOUT, (20u << 2) | 1u);
    if (synchronize() != 0) {
        goto failed;
    }
    TIKU_P4_REG_WRITE(UART_CONFIG, config);
    if (synchronize() != 0 || TIKU_P4_REG_READ(UART_DIVIDER) !=
                                  ((divider >> 4) | ((divider & 15u) << 20))) {
        goto failed;
    }
    if (!loopback) {
        TIKU_P4_REG_WRITE(IO_MUX(UART_RX_PIN),
                          (saved_rx_mux & ~(MUX_SEL_MSK | MUX_FUN_WPD)) |
                              MUX_FUN_IE | MUX_FUN_WPU);
        TIKU_P4_REG_WRITE(UART_RX_SIGNAL, saved_rx_signal & ~IN_SEL_MATRIX);
        TIKU_P4_REG_WRITE(IO_MUX(UART_TX_PIN), saved_tx_mux & ~MUX_SEL_MSK);
    }
    head = tail = 0;
    counters = (tiku_p4_uart_stats_t){0};
    TIKU_P4_REG_WRITE(UART_CLEAR, 0xFFFFFu);
    ready = 1;
    TIKU_P4_REG_WRITE(UART_ENABLE, UART_INTERRUPTS);
    if (tiku_p4_irq_enable(UART_IRQ_LINE, 1) != 0) {
        goto failed;
    }
    TIKU_P4_IRQ_RESTORE(state);
    return 0;
failed:
    tiku_p4_uart_stop();
    TIKU_P4_IRQ_RESTORE(state);
    return -1;
}

int tiku_p4_uart_ready(void)
{
    return ready;
}
void tiku_uart_init(void)
{
    (void)tiku_p4_uart_start(TIKU_BOARD_UART_BAUD, 0);
}

void tiku_uart_putc(char byte)
{
    unsigned n;
    uint32_t state;
    /* The wait leaves the caller's interrupt mask unchanged. FIFO insertion
     * is atomic with its capacity check against another execution context. */
    for (n = 0; ready && n < UART_TX_SPINS; n++) {
        state = TIKU_P4_IRQ_SAVE();
        if (((TIKU_P4_REG_READ(UART_STATUS) >> 16) & 255u) < UART_FIFO_SIZE) {
            TIKU_P4_REG_WRITE(UART_FIFO, (uint8_t)byte);
            TIKU_P4_IRQ_RESTORE(state);
            return;
        }
        TIKU_P4_IRQ_RESTORE(state);
    }
    state = TIKU_P4_IRQ_SAVE();
    if (counters.tx_dropped != UINT32_MAX) {
        counters.tx_dropped++;
    }
    TIKU_P4_IRQ_RESTORE(state);
}
uint8_t tiku_uart_rx_ready(void)
{
    uint8_t result;
    uint32_t state = TIKU_P4_IRQ_SAVE();
    receive();
    result = ready && head != tail;
    TIKU_P4_IRQ_RESTORE(state);
    return result;
}
int tiku_uart_getc(void)
{
    int result = -1;
    uint32_t state = TIKU_P4_IRQ_SAVE();
    receive();
    if (ready && head != tail) {
        result = bytes[tail];
        tail = (tail + 1u) & UART_RING_MASK;
    }
    TIKU_P4_IRQ_RESTORE(state);
    return result;
}
uint16_t tiku_uart_overrun_count(void)
{
    uint16_t result;
    uint32_t state = TIKU_P4_IRQ_SAVE();
    receive();
    result = counters.overruns;
    TIKU_P4_IRQ_RESTORE(state);
    return result;
}
void tiku_uart_overrun_reset(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    counters.overruns = 0;
    TIKU_P4_IRQ_RESTORE(state);
}
void tiku_p4_uart_stats(tiku_p4_uart_stats_t *out)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    if (out != NULL) {
        *out = counters;
    }
    TIKU_P4_IRQ_RESTORE(state);
}
