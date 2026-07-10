/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mqtt.c - "mqtt" command implementation (MQTT 3.1.1 client)
 *
 * Connects to an MQTT broker over the TCP stack and optionally publishes a
 * message (QoS 0).  The MQTT client is event-driven (callbacks) plus a
 * once-per-second periodic; this command wraps it in the shell's async tick
 * pattern: connect kicks off the TCP handshake + CONNECT, the tick paces
 * mqtt_periodic() and acts on the connection event (the shell loop already
 * drives tcp_periodic()).  All I/O flows through the shared SLIP demux, so the
 * shell stays interactive.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_mqtt.h"
#include "tiku_shell_cmd_slip.h"                  /* tiku_shell_cmd_slip_enable */
#include <kernel/shell/tiku_shell.h>              /* SHELL_PRINTF */
#include <kernel/timers/tiku_clock.h>             /* tiku_clock_time */
#include <tikukits/net/tiku_kits_net.h>           /* TIKU_KITS_NET_IP_ADDR */
#include <tikukits/net/mqtt/tiku_kits_net_mqtt.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* CONFIG + STATE                                                            */
/*---------------------------------------------------------------------------*/

#define MQTT_DEFAULT_PORT    1883u
#define MQTT_PERIODIC_EVERY  ((tiku_clock_time_t)TIKU_CLOCK_SECOND)
#define MQTT_DEADLINE        ((tiku_clock_time_t)(8u * TIKU_CLOCK_SECOND))
#define MQTT_TOPIC_MAX       40u
#define MQTT_PAYLOAD_MAX     64u

typedef enum { MQTT_PH_IDLE, MQTT_PH_BUSY } mqtt_phase_t;

static mqtt_phase_t      mqtt_phase;
static tiku_clock_time_t mqtt_t0;
static tiku_clock_time_t mqtt_last;
static volatile uint8_t  mqtt_evt;            /* last event from the callback */
static uint8_t           mqtt_do_pub;
static char              mqtt_topic[MQTT_TOPIC_MAX];
static char              mqtt_payload[MQTT_PAYLOAD_MAX];

/*---------------------------------------------------------------------------*/
/* HELPERS / CALLBACKS                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief MQTT event callback: latch the last event for the tick to act on.
 *
 * Invoked by the MQTT client on connection-state changes (CONNECTED,
 * ERROR, DISCONNECTED); stores the code in mqtt_evt so the shell tick
 * can act on it outside callback context.
 *
 * @param event  The TIKU_KITS_NET_MQTT_EVT_* code being reported.
 */
static void
mqtt_event_cb(uint8_t event)
{
    mqtt_evt = event;
}

static void
mqtt_msg_cb(const char *topic, uint16_t topic_len, const uint8_t *payload,
            uint16_t payload_len, uint8_t qos, uint8_t retain)
{
    (void)topic; (void)topic_len; (void)payload;
    (void)payload_len; (void)qos; (void)retain;
}

static uint8_t
mqtt_parse_ip(const char *s, uint8_t out[4])
{
    uint8_t i;

    for (i = 0; i < 4u; i++) {
        uint16_t v = 0;
        uint8_t  digits = 0;

        while (*s >= '0' && *s <= '9') {
            v = (uint16_t)(v * 10u + (uint16_t)(*s - '0'));
            if (v > 255u) {
                return 0;
            }
            s++;
            digits++;
        }
        if (digits == 0u) {
            return 0;
        }
        out[i] = (uint8_t)v;
        if (i < 3u) {
            if (*s != '.') {
                return 0;
            }
            s++;
        }
    }
    return (*s == '\0') ? 1u : 0u;
}

/**
 * @brief Parse a decimal string into a uint16_t.
 *
 * Consumes leading decimal digits and stops at the first non-digit.
 *
 * @param s  Decimal digit string.
 * @return   Parsed value (0 if no digits; wraps silently on overflow).
 */
static uint16_t
mqtt_parse_u16(const char *s)
{
    uint16_t v = 0;

    while (*s >= '0' && *s <= '9') {
        v = (uint16_t)(v * 10u + (uint16_t)(*s - '0'));
        s++;
    }
    return v;
}

/**
 * @brief Copy a C string into a fixed buffer, always NUL-terminating.
 *
 * Copies at most max-1 bytes from src, then writes a terminating NUL,
 * truncating if src is longer.
 *
 * @param dst  Destination buffer of at least max bytes.
 * @param src  Source C string.
 * @param max  Size of the destination buffer in bytes.
 */
static void
mqtt_copy(char *dst, const char *src, uint8_t max)
{
    uint8_t i = 0;

    while (src[i] != '\0' && i < (uint8_t)(max - 1u)) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/*---------------------------------------------------------------------------*/
/* COMMAND + TICK                                                            */
/*---------------------------------------------------------------------------*/

uint8_t
tiku_shell_cmd_mqtt_active(void)
{
    return (uint8_t)(mqtt_phase != MQTT_PH_IDLE);
}

void
tiku_shell_cmd_mqtt(uint8_t argc, const char *argv[])
{
    static const uint8_t self[4] = TIKU_KITS_NET_IP_ADDR;
    uint8_t  broker[4];
    uint16_t port = MQTT_DEFAULT_PORT;

    if (mqtt_phase != MQTT_PH_IDLE) {
        SHELL_PRINTF("mqtt already running\n");
        return;
    }

    /* Default broker: the SLIP host (subnet .1). */
    broker[0] = self[0];
    broker[1] = self[1];
    broker[2] = self[2];
    broker[3] = 1u;
    mqtt_do_pub = 0;

    if (argc >= 2 && strcmp(argv[1], "pub") == 0) {
        if (argc < 4) {
            SHELL_PRINTF("usage: mqtt pub <topic> <msg> [broker] [port]\n");
            return;
        }
        mqtt_copy(mqtt_topic, argv[2], MQTT_TOPIC_MAX);
        mqtt_copy(mqtt_payload, argv[3], MQTT_PAYLOAD_MAX);
        mqtt_do_pub = 1;
        if (argc >= 5 && !mqtt_parse_ip(argv[4], broker)) {
            SHELL_PRINTF("usage: mqtt pub <topic> <msg> [broker] [port]\n");
            return;
        }
        if (argc >= 6) {
            port = mqtt_parse_u16(argv[5]);
        }
    } else {
        if (argc >= 2 && !mqtt_parse_ip(argv[1], broker)) {
            SHELL_PRINTF("usage: mqtt [broker] [port] | "
                         "mqtt pub <topic> <msg> [broker] [port]\n");
            return;
        }
        if (argc >= 3) {
            port = mqtt_parse_u16(argv[2]);
        }
    }

    tiku_shell_cmd_slip_enable();
    tiku_kits_net_mqtt_init();
    tiku_kits_net_mqtt_set_server(broker, port);
    tiku_kits_net_mqtt_set_credentials("tikuos", 0, 0);

    mqtt_evt = 0;
    if (tiku_kits_net_mqtt_connect(mqtt_msg_cb, mqtt_event_cb)
            != TIKU_KITS_NET_OK) {
        SHELL_PRINTF("mqtt: connect failed\n");
        return;
    }

    mqtt_phase = MQTT_PH_BUSY;
    mqtt_t0    = tiku_clock_time();
    mqtt_last  = mqtt_t0;
    SHELL_PRINTF("MQTT connecting to %u.%u.%u.%u:%u ...\n",
                 broker[0], broker[1], broker[2], broker[3], port);
}

void
tiku_shell_cmd_mqtt_tick(void)
{
    if (mqtt_phase == MQTT_PH_IDLE) {
        return;
    }

    /* Pace the MQTT housekeeping (~1 Hz); the shell loop drives tcp_periodic. */
    if ((tiku_clock_time_t)(tiku_clock_time() - mqtt_last) >= MQTT_PERIODIC_EVERY) {
        mqtt_last = tiku_clock_time();
        tiku_kits_net_mqtt_periodic();
    }

    if (mqtt_evt == TIKU_KITS_NET_MQTT_EVT_CONNECTED) {
        mqtt_evt = 0;
        if (mqtt_do_pub) {
            if (tiku_kits_net_mqtt_publish(mqtt_topic,
                    (const uint8_t *)mqtt_payload,
                    (uint16_t)strlen(mqtt_payload), 0, 0) == TIKU_KITS_NET_OK) {
                SHELL_PRINTF("mqtt: published to %s\n", mqtt_topic);
            } else {
                SHELL_PRINTF("mqtt: publish failed\n");
            }
        } else {
            SHELL_PRINTF("mqtt: connected\n");
        }
        (void)tiku_kits_net_mqtt_disconnect();
        mqtt_phase = MQTT_PH_IDLE;
        return;
    }

    if (mqtt_evt == TIKU_KITS_NET_MQTT_EVT_ERROR ||
        mqtt_evt == TIKU_KITS_NET_MQTT_EVT_DISCONNECTED) {
        mqtt_evt = 0;
        SHELL_PRINTF("mqtt: connect failed\n");
        mqtt_phase = MQTT_PH_IDLE;
        return;
    }

    if ((tiku_clock_time_t)(tiku_clock_time() - mqtt_t0) >= MQTT_DEADLINE) {
        (void)tiku_kits_net_mqtt_disconnect();
        SHELL_PRINTF("mqtt: timeout (no CONNACK)\n");
        mqtt_phase = MQTT_PH_IDLE;
    }
}
