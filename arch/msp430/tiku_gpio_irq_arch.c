/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.c - MSP430 GPIO interrupt to event bridge.
 *
 * Each port has one vector for its eight pins: the ISR reads PxIV, which
 * returns the highest-priority pending pin and clears its flag, and posts one
 * event per pin.  Ports 1-4 are served, each where PORTn_VECTOR is defined.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>
#include <hal/tiku_compiler.h>
#include <interfaces/gpio/tiku_gpio.h>
#include <kernel/process/tiku_process.h>
#include <kernel/vfs/tree/tiku_vfs_tree_gpio.h>
#include <msp430.h>

/*---------------------------------------------------------------------------*/
/* PER-PORT REGISTER ACCESS                                                  */
/*---------------------------------------------------------------------------*/

/*
 * Pointer table keyed by 1-based port index. Each entry collects
 * the IE / IES / IFG / IV registers for one port. Ports not
 * present on the active device get a NULL entry and are
 * rejected up front.
 */
typedef struct {
    volatile uint8_t      *ie;     /* PxIE: interrupt enable */
    volatile uint8_t      *ies;    /* PxIES: 0 rising, 1 falling */
    volatile uint8_t      *ifg;    /* PxIFG: pending flags */
    volatile unsigned int *iv;     /* PxIV: top pending pin, read clears */
} gpio_irq_port_t;

/* Highest port number with interrupt support here. */
#define GPIO_IRQ_MAX_PORT 4

/*
 * Each entry is gated on PORTn_VECTOR, the macro the ISRs below also use.
 * PxIE and its siblings are sfr_b() symbols, not macros, so defined(P1IE)
 * is false under msp430-elf-gcc even where the port has interrupts, and a
 * guard on it would leave every entry NULL.  Taking &P1IE inside the
 * branch needs only the symbol.
 */
static const gpio_irq_port_t gpio_irq_ports[GPIO_IRQ_MAX_PORT + 1] = {
    [0] = { 0, 0, 0, 0 },        /* port 0 not used */
#if defined(PORT1_VECTOR)
    [1] = { &P1IE, &P1IES, &P1IFG, &P1IV },
#else
    [1] = { 0, 0, 0, 0 },
#endif
#if defined(PORT2_VECTOR)
    [2] = { &P2IE, &P2IES, &P2IFG, &P2IV },
#else
    [2] = { 0, 0, 0, 0 },
#endif
#if defined(PORT3_VECTOR)
    [3] = { &P3IE, &P3IES, &P3IFG, &P3IV },
#else
    [3] = { 0, 0, 0, 0 },
#endif
#if defined(PORT4_VECTOR)
    [4] = { &P4IE, &P4IES, &P4IFG, &P4IV },
#else
    [4] = { 0, 0, 0, 0 },
#endif
};

/* Per-pin "trigger on both edges" flag. When set, the ISR
 * toggles PxIES before returning so the next opposite edge also
 * fires. One bit per pin per port; port 0 unused. */
static uint8_t gpio_irq_both[GPIO_IRQ_MAX_PORT + 1];

/*---------------------------------------------------------------------------*/
/* PUBLIC HAL ENTRY POINTS                                                   */
/*---------------------------------------------------------------------------*/

int
tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin,
                          tiku_gpio_edge_t edge)
{
    const gpio_irq_port_t *p;
    uint8_t mask;

    if (port == 0 || port > GPIO_IRQ_MAX_PORT || pin > 7) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    p = &gpio_irq_ports[port];
    if (p->ie == 0) {
        return TIKU_GPIO_IRQ_ERR_UNSUP;
    }
    mask = (uint8_t)(1u << pin);

    /* The pin becomes an input with its pull-up on (tiku_gpio_dir_in()); a
     * button to ground then gives a falling edge. */
    if (tiku_gpio_dir_in(port, pin) != TIKU_GPIO_OK) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }

    /* Mask the pin across the reconfigure */
    *p->ie &= (uint8_t)~mask;

    /* Edge select: PxIES bit = 1 -> falling, 0 -> rising. The
     * "both" mode starts on the falling edge and the ISR flips
     * IES on each fire so the next opposite edge fires too. */
    if (edge == TIKU_GPIO_EDGE_FALLING || edge == TIKU_GPIO_EDGE_BOTH) {
        *p->ies |= mask;
    } else {
        *p->ies &= (uint8_t)~mask;
    }

    if (edge == TIKU_GPIO_EDGE_BOTH) {
        gpio_irq_both[port] |= mask;
    } else {
        gpio_irq_both[port] &= (uint8_t)~mask;
    }

    /* Clear any spurious flag that may have latched while IES
     * was being touched, then unmask. */
    *p->ifg &= (uint8_t)~mask;
    *p->ie  |= mask;

    return TIKU_GPIO_IRQ_OK;
}

int
tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin)
{
    const gpio_irq_port_t *p;
    uint8_t mask;

    if (port == 0 || port > GPIO_IRQ_MAX_PORT || pin > 7) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    p = &gpio_irq_ports[port];
    if (p->ie == 0) {
        return TIKU_GPIO_IRQ_ERR_UNSUP;
    }
    mask = (uint8_t)(1u << pin);

    *p->ie  &= (uint8_t)~mask;
    *p->ifg &= (uint8_t)~mask;
    gpio_irq_both[port] &= (uint8_t)~mask;

    return TIKU_GPIO_IRQ_OK;
}

/*---------------------------------------------------------------------------*/
/* SHARED ISR BODY                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Post one TIKU_EVENT_GPIO for each pending pin of @p port.
 *
 * Reads PxIV until it returns 0: each read returns (pin + 1) * 2 for the
 * highest-priority pending pin and clears that pin's flag.
 */
static inline void
gpio_irq_dispatch(uint8_t port)
{
    const gpio_irq_port_t *p = &gpio_irq_ports[port];
    unsigned int iv;

    while ((iv = *p->iv) != 0) {
        /* iv is 2*(pin+1); pin = iv/2 - 1 */
        uint8_t pin  = (uint8_t)((iv >> 1) - 1u);
        uint8_t mask = (uint8_t)(1u << pin);

        /* For "both" pins, flip IES so the opposite edge fires
         * next. The flag was already cleared by reading PxIV. */
        if (gpio_irq_both[port] & mask) {
            *p->ies ^= mask;
        }

        tiku_process_post(TIKU_PROCESS_BROADCAST,
                          TIKU_EVENT_GPIO,
                          (tiku_event_data_t)
                              TIKU_GPIO_IRQ_PACK(port, pin));

        /* Also notify the watchers of /dev/gpio/<port>/<pin>, so a rule
         * or `watch` on the node sees the edge. */
        tiku_vfs_tree_gpio_notify(port, pin);
    }
}

/*---------------------------------------------------------------------------*/
/* PORT ISRs                                                                 */
/*---------------------------------------------------------------------------*/

#if defined(PORT1_VECTOR)
TIKU_ISR(PORT1_VECTOR, tiku_gpio_irq_port1_isr)
{
    gpio_irq_dispatch(1);
}
#endif

#if defined(PORT2_VECTOR)
TIKU_ISR(PORT2_VECTOR, tiku_gpio_irq_port2_isr)
{
    gpio_irq_dispatch(2);
}
#endif

#if defined(PORT3_VECTOR)
TIKU_ISR(PORT3_VECTOR, tiku_gpio_irq_port3_isr)
{
    gpio_irq_dispatch(3);
}
#endif

#if defined(PORT4_VECTOR)
TIKU_ISR(PORT4_VECTOR, tiku_gpio_irq_port4_isr)
{
    gpio_irq_dispatch(4);
}
#endif
