#!/bin/sh
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# axonpack_selftest.sh - pack and verify every Axon model with axonpack.
#
# Builds the image once per model, packs the model, and runs axonpack's
# --verify-elf check against that main.elf.  Exits 1 when any model fails to
# build or to verify.
#
# SPDX-License-Identifier: Apache-2.0

# The four default models differ in weight size, command-buffer size, site
# count and the number of symbols their sites name.
#
# Usage:  tools/axonpack_selftest.sh [model ...]      (default: all shipped)
# Needs:  temp/axon-models checkout, MCU=nrf54lm20b toolchain.
set -e

MODELS="${*:-tinyml_kws tinyml_ic tinyml_ad tinyml_vww}"
OBJ=build/nrf54lm20b/temp/axon-models/lib/axon/tests/axon/inference/src/nrf_axon_app_test_nn_inference.o
OUT=temp/axm
mkdir -p "$OUT"
fail=0

# Packing references include weights and test vectors, not just firmware.
# This layout is not flashed; device firmware keeps its normal code limit.
cap_flags=-Wl,--defsym=__tiku_code_cap_override=0x100000
for m in $MODELS; do
    printf '\n======== %s\n' "$m"
    rm -f main.elf
    if ! make MCU=nrf54lm20b TIKU_AXON_ENABLE=1 TIKU_AXON_MODEL="$m" \
              EXTRA_CFLAGS=-DTIKU_SHELL_CMD_AXONSPROBE=1 \
              EXTRA_LDFLAGS="$cap_flags" -j8 >/dev/null 2>&1; then
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
