/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usb_serial_jtag_hal.h - Native USB Serial/JTAG FIFO register selection.
 *
 * Maps the TIKU_JTAG_* register and bit names onto the ESP32-P4 or ESP32-C5
 * register set; any other platform fails the build.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_USB_SERIAL_JTAG_HAL_H_
#define TIKU_USB_SERIAL_JTAG_HAL_H_
#if defined(PLATFORM_ESP32P4)
#include <arch/esp32p4/tiku_esp32p4_regs.h>
#define TIKU_JTAG_READ TIKU_P4_REG_READ
#define TIKU_JTAG_WRITE TIKU_P4_REG_WRITE
#define TIKU_JTAG_EP1 TIKU_P4_USB_EP1
#define TIKU_JTAG_EP1_CONF TIKU_P4_USB_EP1_CONF
#define TIKU_JTAG_INT_ENA TIKU_P4_USB_INT_ENA
#define TIKU_JTAG_FRAM_NUM TIKU_P4_USB_FRAM_NUM
#define TIKU_JTAG_WR_DONE TIKU_P4_USB_WR_DONE
#define TIKU_JTAG_TX_FREE TIKU_P4_USB_TX_FREE
#define TIKU_JTAG_RX_AVAIL TIKU_P4_USB_RX_AVAIL
#define TIKU_JTAG_PACKET_SIZE TIKU_P4_USB_PACKET_SIZE
#elif defined(PLATFORM_ESP32C5)
#include <arch/esp32c5/tiku_esp32c5_regs.h>
#define TIKU_JTAG_READ TIKU_C5_REG_READ
#define TIKU_JTAG_WRITE TIKU_C5_REG_WRITE
#define TIKU_JTAG_EP1 TIKU_C5_USB_EP1
#define TIKU_JTAG_EP1_CONF TIKU_C5_USB_EP1_CONF
#define TIKU_JTAG_INT_ENA TIKU_C5_USB_INT_ENA
#define TIKU_JTAG_FRAM_NUM TIKU_C5_USB_FRAM_NUM
#define TIKU_JTAG_WR_DONE TIKU_C5_USB_WR_DONE
#define TIKU_JTAG_TX_FREE TIKU_C5_USB_TX_FREE
#define TIKU_JTAG_RX_AVAIL TIKU_C5_USB_RX_AVAIL
#define TIKU_JTAG_PACKET_SIZE TIKU_C5_USB_PACKET_SIZE
#else
#error Native USB Serial/JTAG requires ESP32-C5 or ESP32-P4
#endif
#endif
