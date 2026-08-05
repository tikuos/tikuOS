#!/bin/sh
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# check_durable_placement.sh - ban raw durable/warm section attributes.
#
# Durable placement must go through the kernel-owned grade macros in
# kernel/memory/tiku_mem.h, so a hand-rolled section attribute is a build
# failure rather than a latent data-loss bug.  Scope and allow-list below.
#
# SPDX-License-Identifier: Apache-2.0

# Scope: the main repo only (kernel/ interfaces/ drivers/ boot/ hal/ apps/).
# arch/ is allowed (linker scripts + the mem/mpu ports ARE the mechanism).
# tikukits/ and TikuBench/ are separate repositories with their own review.
#
# Both `.persistent` and `.uninit` are banned.  `.uninit` is not cosmetic: it
# means DIFFERENT things per platform.  On rp2350/ambiq it sits INSIDE the
# mirrored, MPU-protected durable window, while TIKU_PERSIST_WARM sits
# deliberately outside it; on ra8p1/nordic the two coincide; and MSP430 has no
# .uninit section at all, so the attribute would create an orphan.  Anyone
# writing it to mean "survives a warm reset" gets that on some boards and
# durable-plus-MPU-protected on others, with no diagnostic either way.
#
# Allow-list:
#   kernel/memory/tiku_mem.h - the macro definitions themselves.
#   tiku_shell_cmd_mrambench.c - DELIBERATE, not debt: its scratch word wants
#   to be inside the mirrored window so `mrambench verify` can force a
#   dirty-check hit, which is the opposite of the WARM grade.  Ambiq-only
#   (Makefile-gated), so the per-platform ambiguity above cannot bite it.
#   (The Phase-C debt entries — tiku_nvm_map.c, tiku_shell_cmd_history.c —
#   migrated to the grade macros on 2026-07-15; do not add new debt entries.)
#
# Exit 0 = clean, 1 = violations printed.

set -u
cd "$(dirname "$0")/.." || exit 2

ALLOW='^(kernel/memory/tiku_mem\.h|kernel/shell/commands/tiku_shell_cmd_mrambench\.c):'

viol=$(grep -rnE 'section\("\.(persistent|uninit)' \
        --include='*.c' --include='*.h' --include='*.inl' \
        kernel interfaces drivers boot hal apps 2>/dev/null \
       | grep -Ev "$ALLOW")

if [ -n "$viol" ]; then
    echo "check_durable_placement: raw .persistent/.uninit placement outside"
    echo "the grade macros (use TIKU_DURABLE / TIKU_PERSIST_WARM /"
    echo "TIKU_FRAM_SPILL from kernel/memory/tiku_mem.h.  NOTE .uninit is NOT"
    echo "a portable spelling of the WARM grade -- it is inside the mirrored"
    echo "durable window on rp2350/ambiq and absent entirely on MSP430):"
    echo "$viol"
    exit 1
fi
echo "check_durable_placement: OK"
exit 0
