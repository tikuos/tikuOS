/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_apollo4l.c - Apollo4 SAR-ADC driver.
 *
 * Builds the driver in tiku_adc_ambiq.inl for the Apollo4 Lite and Plus,
 * whose ADC clocks from HFRC with nothing to enable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_adc_arch.h"
#include "apollo4l.h"       /* CMSIS register defs (ADC/PWRCTRL) */

/* The ADC clock needs no enable on Apollo4. */
#define TIKU_ADC_ARCH_CLK_ENABLE()   do { } while (0)

#include "tiku_adc_ambiq.inl"
