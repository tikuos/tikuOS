/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * xip_head.ld - ESP32-C61 XIP image header, first in the XIP window.
 *
 * tiku_esp32c61_xip_ok() reads it to tell an xip.bin left by another build
 * apart before any call into it.  The Makefile includes this fragment ahead
 * of every other XIP fragment.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

.xip.head :
{
    KEEP(*(.xip.header))
    . = ALIGN(4);
} > XIP
