#!/bin/sh
#
# Tiku Operating System v0.06
# Simple. Ubiquitous. Intelligence, Everywhere.
# http://tiku-os.org
#
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
#
# install_hooks.sh - point this repo and every nested one at hygiene/githooks.
#
# core.hooksPath is per-repository local config, so a clone starts without it
# and each nested repo needs its own. Run once after cloning.
#
# SPDX-License-Identifier: Apache-2.0

set -e

hygiene=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
root=$(CDPATH= cd -- "$hygiene/.." && pwd -P)
hooks="$hygiene/githooks"

[ -d "$hooks" ] || { echo "no githooks in $hygiene" >&2; exit 1; }

# The nested repos are separate checkouts with their own .git, so they get an
# absolute path; a relative one would resolve against the wrong root.
for repo in "$root" "$root"/TikuBench "$root"/tikukits "$root"/applications \
            "$root"/drivers "$root"/kintsugi "$root"/examples \
            "$root"/tikuConsole "$root"/experiment; do
    [ -e "$repo/.git" ] || continue
    git -C "$repo" config core.hooksPath "$hooks"
    printf '  %-28s hooksPath -> %s\n' \
        "$(basename "$repo")" "$(git -C "$repo" config core.hooksPath)"
done

echo
echo "commit messages are now checked against hygiene/commentstyle.md"
echo "bypass a single commit with: git commit --no-verify"
