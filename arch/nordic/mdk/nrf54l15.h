/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * nrf54l15.h - vendored Nordic MDK register map entry point (nRF54L15, app core)
 *
 * TikuOS vendors only the register-definition subset of the Nordic MDK: the
 * peripheral struct types (nrf54l15_types.h) and the base-pointer instances
 * (nrf54l15_global.h), plus the compiler-abstraction macros.  Those three
 * headers are self-contained -- nrf54l15_types.h defines __IOM/__IM/__OM under
 * #ifndef -- so they compile with just <stdint.h> and need NO CMSIS core.  The
 * Cortex-M33 core intrinsics TikuOS actually uses (NVIC, SCB, SysTick, WFI,
 * PRIMASK) are hand-rolled in arch/nordic/tiku_nordic_core.h instead, keeping
 * the port free of a full CMSIS dependency (the same approach as arm-rp2350).
 *
 * The vendored files below retain their original BSD-3-Clause license blocks;
 * only their internal "../../common/compiler_abstraction.h" include was
 * flattened to a local path.  Source: NordicSemiconductor/nrfx bsp/stable/mdk.
 *
 * All peripheral access uses the explicit secure (_S) aliases -- the app runs
 * All-Secure (no TF-M / SPM), so 0x5xxx_xxxx peripheral views are correct.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_NRF54L15_MDK_H_
#define TIKU_NORDIC_NRF54L15_MDK_H_

#include <stdint.h>
#include "compiler_abstraction.h"
#include "nrf54l15_types.h"
#include "nrf54l15_global.h"

#endif /* TIKU_NORDIC_NRF54L15_MDK_H_ */
