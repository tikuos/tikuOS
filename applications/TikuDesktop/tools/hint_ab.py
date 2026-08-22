#!/usr/bin/env python3
"""Is the grid-fitting worth having?  Every font, both ways.

A DEVELOPMENT tool.  Renders each face with the vertical fit on and off
and holds both against freetype, which is hinted, so "closer" is the best
proxy we have for "looks like what a reader expects".

  tools/hint_ab.py [sizes]
"""
import glob, os, sys, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cf", os.path.join(HERE, "compare_freetype.py"))
cf = importlib.util.module_from_spec(spec); spec.loader.exec_module(cf)

sizes = [int(v) for v in (sys.argv[1].split(",") if len(sys.argv) > 1 else ["10", "12"])]
cps = sorted({ord(c) for c in cf.DEFAULT_TEXT})

def mae_for(font, px):
    mine, err = cf.ours(font, px, cps)
    if mine is None:
        return None
    family, mine = mine
    try:
        ref = cf.theirs(font, px, cps)
    except Exception:
        return None
    shared = sorted(set(mine) & set(ref))
    if not shared:
        return None
    total = 0.0
    for cp in shared:
        _, _, m = cf.compare(mine[cp], ref[cp])
        total += m
    return family, total / len(shared)

fonts = sorted(glob.glob("/usr/share/fonts/**/*.ttf", recursive=True) +
               glob.glob("/usr/share/fonts/**/*.otf", recursive=True))
better = worse = same = 0
deltas, worst_regress, best_gain = [], [], []
for font in fonts:
    for px in sizes:
        os.environ.pop("TIKU_HINT", None)
        on = mae_for(font, px)
        os.environ["TIKU_HINT"] = "0"
        off = mae_for(font, px)
        os.environ.pop("TIKU_HINT", None)
        if on is None or off is None:
            continue
        family, a = on
        _, b = off
        d = a - b                      # negative = hinting is closer
        deltas.append(d)
        if d < -0.25: better += 1
        elif d > 0.25: worse += 1
        else: same += 1
        worst_regress.append((d, px, family, os.path.basename(font)))
        best_gain.append((-d, px, family, os.path.basename(font)))

worst_regress.sort(reverse=True); best_gain.sort(reverse=True)
n = len(deltas) or 1
print("font/size pairs measured: %d   sizes: %s" % (len(deltas), sizes))
print("  hinting is CLOSER to freetype: %d   further: %d   no change: %d"
      % (better, worse, same))
print("  mean change in |diff|: %+.2f  (negative is better)" % (sum(deltas)/n))
print("\n  biggest gains:")
for g, px, fam, f in best_gain[:6]:
    print("    %+6.1f  %2dpx  %-26s %s" % (-g, px, fam[:26], f))
print("\n  biggest regressions:")
for d, px, fam, f in worst_regress[:6]:
    print("    %+6.1f  %2dpx  %-26s %s" % (d, px, fam[:26], f))
