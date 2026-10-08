/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic.c - Tiku BASIC interpreter engine (orchestrator).
 *
 * One translation unit amalgamated from themed .inl pieces by the includes
 * below: the statics stay private, the optimiser sees the whole engine, and
 * no piece needs a header.  Include order follows dependency.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_basic.h"
#include "tiku_basic_ext.h"           /* native builtin registry */
#include <shell/tiku_shell.h>  /* device NVM label, before the config */
#include "tiku_basic_config.h"
#include <kernel/memory/tiku_mem.h>
#include <kernel/memory/tiku_reclaim.h>
#include <kernel/timers/tiku_clock.h>
#include <hal/tiku_cpu.h>                /* SLEEP enters low-power idle */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* Hardware-bridge headers, each included only when its BASIC bridge is
 * enabled.  The bridges are platform-agnostic; the gpio interface header
 * includes the per-MCU arch header. */
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
#if defined(PLATFORM_ESP32C61)
#include <arch/esp32c61/tiku_cpu_common.h>
#endif
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
#include <tikukits/codec/json/tiku_kits_codec_json.h>  /* JSON$ */
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
#include <shell/tiku_shell_pump.h>          /* shared busy-wait pump */
#if (TIKU_KITS_NET_MQTT_ENABLE + 0)
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>   /* tcp_init (MQTT words) */
#include <tikukits/net/mqtt/tiku_kits_net_mqtt.h>  /* MQTTPUB */
#endif
#if (TIKU_KITS_NET_HTTP_ENABLE + 0)
/* HTTPGET$ runs over the http kit's certificate engine: TCP transport, DNS,
 * the X.509 trust store and the TLS 1.3 client (TLS 1.2 as the fallback). */
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>
#include <tikukits/net/ipv4/tiku_kits_net_dns.h>
#include <tikukits/net/tls/x509/tiku_kits_crypto_x509.h>
#include <tikukits/net/tls/tls13/tiku_kits_crypto_tls13.h>
#if (TIKU_DRV_WIFI_CYW43_ENABLE + 0) || (TIKU_DRV_WIFI_ESP_ENABLE + 0)
#include <interfaces/wireless/tiku_wireless.h>     /* rx_poll in the pump  */
#endif
#if defined(TIKU_DRV_WIFI_CYW43_ENABLE) && TIKU_DRV_WIFI_CYW43_ENABLE
#include <arch/arm-rp2350/tiku_trng_arch.h>        /* TLS entropy          */
#endif
#endif
#endif
#if TIKU_BASIC_BLE_ENABLE
#include <interfaces/bluetooth/tiku_ble_serial.h>  /* serial-over-BLE */
#include <interfaces/bluetooth/tiku_ble_adv.h>     /* BLEBEACON/BLESCAN$ */
#endif

/*---------------------------------------------------------------------------*/
/* AMALGAMATION                                                              */
/*---------------------------------------------------------------------------*/

#include "tiku_basic_cursor.inl"      /* cursor ops, before all parsers */
#include "tiku_basic_state.inl"
#include "tiku_basic_reclaim_state.inl"
#include "tiku_basic_token.inl"       /* keyword crunch, before its users */
#include "tiku_basic_arena.inl"
#include "tiku_basic_persist.inl"
#include "tiku_basic_ckpt.inl"        /* checkpoint (needs arena + persist) */
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
#include "tiku_basic_ext.inl"         /* registry impl (needs parse_expr) */
#include "tiku_basic_ext_kits.inl"    /* native words bundled with BASIC */
#include "tiku_basic_module.h"        /* loadable native module ABI */
#include "tiku_basic_program.inl"
#include "tiku_basic_stmt.inl"
#include "tiku_basic_net.inl"
#include "tiku_basic_ble.inl"
#include "tiku_basic_subs.inl"
#include "tiku_basic_multi_if.inl"
#include "tiku_basic_select.inl"
#include "tiku_basic_renum.inl"
#include "tiku_basic_import.inl"      /* IMPORT: needs renum, subs, persist */
#include "tiku_basic_named_slots.inl"
#include "tiku_basic_dispatch.inl"
#include "tiku_basic_debug.inl"
#include "tiku_basic_run.inl"
#include "tiku_basic_repl.inl"
#include "tiku_basic_shell.inl"
#include "tiku_basic_mode.inl"
#include "tiku_basic_reclaim.inl"
