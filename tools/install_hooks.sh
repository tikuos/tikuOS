#!/bin/sh
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# install_hooks.sh - point this repo and every nested one at .githooks.
#
# core.hooksPath is per-repository local config, so a clone starts without it
# and each nested repo needs its own. Run once after cloning.
#
# SPDX-License-Identifier: Apache-2.0

set -e

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
hooks="$root/.githooks"

[ -d "$hooks" ] || { echo "no .githooks in $root" >&2; exit 1; }

# The nested repos are separate checkouts with their own .git, so they get an
# absolute path; a relative one would resolve against the wrong root.
for repo in "$root" "$root"/TikuBench "$root"/tikukits "$root"/kintsugi \
            "$root"/examples "$root"/tikuConsole "$root"/experiment; do
    [ -e "$repo/.git" ] || continue
    if [ "$repo" = "$root" ]; then
        git -C "$repo" config core.hooksPath .githooks
    else
        git -C "$repo" config core.hooksPath "$hooks"
    fi
    printf '  %-28s hooksPath -> %s\n' \
        "$(basename "$repo")" "$(git -C "$repo" config core.hooksPath)"
done

echo
echo "commit messages are now checked against commentstyle.md"
echo "bypass a single commit with: git commit --no-verify"
