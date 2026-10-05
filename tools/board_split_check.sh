#!/bin/sh
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# board_split_check.sh - hash each image of a fixed build matrix, and diff.
#
# Builds every row from scratch and records the sha256 of its main.elf, so a
# build-system edit that should change no image can be checked: record before
# the edit, check after.  Sub-commands are below.
#
# SPDX-License-Identifier: Apache-2.0

#   tools/board_split_check.sh record [file]   build the matrix, write hashes
#   tools/board_split_check.sh check  [file]   rebuild, diff against them
#   tools/board_split_check.sh determinism     build one row twice, compare
#
# main.elf, main.bin and main.hex are one file each at the repository root,
# shared by every MCU, and a row that fails to link leaves the previous row's
# image there.  Each row removes them before it builds.
#
# Each row also deletes build/<mcu> first.  The flag-change guard rebuilds
# after an edit to the Makefile or to the command-line overrides, but not
# after an edit to an included .mk file; an incremental build then recompiles
# nothing and hashes the same.
set -u
cd "$(dirname "$0")/.." || exit 1

BASELINE="${2:-tools/board_split_baseline.txt}"

# One row per line: NAME | make arguments.  The rows cover MSP430, RP2350
# (both boards), Nordic and Ambiq, the Apollo510 driver options, and
# tiku_bare: Apollo510 silicon on a board that carries none of the EVB's
# parts.  STM32N6, RA8P1 and ESP32-C61 have no row.
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

# Build one row from scratch; echo the sha256 of its main.elf and ok, or a
# dash and FAILED or NO-IMAGE.
hash_row() {   # $1 = the make arguments of the row
    rm -f main.elf main.bin main.hex
    # A clean build: delete this row's build dir, build/$(MCU).
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

# Build every row of MATRIX and print one name-and-result line per row.
run_matrix() {
    echo "$MATRIX" | while IFS='|' read -r name args; do
        name=$(echo "$name" | tr -d ' ')
        [ -z "$name" ] && continue
        printf '%-24s %s\n' "$name" "$(hash_row "$args")"
    done
}

case "${1:-record}" in
determinism)
    # The recorded hashes are comparable only if the same inputs give the
    # same image: build one row twice and compare.
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
