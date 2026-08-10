/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_layout.ld - the ONE canonical NVM layout derivation (ARM).
 *
 * Every ARM platform divides NVM the same way -- code, NVM region, persist
 * partition -- and this fragment holds that arithmetic once.  The input/output
 * symbol contract is below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Every ARM platform divides its non-volatile memory in the same order:
 *
 *     [boot] code window | NVM region | persist partition   (top)
 *
 * This fragment holds the arithmetic ONCE.  A device script defines three
 * input symbols, INCLUDEs this file, then aliases the generic outputs to
 * its platform's historical symbol names (rram/mram/flash):
 *
 *   inputs (define BEFORE the INCLUDE):
 *     __tiku_layout_top           end of usable NVM (absolute address)
 *     __tiku_layout_code_cap      end of the code estate = NVM region base
 *     __tiku_layout_persist_size  persist partition bytes (16 KB in-place RRAM,
 *                                 64 KB MRAM mirror, 4 KB flash sector)
 *
 *   optional (--defsym from the build, not the device script):
 *     __tiku_module_reserve       bytes at the TOP of the code window kept
 *                                 for the Tier-3 module slot, present only
 *                                 when the loader is compiled in
 *
 *   outputs:
 *     __tiku_code_limit           where the image must end: code_cap less
 *                                 the module reserve when one is in force
 *     __tiku_layout_persist_start (top - persist_size)
 *     __tiku_nvmfs_base / __tiku_nvmfs_end / __tiku_nvmfs_size
 *
 * The Tier-3 module slot is NOT an estate.  It lives inside the code
 * window's guaranteed slack -- [code_cap - 32 KB, code_cap) -- and exists
 * only in builds that compile the loader, so a build without it pays
 * nothing and no fixed address ever moves with a feature flag.  The C-side
 * addresses live in kernel/shell/basic/tiku_basic_module.h; keep them equal
 * to code_cap - 32 KB per platform.
 *
 * The ASSERTs stitch every seam, so a script that edits one number cannot
 * silently open a gap or an overlap.  MSP430 does not use this file: its
 * unified FRAM is a genuinely different design (see the device fragments).
 */

__tiku_layout_persist_start = __tiku_layout_top - __tiku_layout_persist_size;

__tiku_nvmfs_base           = __tiku_layout_code_cap;
__tiku_nvmfs_end            = __tiku_layout_persist_start;
__tiku_nvmfs_size           = __tiku_nvmfs_end - __tiku_nvmfs_base;

__tiku_code_limit           = __tiku_layout_code_cap -
                              (DEFINED(__tiku_module_reserve)
                               ? __tiku_module_reserve : 0);

/*
 * ORDERING FIRST -- this is the check that can actually fail.
 *
 * Linker expressions are UNSIGNED: if the code estate grows past the persist
 * partition, the subtraction that computes the region size WRAPS to an enormous
 * positive number rather than falling to zero, so a `size > 0` check passes
 * and the build ships a region whose size is nonsense.  Comparing the
 * addresses in order cannot wrap, so that is what is checked, before
 * anything is derived from them.
 */
ASSERT(__tiku_layout_persist_start < __tiku_layout_top,
       "NVM layout: persist partition is larger than the whole NVM")
ASSERT(__tiku_layout_code_cap < __tiku_layout_persist_start,
       "NVM layout: code window overruns the persist partition (the region size would WRAP, not shrink) -- shrink the code window")

/* A module reserve larger than the window would wrap code_limit the same
 * way; images are asserted against code_limit per device, so pin it here. */
ASSERT(__tiku_code_limit <= __tiku_layout_code_cap,
       "NVM layout: module reserve exceeds the code window")

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
