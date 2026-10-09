/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_uart_arch.c - C5 UART0 with bounded register synchronization and RX drain.
 * Register fields follow ESP-IDF 4d59230 uart_reg.h, pcr_reg.h and uart_ll.h.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_uart_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32c5_regs.h"

#if TIKU_BOARD_UART_BAUD < 300 || TIKU_BOARD_UART_BAUD > 3000000
#error "C5 UART_BAUD must be between 300 and 3000000"
#endif
#if TIKU_UART_RXBUF_SIZE < 2 || TIKU_UART_RXBUF_SIZE > 32768 || \
    (TIKU_UART_RXBUF_SIZE & (TIKU_UART_RXBUF_SIZE - 1))
#error "C5 UART RX buffer must be a power of two between 2 and 32768"
#endif

#define UART_FIFO       0x60000000u
#define UART_RAW        0x60000004u
#define UART_ENABLE     TIKU_C5_UART0_INT_ENABLE
#define UART_CLEAR      0x60000010u
#define UART_DIVIDER    0x60000014u
#define UART_STATUS     0x6000001Cu
#define UART_CONFIG     0x60000020u
#define UART_THRESHOLD  0x60000024u
#define UART_MEMORY     0x60000060u
#define UART_TIMEOUT    0x60000064u
#define UART_UPDATE     0x60000098u
#define UART_PCR        0x60096000u
#define UART_CLOCK      0x60096004u
#define UART_IRQ_LINE   TIKU_C5_UART0_IRQ_LINE
#define UART_IRQ_SOURCE TIKU_C5_UART0_IRQ_SOURCE
#define UART_RX_FULL    (1u << 0)
#define UART_RX_ERRORS  ((1u << 2) | (1u << 3))
#define UART_RX_OVERFLOW (1u << 4)
#define UART_RX_TIMEOUT (1u << 8)
#define UART_INTERRUPTS TIKU_C5_UART0_RX_INTERRUPTS
#define UART_SYNC_SPINS 4096u
#define UART_TX_SPINS   200000u
#define UART_FIFO_SIZE  128u
#define UART_RING_MASK (TIKU_UART_RXBUF_SIZE - 1u)
#define GPIO_ENABLE    0x60091034u
#define GPIO_MUX(n)    (0x60090000u + 4u * (n))
#define GPIO_ROUTE(n)  (0x60091AD4u + 4u * (n))
#define UART_RX_ROUTE  0x600912ECu
#define UART_TX_PIN    TIKU_BOARD_UART_TX_PIN
#define UART_RX_PIN    TIKU_BOARD_UART_RX_PIN

static uint8_t bytes[TIKU_UART_RXBUF_SIZE];
static volatile uint16_t head, tail;
static volatile uint8_t ready;
static tiku_c5_uart_stats_t counters;
static uint32_t pin_mask, saved_enable, saved_tx_mux, saved_tx_route;
static uint32_t saved_rx_mux, saved_rx_route;

/** @brief Wait at most UART_SYNC_SPINS reads for a register update to finish. */
static int synchronize(void)
{
    unsigned n;
    TIKU_C5_REG_WRITE(UART_UPDATE, 1u);
    for (n = 0; n < UART_SYNC_SPINS; n++) {
        if (!(TIKU_C5_REG_READ(UART_UPDATE) & 1u)) { return 0; }
    }
    return -1;
}

/** @brief Increment a 16-bit event counter without wrapping. */
static void increment(uint16_t *value)
{
    if (*value != UINT16_MAX) { (*value)++; }
}

/** @brief Drain one FIFO snapshot with interrupts masked; preserve earlier ring bytes. */
static void receive(void)
{
    uint32_t raw, length;
    if (!ready) { return; }
    raw = TIKU_C5_REG_READ(UART_RAW) & UART_INTERRUPTS;
    TIKU_C5_REG_WRITE(UART_CLEAR, raw);
    if (raw & UART_RX_OVERFLOW) { increment(&counters.overruns); }
    if (raw & UART_RX_ERRORS) { increment(&counters.rx_errors); }
    length = TIKU_C5_REG_READ(UART_STATUS) & 255u;
    if (length > UART_FIFO_SIZE) { length = UART_FIFO_SIZE; }
    while (length--) {
        uint8_t byte = (uint8_t)TIKU_C5_REG_READ(UART_FIFO);
        uint16_t next = (uint16_t)((head + 1u) & UART_RING_MASK);
        if (next == tail) { increment(&counters.overruns); }
        else { bytes[head] = byte; head = next; }
    }
}

/** @brief Account for an RX interrupt and transfer its FIFO snapshot. */
static void uart_interrupt(void)
{
    if (counters.interrupts != UINT32_MAX) { counters.interrupts++; }
    receive();
}

/** @brief Restore only the pads changed by this driver. */
static void restore_pins(void)
{
    if (pin_mask & (1u << UART_TX_PIN)) {
        TIKU_C5_REG_WRITE(GPIO_ROUTE(UART_TX_PIN), saved_tx_route);
        TIKU_C5_REG_WRITE(GPIO_MUX(UART_TX_PIN), saved_tx_mux);
    }
    if (pin_mask & (1u << UART_RX_PIN)) {
        TIKU_C5_REG_WRITE(GPIO_MUX(UART_RX_PIN), saved_rx_mux);
        TIKU_C5_REG_WRITE(UART_RX_ROUTE, saved_rx_route);
    }
    if (pin_mask) {
        TIKU_C5_REG_WRITE(GPIO_ENABLE + 8u, pin_mask & ~saved_enable);
        TIKU_C5_REG_WRITE(GPIO_ENABLE + 4u, pin_mask & saved_enable);
    }
    pin_mask = 0;
}

void tiku_c5_uart_stop(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    ready = 0;
    if (tiku_c5_irq_owned(UART_IRQ_LINE, UART_IRQ_SOURCE, uart_interrupt)) {
        TIKU_C5_REG_WRITE(UART_ENABLE, 0);
        tiku_c5_irq_detach(UART_IRQ_LINE);
        /* Hold the UART in reset before reconnecting an earlier pad route. */
        TIKU_C5_REG_WRITE(UART_PCR, TIKU_C5_REG_READ(UART_PCR) | 2u);
        restore_pins();
    }
    head = tail = 0;
    TIKU_C5_IRQ_RESTORE(state);
}

int tiku_c5_uart_start(uint32_t baud, int loopback)
{
    uint32_t state, pre, divider, config, tx_mux;
    unsigned pin;
    if (baud < 300u || baud > 3000000u || (loopback != 0 && loopback != 1)) { return -1; }
#if defined(TIKU_CONSOLE_JTAG)
    if (!loopback) { return -1; }
#endif
    state = TIKU_C5_IRQ_SAVE();
    if (tiku_c5_irq_owned(UART_IRQ_LINE, UART_IRQ_SOURCE, uart_interrupt)) {
        tiku_c5_uart_stop();
    }
    /* An alternate matrix route would expose internal-loopback traffic on a pad. */
    for (pin = 0; pin < 29; pin++) {
        if (((TIKU_C5_REG_READ(GPIO_MUX(pin)) >> 12) & 7u) == 1u &&
            (TIKU_C5_REG_READ(GPIO_ROUTE(pin)) & 511u) == 6u) {
            TIKU_C5_IRQ_RESTORE(state);
            return -1;
        }
    }
    if (tiku_c5_irq_attach(UART_IRQ_LINE, UART_IRQ_SOURCE, 2u, uart_interrupt) != 0) {
        ready = 0;
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    tx_mux = TIKU_C5_REG_READ(GPIO_MUX(UART_TX_PIN));
    saved_enable = TIKU_C5_REG_READ(GPIO_ENABLE);
    saved_tx_mux = tx_mux;
    saved_tx_route = TIKU_C5_REG_READ(GPIO_ROUTE(UART_TX_PIN));
    pin_mask = 0;
    if (!loopback || ((tx_mux >> 12) & 7u) == 0) {
        pin_mask = 1u << UART_TX_PIN;
        TIKU_C5_REG_WRITE(GPIO_ENABLE + 8u, pin_mask);
        TIKU_C5_REG_WRITE(GPIO_ROUTE(UART_TX_PIN), 256u | (1u << 10));
        /* GPIO function with its output disabled disconnects UART0 TX. */
        TIKU_C5_REG_WRITE(GPIO_MUX(UART_TX_PIN), (tx_mux & ~(7u << 12)) | (1u << 12));
    }
    TIKU_C5_REG_WRITE(UART_PCR, TIKU_C5_REG_READ(UART_PCR) | 3u);
    TIKU_C5_REG_WRITE(UART_PCR, (TIKU_C5_REG_READ(UART_PCR) | 1u) & ~2u);
    TIKU_C5_REG_WRITE(UART_ENABLE, 0);
    pre = (uint32_t)((48000000ULL + 4095ULL * baud - 1u) / (4095ULL * baud));
    divider = (uint32_t)((48000000ULL * 16u + (uint64_t)baud * pre / 2u) /
                         ((uint64_t)baud * pre));
    TIKU_C5_REG_WRITE(UART_CLOCK, (1u << 22) | ((pre - 1u) << 12));
    TIKU_C5_REG_WRITE(UART_DIVIDER, (divider >> 4) | ((divider & 15u) << 20));
    TIKU_C5_REG_WRITE(UART_MEMORY, (TIKU_C5_REG_READ(UART_MEMORY) & ~(1u << 25)) | (1u << 26));
    config = (3u << 2) | (1u << 4) | (1u << 18) | (loopback ? 1u << 12 : 0);
    TIKU_C5_REG_WRITE(UART_CONFIG, config | (3u << 22));
    TIKU_C5_REG_WRITE(UART_THRESHOLD, 32u | (1u << 21));
    TIKU_C5_REG_WRITE(UART_TIMEOUT, (20u << 2) | 1u);
    if (synchronize() != 0) { goto failed; }
    TIKU_C5_REG_WRITE(UART_CONFIG, config);
    if (synchronize() != 0 || TIKU_C5_REG_READ(UART_DIVIDER) !=
        ((divider >> 4) | ((divider & 15u) << 20))) { goto failed; }
    if (!loopback) {
        saved_rx_mux = TIKU_C5_REG_READ(GPIO_MUX(UART_RX_PIN));
        saved_rx_route = TIKU_C5_REG_READ(UART_RX_ROUTE);
        pin_mask |= 1u << UART_RX_PIN;
        TIKU_C5_REG_WRITE(GPIO_MUX(UART_RX_PIN), (saved_rx_mux & ~((7u << 12) | (1u << 7))) |
                          (1u << 9) | (1u << 8));
        TIKU_C5_REG_WRITE(UART_RX_ROUTE, saved_rx_route & ~(1u << 8));
        TIKU_C5_REG_WRITE(GPIO_ENABLE + 8u, 1u << UART_RX_PIN);
        TIKU_C5_REG_WRITE(GPIO_ENABLE + 4u, 1u << UART_TX_PIN);
        TIKU_C5_REG_WRITE(GPIO_MUX(UART_TX_PIN), saved_tx_mux & ~(7u << 12));
    }
    head = tail = 0;
    counters = (tiku_c5_uart_stats_t){0};
    TIKU_C5_REG_WRITE(UART_CLEAR, 0xFFFFFu);
    ready = 1;
    TIKU_C5_REG_WRITE(UART_ENABLE, UART_INTERRUPTS);
    if (tiku_c5_irq_enable(UART_IRQ_LINE, 1) != 0) { goto failed; }
    TIKU_C5_IRQ_RESTORE(state);
    return 0;
failed:
    tiku_c5_uart_stop();
    TIKU_C5_IRQ_RESTORE(state);
    return -1;
}

int tiku_c5_uart_ready(void) { return ready; }
void tiku_uart_init(void) { (void)tiku_c5_uart_start(TIKU_BOARD_UART_BAUD, 0); }

void tiku_uart_putc(char byte)
{
    unsigned n;
    uint32_t state;
    /* The wait leaves the caller's interrupt mask unchanged. FIFO insertion
     * is atomic with its capacity check against another execution context. */
    for (n = 0; ready && n < UART_TX_SPINS; n++) {
        state = TIKU_C5_IRQ_SAVE();
        if (((TIKU_C5_REG_READ(UART_STATUS) >> 16) & 255u) < UART_FIFO_SIZE) {
            TIKU_C5_REG_WRITE(UART_FIFO, (uint8_t)byte);
            TIKU_C5_IRQ_RESTORE(state);
            return;
        }
        TIKU_C5_IRQ_RESTORE(state);
    }
    state = TIKU_C5_IRQ_SAVE();
    if (counters.tx_dropped != UINT32_MAX) { counters.tx_dropped++; }
    TIKU_C5_IRQ_RESTORE(state);
}
uint8_t tiku_uart_rx_ready(void)
{
    uint8_t result;
    uint32_t state = TIKU_C5_IRQ_SAVE();
    receive();
    result = ready && head != tail;
    TIKU_C5_IRQ_RESTORE(state);
    return result;
}
int tiku_uart_getc(void)
{
    int result = -1;
    uint32_t state = TIKU_C5_IRQ_SAVE();
    receive();
    if (ready && head != tail) { result = bytes[tail]; tail = (tail + 1u) & UART_RING_MASK; }
    TIKU_C5_IRQ_RESTORE(state);
    return result;
}
uint16_t tiku_uart_overrun_count(void)
{
    uint16_t result;
    uint32_t state = TIKU_C5_IRQ_SAVE();
    receive();
    result = counters.overruns;
    TIKU_C5_IRQ_RESTORE(state);
    return result;
}
void tiku_uart_overrun_reset(void)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    counters.overruns = 0;
    TIKU_C5_IRQ_RESTORE(state);
}
void tiku_c5_uart_stats(tiku_c5_uart_stats_t *out)
{
    uint32_t state = TIKU_C5_IRQ_SAVE();
    if (out != NULL) { *out = counters; }
    TIKU_C5_IRQ_RESTORE(state);
}
