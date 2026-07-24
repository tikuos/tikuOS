/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * nrf54lm20b.h - vendored Nordic MDK register map entry point
 *                (nRF54LM20B, app core)
 *
 * The nRF54LM20B is the nRF54LM20A plus the 128 MHz Axon NPU: the register
 * maps are otherwise identical (memory, every peripheral base, IRQ enums --
 * diff-proven against the LM20A headers), differing by exactly one block,
 * NRF_AXONS @ 0x50056000 (IRQn 86).  Same vendoring rules as the siblings:
 * register-definition subset only, BSD-3-Clause banners retained, the single
 * local edit is the flattened compiler_abstraction.h include (byte-identical
 * to the shared copy, so not duplicated).  See PROVENANCE.md.
 *
 * All peripheral access uses the explicit secure (_S) aliases -- the app runs
 * All-Secure (no TF-M / SPM).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_NRF54LM20B_MDK_H_
#define TIKU_NORDIC_NRF54LM20B_MDK_H_

#include <stdint.h>
#include "compiler_abstraction.h"
#include "nrf54lm20b_types.h"
#include "nrf54lm20b_global.h"

#endif /* TIKU_NORDIC_NRF54LM20B_MDK_H_ */
