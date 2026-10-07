#!/bin/sh
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# check_durable_placement.sh - ban raw durable and retained section attributes.
#
# Data placed in .persistent, .retained or .uninit must use the grade macros of
# kernel/memory/tiku_mem.h.  Prints every raw section attribute outside the
# allow-list and exits 1; make lint runs it.
#
# SPDX-License-Identifier: Apache-2.0

# Scope: kernel/, interfaces/, drivers/, boot/, hal/ and apps/.  arch/ is not
# scanned: its linker scripts and memory and MPU ports implement the grades.
# tikukits/ and TikuBench/ are separate repositories.
#
# `.persistent`, `.retained` and `.uninit` are all banned.  `.uninit` is not one
# grade: on RP2350 and Ambiq it is inside the mirrored, MPU-protected durable
# window, with `.retained` outside it; on the other ARM ports and the ESP32-C61
# the two share a section; MSP430 has no `.uninit` section, so the attribute
# makes an orphan.  Code that means "survives a warm reset" gets durable,
# write-protected memory on some boards, with no diagnostic.
#
# Allow-list:
#   kernel/memory/tiku_mem.h - the macro definitions themselves.
#   tiku_shell_cmd_mrambench.c - its scratch word must sit inside the mirrored
#   window, so `mrambench verify` can force a dirty-check hit.  The Makefile
#   builds it for Ambiq only.
#
# Exit 0 = clean, 1 = violations printed.

set -u
cd "$(dirname "$0")/.." || exit 2

ALLOW='^(kernel/memory/tiku_mem\.h|kernel/shell/commands/tiku_shell_cmd_mrambench\.c):'

viol=$(grep -rnE 'section\("\.(persistent|retained|uninit)' \
        --include='*.c' --include='*.h' --include='*.inl' \
        kernel interfaces drivers boot hal apps 2>/dev/null \
       | grep -Ev "$ALLOW")

if [ -n "$viol" ]; then
    echo "check_durable_placement: raw .persistent/.retained/.uninit placement outside"
    echo "the grade macros (use TIKU_DURABLE / TIKU_RETAINED /"
    echo "TIKU_FRAM_SPILL from kernel/memory/tiku_mem.h.  NOTE .uninit is NOT"
    echo "a portable spelling of the WARM grade -- it is inside the mirrored"
    echo "durable window on rp2350/ambiq and absent entirely on MSP430):"
    echo "$viol"
    exit 1
fi
echo "check_durable_placement: OK"
exit 0
