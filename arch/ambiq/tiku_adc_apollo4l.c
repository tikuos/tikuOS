/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_apollo4l.c - Apollo4 Lite (AMAP42KL) SAR-ADC entry point.
 *
 * Apollo4's ADC runs straight off HFRC, so it needs no special clock bring-up
 * (the clock hook is a no-op). All the conversion logic -- power-on, slot
 * config, software trigger and FIFO read -- is shared with Apollo5 in
 * tiku_adc_ambiq.inl, since the ADC register block is identical across parts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_adc_arch.h"
#include "apollo4l.h"       /* CMSIS register defs (ADC/PWRCTRL) */

/* Apollo4: the ADC is fed by HFRC directly -- no extra clock enable needed. */
#define TIKU_ADC_ARCH_CLK_ENABLE()   do { } while (0)

#include "tiku_adc_ambiq.inl"
