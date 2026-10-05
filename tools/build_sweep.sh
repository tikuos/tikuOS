#!/bin/sh
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# build_sweep.sh - build each MCU in TARGETS, then run make lint.
#
# Prints every target's text, data and bss sizes, or the first error lines of
# a failed build, and exits 1 when any build or the lint fails.  STM32N6,
# RA8P1, ESP32-C61 and the Apollo510B are not in TARGETS.
#
# SPDX-License-Identifier: Apache-2.0

# main.elf is one file at the repository root, shared by every target, and a
# build that fails to link leaves the previous target's ELF there for `size` to
# report.  Each target removes it before building.
#
# Usage:
#   tools/build_sweep.sh                 # every target + lint
#   tools/build_sweep.sh --no-lint       # builds only
#   tools/build_sweep.sh -j4             # pass jobs through to make

set -u

JOBS="-j8"
RUN_LINT=1
for arg in "$@"; do
    case "$arg" in
        --no-lint) RUN_LINT=0 ;;
        -j*)       JOBS="$arg" ;;
        *) echo "usage: $0 [--no-lint] [-jN]" >&2; exit 2 ;;
    esac
done

SHELL_FLAGS="TIKU_SHELL_ENABLE=1 TIKU_SHELL_BASIC_ENABLE=1"

# "<mcu>|<extra make flags>".  fr6989 builds without BASIC: BASIC's arena does
# not fit in that part's 128 KB of FRAM.
TARGETS="
nrf54lm20a|$SHELL_FLAGS
nrf54lm20b|$SHELL_FLAGS
nrf54l15|$SHELL_FLAGS
apollo4l|$SHELL_FLAGS
apollo4p|$SHELL_FLAGS
apollo510|$SHELL_FLAGS
rp2350|$SHELL_FLAGS
msp430fr5994|$SHELL_FLAGS
msp430fr6989|TIKU_SHELL_ENABLE=1
"

# The `... | while read` loop below runs in a subshell, where a counter does
# not survive the pipe, so failures are appended to a file instead.
FAILFILE=$(mktemp) || exit 2
trap 'rm -f "$FAILFILE"' EXIT
: > "$FAILFILE"

printf '%-14s %-6s %9s %7s %9s  %s\n' TARGET STATUS TEXT DATA BSS FLAGS
printf '%s\n' '---------------------------------------------------------------------'

# One target per line: the flag lists contain spaces, and splitting on words
# would make each flag a target of its own.
printf '%s\n' "$TARGETS" | while IFS= read -r entry; do
    [ -n "$entry" ] || continue
    mcu=$(printf '%s' "$entry" | cut -d'|' -f1)
    flags=$(printf '%s' "$entry" | cut -d'|' -f2-)
    [ -n "$mcu" ] || continue

    rm -f main.elf                       # never measure a stale ELF
    log=$(make MCU="$mcu" $flags $JOBS 2>&1)
    if [ ! -f main.elf ]; then
        printf '%-14s %-6s %9s %7s %9s  %s\n' "$mcu" FAIL - - - "$flags"
        printf '%s\n' "$log" | grep -E 'error:|Error [0-9]|overflowed|ERROR' \
            | head -4 | sed 's/^/      /'
        echo "$mcu" >> "$FAILFILE"
        continue
    fi
    set -- $(printf '%s\n' "$log" | awk '/^ *text/ {getline; print $1, $2, $3}')
    printf '%-14s %-6s %9s %7s %9s  %s\n' "$mcu" OK "${1:--}" "${2:--}" \
        "${3:--}" "$flags"
done

if [ "$RUN_LINT" -eq 1 ]; then
    printf '%s\n' '---------------------------------------------------------------------'
    if make lint >/dev/null 2>&1; then
        echo "lint           OK"
    else
        echo "lint           FAIL"
        make lint 2>&1 | grep -v '^make' | head -6 | sed 's/^/      /'
        echo lint >> "$FAILFILE"
    fi
fi

fails=$(wc -l < "$FAILFILE" | tr -d ' ')
printf '%s\n' '---------------------------------------------------------------------'
if [ "$fails" -eq 0 ]; then
    echo "sweep: all green"
    exit 0
fi
echo "sweep: $fails failure(s): $(tr '\n' ' ' < "$FAILFILE")"
exit 1
