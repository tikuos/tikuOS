#!/bin/sh
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# axonpack_selftest.sh - prove axonpack is MODEL-AGNOSTIC, not vww-shaped.
#
# Builds every model the Axon checkout ships and requires each to pass the
# reconstruction gate: patch the packed command buffer with the address the
# real linker used, then demand byte-identical output.  Detail below.
#
# SPDX-License-Identifier: Apache-2.0

# The models differ enough for this to mean something: tinyml_ad has 270 KB of
# weights but 632 B of commands and only TWO relocation symbols, tinyml_ic has
# 1468 relocation sites, tinyml_kws is 22 KB.  A tool that passes all of them
# unchanged is generic by evidence rather than by claim -- and the symbol COUNT
# varying per model is why the loader must resolve symbols through a registry
# instead of a fixed table.
#
# Usage:  tools/axonpack_selftest.sh [model ...]      (default: all shipped)
# Needs:  temp/axon-models checkout, MCU=nrf54lm20b toolchain.
set -e

MODELS="${*:-tinyml_kws tinyml_ic tinyml_ad tinyml_vww}"
OBJ=build/nrf54lm20b/temp/axon-models/lib/axon/tests/axon/inference/src/nrf_axon_app_test_nn_inference.o
OUT=temp/axm
mkdir -p "$OUT"
fail=0

for m in $MODELS; do
    printf '\n======== %s\n' "$m"
    rm -f main.elf
    if ! make MCU=nrf54lm20b TIKU_AXON_ENABLE=1 TIKU_AXON_MODEL="$m" \
              EXTRA_CFLAGS=-DTIKU_SHELL_CMD_AXONSPROBE=1 -j8 >/dev/null 2>&1; then
        echo "  BUILD FAILED"; fail=$((fail + 1)); continue
    fi
    if python3 tools/axonpack.py --obj "$OBJ" --model "$m" \
              --out "$OUT/$m.axm" --verify-elf main.elf; then
        :
    else
        echo "  GATE FAILED"; fail=$((fail + 1))
    fi
done

printf '\n======== summary\n'
for m in $MODELS; do
    [ -f "$OUT/$m.axm" ] && printf '  %-24s %8d B\n' "$m" "$(wc -c <"$OUT/$m.axm")"
done
if [ "$fail" -ne 0 ]; then
    echo "  $fail model(s) FAILED -- axonpack has become model-specific"
    exit 1
fi
echo "  all models packed and byte-identical: axonpack is model-agnostic"
