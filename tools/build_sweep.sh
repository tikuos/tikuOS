#!/bin/sh
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# build_sweep.sh - compile every supported MCU in one command.
#
# A per-platform integer width is invisible to any single-target build, so
# the only defence is compiling all of them -- which is cheap and takes one
# command.  Usage and the shared-main.elf trap are below.
#
# SPDX-License-Identifier: Apache-2.0

# Also handles the shared-main.elf trap: main.elf lives at the repo ROOT and is
# reused across targets, so a build that fails to relink leaves the PREVIOUS
# target's ELF in place and `size` reports it as if it were the new one.  Every
# target here removes it first.
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

# "<mcu>|<extra make flags>".  fr6989 is built WITHOUT BASIC on purpose: it is a
# 128 KB FRAM part and BASIC's arena overflows HIFRAM there by ~118 KB, which is
# a capacity fact about the silicon, not a regression to chase.
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

# A `... | while read` loop runs in a SUBSHELL, so a counter incremented inside
# it is lost at the pipe's end -- the failure tally has to live in a file.
FAILFILE=$(mktemp) || exit 2
trap 'rm -f "$FAILFILE"' EXIT
: > "$FAILFILE"

printf '%-14s %-6s %9s %7s %9s  %s\n' TARGET STATUS TEXT DATA BSS FLAGS
printf '%s\n' '---------------------------------------------------------------------'

# Iterate by LINE, not by word: the flag lists contain spaces, so an unquoted
# `for entry in $TARGETS` splits each flag into its own bogus target.
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
