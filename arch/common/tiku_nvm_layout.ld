/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_layout.ld - the ONE canonical NVM layout derivation (ARM).
 *
 * Every ARM platform divides its non-volatile memory in the same order:
 *
 *     [boot] code | module slot | NVM region | durable home   (top)
 *
 * This fragment holds the arithmetic ONCE.  A device script defines four
 * input symbols, INCLUDEs this file, then aliases the generic outputs to
 * its platform's historical symbol names (rram/mram/flash):
 *
 *   inputs (define BEFORE the INCLUDE):
 *     __tiku_layout_top           end of usable NVM (absolute address)
 *     __tiku_layout_code_cap      end of the code estate = module slot base
 *     __tiku_layout_module_size   module slot bytes (0x8000 on ARM)
 *     __tiku_layout_durable_size  durable-home bytes (16 KB in-place RRAM,
 *                                 64 KB MRAM mirror, 4 KB flash sector)
 *
 *   outputs:
 *     __tiku_module_slot / __tiku_module_slot_size
 *     __tiku_layout_durable_start (top - durable_size)
 *     __tiku_nvmfs_base / __tiku_nvmfs_end / __tiku_nvmfs_size
 *
 * The ASSERTs stitch every seam, so a script that edits one number cannot
 * silently open a gap or an overlap.  MSP430 does not use this file: its
 * unified FRAM is a genuinely different design (see the device fragments).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

__tiku_module_slot          = __tiku_layout_code_cap;
__tiku_module_slot_size     = __tiku_layout_module_size;

__tiku_layout_durable_start = __tiku_layout_top - __tiku_layout_durable_size;

__tiku_nvmfs_base           = __tiku_module_slot + __tiku_module_slot_size;
__tiku_nvmfs_end            = __tiku_layout_durable_start;
__tiku_nvmfs_size           = __tiku_nvmfs_end - __tiku_nvmfs_base;

ASSERT(__tiku_module_slot + __tiku_module_slot_size == __tiku_nvmfs_base,
       "NVM layout: gap/overlap between module slot and NVM region")
ASSERT(__tiku_nvmfs_base + __tiku_nvmfs_size == __tiku_layout_durable_start,
       "NVM layout: gap/overlap between NVM region and durable home")
ASSERT(__tiku_layout_durable_start + __tiku_layout_durable_size
           == __tiku_layout_top,
       "NVM layout: durable home does not end at the top of NVM")
ASSERT(__tiku_nvmfs_size > 0, "NVM layout: region squeezed to nothing")
