/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_platform.h - MSP430 platform header.
 *
 * Pulls in the MSP430 device and board selection and the arch entry points the
 * portable layers expect on this platform.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #ifndef TIKU_PLATFORM_H
 #define TIKU_PLATFORM_H

 #include <tiku.h>

 /*
  * Device-specific definitions are now provided by the device/board
  * headers selected via tiku_device_select.h (included from tiku.h).
  * The old TIKU_PLATFORM_MSP430_FR5969 define has been replaced by
  * TIKU_DEVICE_MSP430FR5969 in tiku.h.
  */

#endif