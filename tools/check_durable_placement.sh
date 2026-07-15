#!/bin/sh
#
# check_durable_placement.sh - ban raw .persistent section attributes.
#
# WHY: durable placement must go through the kernel-owned grade macros in
# kernel/memory/tiku_mem.h (TIKU_DURABLE / TIKU_PERSIST_WARM /
# TIKU_FRAM_SPILL).  Hand-rolled per-file macros of the shape
#   #ifdef PLATFORM_MSP430  ->  section(".persistent")
#   #else                   ->  (nothing)
# are exactly how the 2026-07 audit found the inittab, shell history, and
# BASIC save slots silently VOLATILE on platforms that have working durable
# .persistent (see kintsugi/memory.md section 10c).  This check makes the
# pattern a build failure instead of a latent data-loss bug.
#
# Scope: the main repo only (kernel/ interfaces/ drivers/ boot/ hal/ apps/).
# arch/ is allowed (linker scripts + the mem/mpu ports ARE the mechanism).
# tikukits/ and TikuBench/ are separate repositories with their own review.
#
# Allow-list:
#   kernel/memory/tiku_mem.h - the macro definitions themselves.
#   (The Phase-C debt entries — tiku_nvm_map.c, tiku_shell_cmd_history.c —
#   migrated to the grade macros on 2026-07-15; do not add new entries.)
#
# Exit 0 = clean, 1 = violations printed.

set -u
cd "$(dirname "$0")/.." || exit 2

ALLOW='^(kernel/memory/tiku_mem\.h):'

viol=$(grep -rn 'section(".persistent' \
        --include='*.c' --include='*.h' --include='*.inl' \
        kernel interfaces drivers boot hal apps 2>/dev/null \
       | grep -Ev "$ALLOW")

if [ -n "$viol" ]; then
    echo "check_durable_placement: raw .persistent placement outside the"
    echo "grade macros (use TIKU_DURABLE / TIKU_PERSIST_WARM / TIKU_FRAM_SPILL"
    echo "from kernel/memory/tiku_mem.h -- see kintsugi/memoryfix.md Phase A):"
    echo "$viol"
    exit 1
fi
echo "check_durable_placement: OK"
exit 0
