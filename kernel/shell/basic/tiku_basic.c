/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic.c - Tiku BASIC interpreter engine (orchestrator).
 *
 * The interpreter is treated as a complex extension of the kernel
 * shell rather than a single shell command.  It owns ~5 KB of
 * source split across 22 themed pieces (one .inl per concern), all
 * amalgamated into this single translation unit by the chain of
 * #include directives below.
 *
 * The amalgamation keeps file-static identifiers private to BASIC,
 * lets the LTO pass see across the whole engine, and avoids the
 * boilerplate of a separate header per piece.  The order of
 * #includes follows the natural dependency chain:
 *
 *   config / state                  - tunables, types, globals
 *   arena / persist / VFS bridge    - memory + FRAM persistence
 *   peek-poke / hardware / PRNG     - low-level helpers
 *   trig                            - SIN / COS LUT
 *   lex / I/O                       - lexical helpers, line reader
 *   string / call / expr            - expression parsers
 *   program                         - the line table
 *   stmt / multi-IF / RENUM / slots - statement executors
 *   dispatch                        - exec_if + the keyword switch
 *   run / repl / shell              - run loop + REPL + entry points
 *
 * The public API is declared in tiku_basic.h.  The shell command
 * `basic` lives in kernel/shell/commands/tiku_shell_cmd_basic.{c,h}
 * as a thin dispatch stub over these entry points.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_basic.h"
#include "tiku_basic_config.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/memory/tiku_mem.h>
#include <kernel/timers/tiku_clock.h>
#include <hal/tiku_cpu.h>                /* SLEEP -> real low-power idle */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* Hardware-bridge headers.  Each is pulled in only when the matching
 * BASIC bridge is enabled, so a slim BASIC build (e.g. no GPIO, no
 * I2C) doesn't drag in unused HAL code. */
/* The BASIC hardware-bridge code (tiku_basic_*.inl) is platform-agnostic, so
 * these declarations must be visible on every arch, not just MSP430. The gpio
 * interface header dispatches to the right per-MCU arch header internally. */
#if TIKU_BASIC_GPIO_ENABLE
#include <interfaces/gpio/tiku_gpio.h>
#endif
#if TIKU_BASIC_ADC_ENABLE
#include <interfaces/adc/tiku_adc.h>
#endif
#if TIKU_BASIC_I2C_ENABLE
#include <interfaces/bus/tiku_i2c_bus.h>
#endif
#if TIKU_BASIC_REBOOT_ENABLE
#include <kernel/cpu/tiku_watchdog.h>
#endif
#if TIKU_BASIC_LED_ENABLE
#include <interfaces/led/tiku_led.h>
#endif
#if TIKU_BASIC_VFS_ENABLE
#include <kernel/vfs/tiku_vfs.h>
#include <stdlib.h>     /* strtol for VFSREAD value parsing */
#endif
#if TIKU_BASIC_RTC_ENABLE
#include <kernel/cpu/tiku_rtc.h>          /* NOW / SETTIME wall-clock seconds */
#if (TIKU_KIT_TIME_ENABLE + 0)
#include <tikukits/time/tiku_kits_time.h> /* DATE$ / TIME$ calendar breakdown */
#endif
#endif
#if TIKU_BASIC_JSON_ENABLE
#include <tikukits/codec/json/tiku_kits_codec_json.h>  /* JSON$ path extractor */
#endif
#if TIKU_BASIC_CRYPTO_ENABLE
#include <tikukits/crypto/base64/tiku_kits_crypto_base64.h>  /* BASE64$ */
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>  /* SHA256$ */
#include <tikukits/crypto/hmac/tiku_kits_crypto_hmac.h>      /* HMAC$   */
#endif
#if TIKU_BASIC_NET_ENABLE
#include <tikukits/net/ipv4/tiku_kits_net_udp.h>   /* UDPSEND */
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>  /* IPADDR$ / NETUP */
#include <kernel/cpu/tiku_watchdog.h>              /* WDT kick (delay/wait) */
#include <kernel/shell/tiku_shell_pump.h>          /* shared busy-wait pump */
#if (TIKU_KITS_NET_MQTT_ENABLE + 0)
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>   /* tcp_init (MQTT words) */
#include <tikukits/net/mqtt/tiku_kits_net_mqtt.h>  /* MQTTPUB */
#endif
#if (TIKU_KITS_NET_HTTP_ENABLE + 0)
/* HTTPGET$ runs over the certificate-based TLS 1.3 client (not the PSK-only
 * http kit): TCP transport + DNS + X.509 trust store + the tls13 client. */
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>
#include <tikukits/net/ipv4/tiku_kits_net_dns.h>
#include <tikukits/net/tls/x509/tiku_kits_crypto_x509.h>
#include <tikukits/net/tls/tls13/tiku_kits_crypto_tls13.h>
#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
#include <drivers/wifi/cyw43/whd.h>                /* whd_drain_rx in pump */
#include <arch/arm-rp2350/tiku_trng_arch.h>        /* TLS entropy          */
#endif
#endif
#endif
#if TIKU_BASIC_BLE_ENABLE
#include <interfaces/bluetooth/tiku_ble_serial.h>  /* BLEADV/BLESEND/BLEUP/BLEGET$ */
#include <interfaces/bluetooth/tiku_ble_adv.h>     /* BLEBEACON/BLESCAN$ (broadcast) */
#endif

/*---------------------------------------------------------------------------*/
/* AMALGAMATION                                                              */
/*---------------------------------------------------------------------------*/

#include "tiku_basic_state.inl"
#include "tiku_basic_token.inl"       /* A2: keyword crunch (before all users) */
#include "tiku_basic_arena.inl"
#include "tiku_basic_persist.inl"
#include "tiku_basic_ckpt.inl"        /* F1: PERSIST / RUN RESUME (needs arena + persist) */
#include "tiku_basic_vfs_file.inl"
#include "tiku_basic_peek_poke.inl"
#include "tiku_basic_hw.inl"
#include "tiku_basic_prng.inl"
#include "tiku_basic_trig.inl"
#include "tiku_basic_mathx.inl"
#include "tiku_basic_lex.inl"
#include "tiku_basic_io.inl"
#include "tiku_basic_https.inl"
#include "tiku_basic_browse.inl"
#include "tiku_basic_string.inl"
#include "tiku_basic_call.inl"
#include "tiku_basic_expr.inl"
#include "tiku_basic_program.inl"
#include "tiku_basic_stmt.inl"
#include "tiku_basic_net.inl"
#include "tiku_basic_ble.inl"
#include "tiku_basic_subs.inl"
#include "tiku_basic_multi_if.inl"
#include "tiku_basic_select.inl"
#include "tiku_basic_renum.inl"
#include "tiku_basic_named_slots.inl"
#include "tiku_basic_dispatch.inl"
#include "tiku_basic_run.inl"
#include "tiku_basic_repl.inl"
#include "tiku_basic_shell.inl"
#include "tiku_basic_mode.inl"
