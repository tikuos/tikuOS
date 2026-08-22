#!/usr/bin/env python3
"""
Keep a kit a kit.

A kit is only a folder until something refuses the includes that would
undo it.  This holds the table the Makefile's header describes and fails
if a kit reaches somewhere it was not given.

Two things are checked:

  * every include that crosses a kit boundary is one ALLOWS permits, and
  * no kit reaches an application -- desktop or Tracker source.  A kit
    that knows about the app on top of it has stopped being a kit, and
    that is the mistake this whole arrangement exists to prevent.

Run it with `make check` in this directory, or from either application's
own test target.

Authors: Ambuj Varshney <ambuj@tiku-os.org>
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KITS_DIR = os.path.dirname(HERE)

# Lowest first.  A kit may include from the kits named here and from
# itself; anything else is an error, INCLUDING a kit further down the
# list, which is what makes this a table rather than an ordering.
ALLOWS = {
    "support":     set(),
    "device":      set(),
    "interface":   set(),
    "storage":     {"support", "device"},
    "translation": {"support", "storage", "interface"},
    "application": {"support", "device", "interface"},
}

INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)

# What a kit must never know the name of.
APP_PREFIXES = ("tiku_trk_desktop", "tiku_trk_app.")


def main():
    owner = {}
    for kit in ALLOWS:
        d = os.path.join(KITS_DIR, kit)
        for name in os.listdir(d):
            if name.endswith(".h"):
                owner[name] = kit

    errors = []
    for kit, allowed in sorted(ALLOWS.items()):
        d = os.path.join(KITS_DIR, kit)
        for name in sorted(os.listdir(d)):
            if not name.endswith((".c", ".h")):
                continue
            path = os.path.join(d, name)
            text = open(path, encoding="utf-8", errors="replace").read()
            for inc in INCLUDE.findall(text):
                if inc.startswith("../"):
                    errors.append("%s/%s: reaches out of the kits by path: %s"
                                  % (kit, name, inc))
                    continue
                if inc.startswith(APP_PREFIXES):
                    errors.append("%s/%s: a kit knows an application: %s"
                                  % (kit, name, inc))
                    continue
                where = owner.get(inc)
                if where is None or where == kit:
                    continue          # its own, or a generated/local header
                if where not in allowed:
                    errors.append("%s/%s: includes %s, which is the %s kit -- "
                                  "not one %s may draw on"
                                  % (kit, name, inc, where, kit))

    if errors:
        print("kit boundaries broken:")
        for e in errors:
            print("  " + e)
        return 1

    print("the kits hold: %d kits, boundaries as declared" % len(ALLOWS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
