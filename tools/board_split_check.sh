#!/bin/sh
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# board_split_check.sh - pin the build baseline across a build-system edit.
#
# Builds the matrix and records a sha256 per image, so a "no behaviour
# change" claim becomes falsifiable: run it before the edit and again after,
# and the diff is the gate.  Sub-commands and the two traps are below.
#
# SPDX-License-Identifier: Apache-2.0

#   tools/board_split_check.sh record [file]   build the matrix, write hashes
#   tools/board_split_check.sh check  [file]   rebuild, diff against them
#   tools/board_split_check.sh determinism     prove the build repeats at all
#
# NOTE main.elf/main.bin/main.hex are SHARED across MCUs (one file at the repo
# root, not per-build-dir) -- the recorded shared-ELF trap.  Every row removes
# them first, so a row that fails to link cannot silently inherit the previous
# row's image and be recorded as a pass.
#
# NOTE 2 -- why every row is built FROM SCRATCH.  Object files do not depend on
# the Makefile: nothing declares it as a prerequisite, and the flag-change guard
# only fires when the COMMAND LINE (MAKEOVERRIDES) changes, not when the
# Makefile's own text does.  So an incremental run of this script after a
# build-system edit recompiles NOTHING and reports IDENTICAL no matter what was
# changed.  Measured, not assumed: editing the ambiq CFLAGS from -Os to -O1
# produced a byte-identical ELF and rebuilt 0 objects.  Since this script exists
# precisely to gate build-system edits, that made it a rubber stamp.  Each row
# now wipes its build dir first.  It is slow; a gate that cannot fail is worse.
set -u
cd "$(dirname "$0")/.." || exit 1

BASELINE="${2:-tools/board_split_baseline.txt}"

# One row per line: NAME | make arguments
# Chosen to cover every platform, both RP2350 boards, and the Apollo510
# driver stack that stages S2/S3 re-gated.  The last row is the S5 board: the
# same Apollo510 silicon on a PCB carrying none of the EVB's parts.  It is here
# so that "a boardless board still builds" stays true rather than being a claim
# made once.
MATRIX="
msp430fr5994-bare        | MCU=msp430fr5994
msp430fr6989-bare        | MCU=msp430fr6989
rp2350-pico2w            | MCU=rp2350 BOARD=pico2w TIKU_SHELL_ENABLE=1
rp2350-pico2             | MCU=rp2350 BOARD=pico2 TIKU_SHELL_ENABLE=1
nrf54l15-shell           | MCU=nrf54l15 TIKU_SHELL_ENABLE=1
nrf54lm20a-shell         | MCU=nrf54lm20a TIKU_SHELL_ENABLE=1
apollo4l-shell           | MCU=apollo4l TIKU_SHELL_ENABLE=1
apollo4p-shell           | MCU=apollo4p TIKU_SHELL_ENABLE=1
apollo510-shell          | MCU=apollo510 TIKU_SHELL_ENABLE=1
apollo510-nor            | MCU=apollo510 TIKU_SHELL_ENABLE=1 TIKU_DRV_NOR_ENABLE=1
apollo510b-shell         | MCU=apollo510b TIKU_SHELL_ENABLE=1
apollo510b-full          | MCU=apollo510b TIKU_SHELL_ENABLE=1 TIKU_DRV_EMMC_ENABLE=1 TIKU_DRV_PSRAM_ENABLE=1 TIKU_DRV_USB_ENABLE=1 TIKU_DRV_BLE_EM9305_ENABLE=1
tiku-bare                | MCU=apollo510 BOARD=tiku_bare TIKU_SHELL_ENABLE=1
"

hash_row() {   # $1 = make args; echoes "sha  ok|FAIL"
    rm -f main.elf main.bin main.hex
    # Wipe this row's build dir (BUILD_DIR = build/$(MCU)) -- see NOTE 2.
    row_mcu=$(echo "$1" | tr ' ' '\n' | sed -n 's/^MCU=//p')
    [ -n "$row_mcu" ] && rm -rf "build/$row_mcu"
    if ! make $1 >/dev/null 2>&1; then
        echo "-                                                                FAILED"
        return
    fi
    # main.elf is the one artifact every platform produces.
    if [ -f main.elf ]; then
        echo "$(sha256sum main.elf | cut -d' ' -f1)  ok"
    else
        echo "-                                                                NO-IMAGE"
    fi
}

run_matrix() {
    echo "$MATRIX" | while IFS='|' read -r name args; do
        name=$(echo "$name" | tr -d ' ')
        [ -z "$name" ] && continue
        printf '%-24s %s\n' "$name" "$(hash_row "$args")"
    done
}

case "${1:-record}" in
determinism)
    # If the same inputs do not produce the same image, every other result
    # here is noise.  Prove it before trusting anything else.
    echo "building apollo510b-shell twice..."
    A=$(hash_row "MCU=apollo510b TIKU_SHELL_ENABLE=1")
    B=$(hash_row "MCU=apollo510b TIKU_SHELL_ENABLE=1")
    echo "  1: $A"
    echo "  2: $B"
    [ "$A" = "$B" ] && echo "DETERMINISTIC" || echo "NOT DETERMINISTIC -- the gate is unusable as-is"
    ;;
record)
    echo "recording baseline -> $BASELINE"
    run_matrix | tee "$BASELINE"
    echo "done"
    ;;
check)
    [ -f "$BASELINE" ] || { echo "no baseline at $BASELINE (run 'record' first)"; exit 2; }
    run_matrix > /tmp/board_split_now.txt
    if diff -u "$BASELINE" /tmp/board_split_now.txt; then
        echo "IDENTICAL -- no behaviour change"
    else
        echo "DIFFERENT -- see the diff above"
        exit 1
    fi
    ;;
*)
    echo "usage: $0 {record|check|determinism} [baseline-file]"; exit 2 ;;
esac
