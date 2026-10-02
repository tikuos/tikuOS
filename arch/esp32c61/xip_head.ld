/*
 * xip_head.ld - the XIP image's header (ESP32-C61), first in the window, so
 * an xip.bin left by another build is told apart before any call into it.
 * The Makefile includes it ahead of every other XIP fragment.
 */

.xip.head :
{
    KEEP(*(.xip.header))
    . = ALIGN(4);
} > XIP
