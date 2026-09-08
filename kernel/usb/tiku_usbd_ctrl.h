/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usbd_ctrl.h - control transfers, descriptors and class requests,
 * with no controller in them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_USBD_CTRL_H_
#define TIKU_USBD_CTRL_H_

#include <stdint.h>
#include <stddef.h>

/*
 * What is here and what is deliberately not, in the mass-storage core's own
 * terms.  Here: every DECISION a device makes about a control transfer --
 * which descriptor answers, what a status or feature request means, how a
 * class request is routed -- and the descriptors themselves, built once
 * from an identity and endpoint numbers instead of hand-typed per port.
 * Not here: WHEN the address takes effect (before the status stage on a
 * DWC2, after it on MUSB and the RP2350, in hardware on the RA8P1), how a
 * data stage moves, or any register.  A controller drives its own EP0 and
 * asks here what each SETUP packet means.
 */

/*---------------------------------------------------------------------------*/
/* IDENTITY                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief pid.codes vendor, and one product per face. */
#define TIKU_USBD_VID           0x1209u
#define TIKU_USBD_PID_CONSOLE   0x0001u
#define TIKU_USBD_PID_DISK      0x0002u

/** @brief The faces a device can wear; routes class requests. */
#define TIKU_USBD_CLASS_CDC     1u
#define TIKU_USBD_CLASS_MSC     2u

/*---------------------------------------------------------------------------*/
/* DESCRIPTOR BUILDERS                                                       */
/*---------------------------------------------------------------------------*/

#define TIKU_USBD_DEVICE_LEN        18u
#define TIKU_USBD_CDC_CONFIG_LEN    75u   /* with the association descriptor */
#define TIKU_USBD_MSC_CONFIG_LEN    32u
#define TIKU_USBD_STRING_MAX        64u
#define TIKU_USBD_SERIAL_LEN(n)     (2u + 4u * (n))

/**
 * @brief The 18-byte device descriptor.
 * @param out       at least TIKU_USBD_DEVICE_LEN bytes
 * @param pid       product id (TIKU_USBD_PID_*)
 * @param composite non-zero for a device whose class lives in an interface
 *                  association (CDC); zero puts the class at the interface
 *                  alone (MSC)
 * @param ep0_mps   EP0 packet size, 64 on every port here
 */
void tiku_usbd_device_desc(uint8_t *out, uint16_t pid, int composite,
                           uint8_t ep0_mps);

/**
 * @brief A CDC-ACM configuration: association, communications interface
 *        with its four class descriptors and interrupt endpoint, data
 *        interface with two bulk endpoints.
 * @param out       at least TIKU_USBD_CDC_CONFIG_LEN bytes
 * @param ep_notify interrupt IN endpoint number
 * @param ep_out    bulk OUT endpoint number
 * @param ep_in     bulk IN endpoint number
 * @param bulk_mps  bulk packet size as first advertised (64 below high
 *                  speed; tiku_usbd_desc_set_bulk_mps() rewrites it once the
 *                  speed is known)
 */
void tiku_usbd_cdc_config(uint8_t *out, uint8_t ep_notify, uint8_t ep_out,
                          uint8_t ep_in, uint16_t bulk_mps);

/** @brief A mass-storage configuration: one interface, two bulk endpoints. */
void tiku_usbd_msc_config(uint8_t *out, uint8_t ep_out, uint8_t ep_in,
                          uint16_t bulk_mps);

/**
 * @brief Rewrite wMaxPacketSize in every BULK endpoint descriptor of a
 *        configuration: 512 once high speed is negotiated, 64 below it.
 *        A host controller sends the speed's size whatever the descriptor
 *        says, and a packet wider than the endpoint is babble never taken.
 */
void tiku_usbd_desc_set_bulk_mps(uint8_t *config, uint16_t len,
                                 uint16_t mps);

/** @brief The language descriptor (US English), 4 bytes. */
const uint8_t *tiku_usbd_string_lang(uint16_t *len);

/** @brief An ASCII string as a UTF-16LE string descriptor.  Returns bytes. */
uint16_t tiku_usbd_string_ascii(uint8_t *out, size_t cap, const char *s);

/** @brief A hex serial descriptor from @p n bytes of id (a FICR, a flash id). */
uint16_t tiku_usbd_string_serial(uint8_t *out, size_t cap,
                                 const uint8_t *id, uint8_t n);

/*---------------------------------------------------------------------------*/
/* THE DESCRIPTOR SET A DEVICE PRESENTS                                      */
/*---------------------------------------------------------------------------*/

typedef struct {
    const uint8_t *device;        /**< 18 bytes                             */
    const uint8_t *config;        /**< the configuration, patched by speed  */
    uint16_t       config_len;
    const uint8_t *string[4];     /**< lang, manufacturer, product, serial  */
    uint16_t       string_len[4];
    uint8_t        klass;         /**< TIKU_USBD_CLASS_*: routes class reqs */
    uint8_t        self_powered;  /**< GET_STATUS bit 0                     */
} tiku_usbd_desc_set_t;

/*---------------------------------------------------------------------------*/
/* CONTROL-TRANSFER DECISIONS                                                */
/*---------------------------------------------------------------------------*/

/** @brief What the controller does with EP0 next. */
typedef enum {
    TIKU_USBD_CTRL_STALL = 0,   /**< stall both directions of EP0           */
    TIKU_USBD_CTRL_STATUS,      /**< no data: send the zero-length status   */
    TIKU_USBD_CTRL_REPLY,       /**< send @c data/@c len (already clamped)  */
    TIKU_USBD_CTRL_ACCEPT_OUT   /**< take @c len bytes from the host, then
                                 *   send the status; the bytes are ignored  */
} tiku_usbd_ctrl_action_t;

/** @brief Side effects the request asks for, beyond the EP0 traffic. */
#define TIKU_USBD_FX_ADDRESS    (1u << 0)  /**< apply @c address (your timing) */
#define TIKU_USBD_FX_CONFIG     (1u << 1)  /**< configuration @c config chosen */
#define TIKU_USBD_FX_HALT_EP    (1u << 2)  /**< halt endpoint @c ep            */
#define TIKU_USBD_FX_UNHALT_EP  (1u << 3)  /**< clear the halt on @c ep, and
                                            *   its data toggle with it       */
#define TIKU_USBD_FX_LINE_STATE (1u << 4)  /**< CDC: @c dtr changed           */
#define TIKU_USBD_FX_CLASS_RESET (1u << 5) /**< MSC: bulk-only reset          */

typedef struct {
    tiku_usbd_ctrl_action_t action;
    const uint8_t *data;
    uint16_t       len;
    uint8_t        effects;       /**< TIKU_USBD_FX_* bits                  */
    uint8_t        address;
    uint8_t        config;
    uint8_t        ep;            /**< endpoint address for halt/unhalt     */
    uint8_t        dtr;
} tiku_usbd_ctrl_out_t;

/** @brief The device's control state; one per device, zeroed to start. */
typedef struct {
    const tiku_usbd_desc_set_t *set;
    uint8_t config;               /**< answers GET_CONFIGURATION            */
    uint8_t dtr;                  /**< CDC line state, bit 0                */
    uint8_t reply[8];             /**< scratch for one-byte/two-byte answers */
} tiku_usbd_ctrl_t;

/** @brief Bind a descriptor set; also what a bus reset calls. */
void tiku_usbd_ctrl_init(tiku_usbd_ctrl_t *c, const tiku_usbd_desc_set_t *set);

/**
 * @brief Decide one SETUP packet.
 * @param c      the device's control state (updated: config, dtr)
 * @param setup8 the 8 bytes the host sent
 * @param out    the decision
 */
void tiku_usbd_ctrl_setup(tiku_usbd_ctrl_t *c, const uint8_t *setup8,
                          tiku_usbd_ctrl_out_t *out);

/** @brief The line coding every port reports: 115200 8N1, 7 bytes. */
const uint8_t *tiku_usbd_cdc_line_coding(void);

#endif /* TIKU_USBD_CTRL_H_ */
