/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - Apollo510 SAR-ADC driver.
 *
 * Builds the driver in tiku_adc_ambiq.inl for the Apollo510 and Apollo510B.
 * The ADC converts only while HFRC is forced on through CLKGEN.FRCHFRC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_adc_arch.h"
#include "tiku_cpu_common.h"
#include "apollo510.h"      /* CMSIS register defs (ADC/PWRCTRL/CLKGEN) */

#define TIKU_ADC_ARCH_CLK_ENABLE() \
    tiku_ambiq_clock_force(TIKU_AMBIQ_CLOCK_ADC, CLKGEN_MISC_FRCHFRC_Msk)
#define TIKU_ADC_ARCH_CLK_DISABLE() \
    tiku_ambiq_clock_force(TIKU_AMBIQ_CLOCK_ADC, 0u)

#include "tiku_adc_ambiq.inl"
