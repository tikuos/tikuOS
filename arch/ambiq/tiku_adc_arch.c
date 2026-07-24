/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - Apollo510 (Apollo5) SAR-ADC entry point.
 *
 * Apollo5 derives the ADC clock from HFRC and needs it forced on (CLKGEN.
 * FRCHFRC) before the ADC will convert -- the one thing Apollo4 does not need.
 * Everything else (power-on, slot config, software trigger, FIFO read) is
 * shared with Apollo4 in tiku_adc_ambiq.inl, since the ADC register block is
 * identical across both parts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_adc_arch.h"
#include "apollo510.h"      /* CMSIS register defs (ADC/PWRCTRL/CLKGEN) */

/* Apollo5: force HFRC on so the ADC clock runs (Apollo4 needs no equivalent). */
#define TIKU_ADC_ARCH_CLK_ENABLE()   (CLKGEN->MISC_b.FRCHFRC = 1u)

#include "tiku_adc_ambiq.inl"
