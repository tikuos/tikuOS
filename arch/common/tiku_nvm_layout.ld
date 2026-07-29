/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_layout.ld - the ONE canonical NVM layout derivation (ARM).
 *
 * Every ARM platform divides NVM the same way -- code, module slot, NVM
 * region, durable home -- and this fragment holds that arithmetic once.
 * The input/output symbol contract is below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
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
 */

__tiku_module_slot          = __tiku_layout_code_cap;
__tiku_module_slot_size     = __tiku_layout_module_size;

__tiku_layout_durable_start = __tiku_layout_top - __tiku_layout_durable_size;

__tiku_nvmfs_base           = __tiku_module_slot + __tiku_module_slot_size;
__tiku_nvmfs_end            = __tiku_layout_durable_start;
__tiku_nvmfs_size           = __tiku_nvmfs_end - __tiku_nvmfs_base;

/*
 * ORDERING FIRST -- this is the check that can actually fail.
 *
 * The three "gap/overlap" assertions that stood here were tautologies: each
 * restated a definition made a few lines above (__tiku_nvmfs_base IS
 * module_slot + module_slot_size, __tiku_nvmfs_size IS end - base, and
 * durable_start IS top - durable_size), so they held for any inputs whatsoever,
 * including inputs that overlap.
 *
 * And `size > 0` did not cover the case it looks like it covers.  Linker
 * expressions are UNSIGNED: if the code estate grows past the durable home, the
 * subtraction that computes the size WRAPS to an enormous positive number
 * rather than falling to zero, so `> 0` passes and the build ships a region
 * whose size is nonsense.  Comparing the addresses in order cannot wrap, so
 * that is what is checked, before anything is derived from them.
 */
ASSERT(__tiku_layout_durable_start < __tiku_layout_top,
       "NVM layout: durable home is larger than the whole NVM")
ASSERT(__tiku_module_slot + __tiku_module_slot_size < __tiku_layout_durable_start,
       "NVM layout: code window + module slot overruns the durable home (the region size would WRAP, not shrink) -- shrink the code window")

/*
 * FLOOR.  A region merely being non-empty is not useful: it must hold the NVM
 * tier (a 32 KB portable contract) plus a store worth mounting.  The exact
 * requirement is per-platform and lives in C, where tiku_tfs_mount() enforces
 * TIKU_TFS_MIN_SLOTS against the carve it is actually handed; this is the crude
 * backstop that catches a grossly mis-sized window at LINK time, on the four
 * platforms that are not on anyone's bench.
 */
ASSERT(__tiku_nvmfs_size >= 64 * 1024,
       "NVM layout: region too small to hold the 32 KB tier and a file store")
