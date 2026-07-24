/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpio_irq_arch.c - nRF54L GPIO edge interrupts (GPIOTE)
 *
 * Bridges the platform-agnostic (port, pin, edge) request onto the nRF54L
 * GPIOTE peripheral and broadcasts TIKU_EVENT_GPIO on each matching edge.
 *
 * nRF54L15 splits GPIO across two power domains, each served by its own
 * GPIOTE instance:
 *
 *     P0        (LP / always-on domain)  -> GPIOTE30, IRQ line 0 = 268
 *     P1, P2    (main peripheral domain) -> GPIOTE20, IRQ line 0 = 218
 *
 * Each GPIOTE instance owns 8 event channels.  A channel's CONFIG register
 * carries MODE=Event, the pin (PSEL) and port (PORT, a 4-bit field that lets
 * one instance address several ports in its domain), and the edge POLARITY
 * (LoToHi / HiToLo / Toggle -- Toggle gives "both edges" in hardware, so
 * TIKU_GPIO_EDGE_BOTH needs no software IES flipping).  A firing channel
 * latches EVENTS_IN[n]; with its INTENSET0 bit set that raises IRQ line 0,
 * whose ISR decodes the port/pin back out of CONFIG and posts the event.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_gpio_irq_hal.h>
#include <arch/nordic/tiku_device_select.h>   /* MDK types + NRF_GPIOTExx_S      */
#include <arch/nordic/tiku_nordic_core.h>     /* NVIC helpers                    */
#include <arch/nordic/tiku_gpio_arch.h>       /* tiku_nordic_gpio_init_input_*  */
#include <kernel/process/tiku_process.h>      /* tiku_process_post, event ids   */
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* GPIOTE register encoding                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Event channels per GPIOTE instance (CONFIG[0..7]). */
#define TIKU_GPIOTE_NCH         8u

/* CONFIG field layout (nrf54l15_types.h: GPIOTE_CONFIG_*). */
#define TIKU_GPIOTE_CFG_MODE_EVENT   (1UL << 0)   /* MODE = Event                */
#define TIKU_GPIOTE_CFG_PSEL_Pos     4u           /* pin  (5 bits)               */
#define TIKU_GPIOTE_CFG_PSEL_Msk     0x1Fu
#define TIKU_GPIOTE_CFG_PORT_Pos     9u           /* port (4 bits)               */
#define TIKU_GPIOTE_CFG_PORT_Msk     0xFu
#define TIKU_GPIOTE_CFG_POL_Pos      16u          /* polarity (2 bits)           */
#define TIKU_GPIOTE_POL_LOTOHI       1UL          /* rising                      */
#define TIKU_GPIOTE_POL_HITOLO       2UL          /* falling                     */
#define TIKU_GPIOTE_POL_TOGGLE       3UL          /* either edge                 */

/*---------------------------------------------------------------------------*/
/* Per-instance state                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief One GPIOTE instance and its software channel-allocation bookkeeping.
 *
 * @p used marks which of the 8 event channels are currently armed so enable()
 * can find a free slot (or reuse a pin's existing one) and the ISR can ignore
 * spurious latches on unallocated channels.
 */
typedef struct {
    NRF_GPIOTE_Type *reg;                 /**< GPIOTE register block            */
    int32_t          irqn;                /**< IRQ-line-0 number (MDK enum)     */
    uint8_t          used[TIKU_GPIOTE_NCH];/**< 1 = channel armed               */
} gpiote_ctx_t;

static gpiote_ctx_t s_gpiote20 = { NRF_GPIOTE20_S, 218, { 0 } }; /* P1, P2, P3 */
static gpiote_ctx_t s_gpiote30 = { NRF_GPIOTE30_S, 268, { 0 } }; /* P0         */

/**
 * @brief Select the GPIOTE instance that services a given physical port.
 * @param port  Physical port number (0/1/2/3 == P0/P1/P2/P3).
 * @return Pointer to the owning instance context, or NULL for an unknown port.
 *
 * P0 is in the LP / always-on domain (GPIOTE30); P1/P2/P3 are in the main
 * peripheral domain (GPIOTE20).  P3 exists only on the nRF54LM20A; verify its
 * GPIOTE20 reachability on-device via the CONFIG.PORT readback probe, the same
 * way P1/P2 were confirmed on the nRF54L15.
 */
static gpiote_ctx_t *ctx_for_port(uint8_t port)
{
    if (port == 0u) {
        return &s_gpiote30;
    }
    if (port == 1u || port == 2u || port == 3u) {
        return &s_gpiote20;
    }
    return (gpiote_ctx_t *)0;
}

/**
 * @brief Translate the HAL edge selector to a GPIOTE POLARITY value.
 * @param edge  Edge polarity selector (@c tiku_gpio_edge_t).
 * @return LoToHi / HiToLo / Toggle, or 0 for an unrecognised selector.
 */
static uint32_t edge_to_polarity(tiku_gpio_edge_t edge)
{
    switch (edge) {
    case TIKU_GPIO_EDGE_RISING:  return TIKU_GPIOTE_POL_LOTOHI;
    case TIKU_GPIO_EDGE_FALLING: return TIKU_GPIOTE_POL_HITOLO;
    case TIKU_GPIO_EDGE_BOTH:    return TIKU_GPIOTE_POL_TOGGLE;
    default:                     return 0UL;
    }
}

/**
 * @brief Find the channel already armed for (port, pin) on an instance.
 * @return Channel index 0..7, or -1 if the pin is not currently armed.
 */
static int find_channel(gpiote_ctx_t *c, uint8_t port, uint8_t pin)
{
    uint8_t ch;

    for (ch = 0u; ch < TIKU_GPIOTE_NCH; ch++) {
        if (c->used[ch] != 0u) {
            uint32_t cfg  = c->reg->CONFIG[ch];
            uint8_t  cpin = (uint8_t)((cfg >> TIKU_GPIOTE_CFG_PSEL_Pos)
                                      & TIKU_GPIOTE_CFG_PSEL_Msk);
            uint8_t  cprt = (uint8_t)((cfg >> TIKU_GPIOTE_CFG_PORT_Pos)
                                      & TIKU_GPIOTE_CFG_PORT_Msk);
            if (cpin == pin && cprt == port) {
                return (int)ch;
            }
        }
    }
    return -1;
}

/**
 * @brief Find a free event channel on an instance.
 * @return Channel index 0..7, or -1 when all 8 are in use.
 */
static int alloc_channel(gpiote_ctx_t *c)
{
    uint8_t ch;

    for (ch = 0u; ch < TIKU_GPIOTE_NCH; ch++) {
        if (c->used[ch] == 0u) {
            return (int)ch;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------*/
/* HAL API                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Enable an edge-triggered interrupt on the given (port, pin, edge).
 *
 * Configures the pin as an input with pull-up, allocates (or reuses) a GPIOTE
 * event channel on the port's owning instance, programs its CONFIG for the
 * requested edge, clears any stale latch, unmasks the channel on IRQ line 0,
 * and enables that line in the NVIC.  Subsequent matching edges broadcast
 * TIKU_EVENT_GPIO with data = TIKU_GPIO_IRQ_PACK(port, pin).
 *
 * @param port  Virtual port number (1=P0, 2=P1, 3=P2).
 * @param pin   Pin index within the port (0..31).
 * @param edge  Edge polarity to arm.
 * @return TIKU_GPIO_IRQ_OK, TIKU_GPIO_IRQ_ERR_INVALID for a bad
 *         port/pin/edge, or TIKU_GPIO_IRQ_ERR_UNSUP if the instance has no
 *         free channel left.
 */
int tiku_gpio_irq_arch_enable(uint8_t port, uint8_t pin,
                              tiku_gpio_edge_t edge)
{
    gpiote_ctx_t *c;
    uint8_t       phys;
    uint32_t      pol;
    int           ch;

    /* The GPIO API is 1-based virtual (1=P0, 2=P1, 3=P2), matching
     * tiku_gpio_arch.c; translate to the physical (0-based) port here. */
    if (port < 1u || port > 3u) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    phys = (uint8_t)(port - 1u);

    c = ctx_for_port(phys);
    if (c == (gpiote_ctx_t *)0 || pin > TIKU_GPIOTE_CFG_PSEL_Msk) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    pol = edge_to_polarity(edge);
    if (pol == 0UL) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }

    /* Idle-high input so a button (active-low) reads 1 until pressed. */
    tiku_nordic_gpio_init_input_pullup(phys, pin);

    ch = find_channel(c, phys, pin);      /* re-arm in place if already set */
    if (ch < 0) {
        ch = alloc_channel(c);
        if (ch < 0) {
            return TIKU_GPIO_IRQ_ERR_UNSUP;   /* all 8 channels occupied */
        }
    }

    c->reg->CONFIG[ch] = TIKU_GPIOTE_CFG_MODE_EVENT
                       | ((uint32_t)pin  << TIKU_GPIOTE_CFG_PSEL_Pos)
                       | ((uint32_t)phys << TIKU_GPIOTE_CFG_PORT_Pos)
                       | (pol            << TIKU_GPIOTE_CFG_POL_Pos);
    c->reg->EVENTS_IN[ch] = 0UL;
    (void)c->reg->EVENTS_IN[ch];              /* flush the clear */
    c->used[ch] = 1u;
    c->reg->INTENSET0 = (1UL << ch);

    tiku_nordic_nvic_clear_pending(c->irqn);
    tiku_nordic_nvic_set_priority(c->irqn, 3u);
    tiku_nordic_nvic_enable(c->irqn);

    return TIKU_GPIO_IRQ_OK;
}

/**
 * @brief Mask a pin's edge interrupt and free its channel.
 *
 * Masks the channel on IRQ line 0, disables the CONFIG (MODE=Disabled),
 * clears any pending latch, and releases the channel.  The pin's direction
 * and pull are left as-is so the app can still read the line afterwards.
 * Disabling a pin that was never armed is a successful no-op.
 *
 * @param port  Virtual port number (1=P0, 2=P1, 3=P2).
 * @param pin   Pin index within the port.
 * @return TIKU_GPIO_IRQ_OK, or TIKU_GPIO_IRQ_ERR_INVALID for a bad port.
 */
int tiku_gpio_irq_arch_disable(uint8_t port, uint8_t pin)
{
    gpiote_ctx_t *c;
    uint8_t       phys;
    int           ch;

    if (port < 1u || port > 3u) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    phys = (uint8_t)(port - 1u);

    c = ctx_for_port(phys);
    if (c == (gpiote_ctx_t *)0) {
        return TIKU_GPIO_IRQ_ERR_INVALID;
    }
    ch = find_channel(c, phys, pin);
    if (ch < 0) {
        return TIKU_GPIO_IRQ_OK;              /* not armed: nothing to do */
    }

    c->reg->INTENCLR0     = (1UL << ch);
    c->reg->CONFIG[ch]    = 0UL;              /* MODE = Disabled */
    c->reg->EVENTS_IN[ch] = 0UL;
    c->used[ch]           = 0u;
    return TIKU_GPIO_IRQ_OK;
}

/*---------------------------------------------------------------------------*/
/* IRQ handlers                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Shared GPIOTE ISR body: post one event per fired channel.
 *
 * Walks the instance's 8 event channels; for each latched EVENTS_IN it clears
 * the latch, decodes the (port, pin) back out of the channel's CONFIG, and
 * broadcasts TIKU_EVENT_GPIO to every registered process.  Runs in ISR
 * context at NVIC priority 3.
 *
 * @param c  The GPIOTE instance whose line fired.
 */
static void gpiote_isr(gpiote_ctx_t *c)
{
    uint8_t ch;

    for (ch = 0u; ch < TIKU_GPIOTE_NCH; ch++) {
        if (c->reg->EVENTS_IN[ch] == 0UL) {
            continue;
        }
        c->reg->EVENTS_IN[ch] = 0UL;
        (void)c->reg->EVENTS_IN[ch];          /* flush the clear */

        if (c->used[ch] != 0u) {
            uint32_t cfg  = c->reg->CONFIG[ch];
            uint8_t  pin  = (uint8_t)((cfg >> TIKU_GPIOTE_CFG_PSEL_Pos)
                                      & TIKU_GPIOTE_CFG_PSEL_Msk);
            uint8_t  phys = (uint8_t)((cfg >> TIKU_GPIOTE_CFG_PORT_Pos)
                                      & TIKU_GPIOTE_CFG_PORT_Msk);
            /* Report the 1-based virtual port the caller armed with. */
            tiku_event_data_t data = (tiku_event_data_t)
                TIKU_GPIO_IRQ_PACK((uint8_t)(phys + 1u), pin);

            tiku_process_post(TIKU_PROCESS_BROADCAST, TIKU_EVENT_GPIO, data);
        }
    }
}

/**
 * @brief GPIOTE20 IRQ-line-0 ISR (P1/P2).  Overrides the crt weak alias
 *        wired at IRQ 218.
 */
void tiku_nordic_gpiote20_isr(void)
{
    gpiote_isr(&s_gpiote20);
}

/**
 * @brief GPIOTE30 IRQ-line-0 ISR (P0).  Overrides the crt weak alias wired
 *        at IRQ 268.
 */
void tiku_nordic_gpiote30_isr(void)
{
    gpiote_isr(&s_gpiote30);
}
