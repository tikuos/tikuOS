# Desktop — build notes

## S0 (2026-08-11): session + namespace, hardware-proven

Built `build/tiku-desk-probe`; warning-clean at -Wall -Wextra, libc only.
Verified against three boards over serial: RA8P1 (172 nodes), Apollo510B
(189), nRF54LM20A (187) — plus auto-discovery by /dev/serial/by-id,
`cat`/`write` round-trips, an EACCES refusal surfaced verbatim, and a
live external change picked up by the poll fallback.

### Bugs found and fixed while proving it
- stale prompt in flight corrupted the first command's output — cmd()
  now drains before sending and matches the echo at the END of the
  first line.
- the manifest's `#` column header parsed as a node.
- CR rode along in the last manifest field, so every capability
  compared unequal ("-\r" != "-").

### THE FIRMWARE GAP S0 EXPOSED
`cat /sys/vfs/manifest` is CUT on every board — the tree now exceeds the
shell's 8 KB read buffer, so the device's self-description arrives
incomplete.  Worked around host-side (truncation detected, tail
completed by an ls-walk, those nodes carry no descriptor/capability).
Proper fixes, in order of preference:
1. paged manifest (`manifest <path>` or an offset argument) — bounded
   output regardless of tree size, and it stays one read per pane;
2. raise TIKU_SHELL_READ_MAX (buys time, same wall later).
Until then the UI must treat descriptors as optional, which it already
does.

### Next
S1 (interface layer) needs no firmware.  The `sub` push command remains
the one firmware prerequisite for live windows; the poll fallback is
proven, so it is an optimisation, not a blocker.

## S1 (2026-08-11): interface layer + gallery

R5-SPEC.md holds the measured constants (tints, palette, metrics).  Its
palette is now the DEFAULT table in `../kits/interface/tiku_theme.c`,
which every semantic colour reads through, so a swapped table recolours
every control; the tints and metrics stay compiled in, because a theme
names colour roles and layout never moves with it.

Built:
- `tiku_gfx`   surface, clip, fill/frame/bevel, tint, PNG writer
                    (stored-deflate: no zlib dependency for screenshots)
- `tiku_font`  8-bit coverage glyphs baked by tools/genfont.py from
                    DejaVu Sans 12 px; blended against the destination so
                    labels antialias correctly over bevels and selections
- `tiku_ui`    button (normal/pressed/default/focus/disabled),
                    checkbox, radio, text field with caret, scrollbar with
                    the knurled thumb, menu bar, dropped menu, list rows +
                    sortable headers, window frame with the yellow tab
- `tiku_x11`   native window (plain Xlib, no toolkit)
- `tiku-desk-gallery`  every control in every state; `-o FILE` dumps a PNG,
                    which IS the fidelity test artefact

Bug worth remembering: PIL's getbbox measures from the ASCENDER line, the
renderer draws from the baseline.  The generator now rebases (oy -= ascent);
before that every label sat a full ascent too high.

### Calibration status
Structure, tints and metrics come from the documented BeOS API and look
right.  Still inference, flagged in R5-SPEC.md: the exact tab yellow,
selection blue, focus blue, and the button corner radius.  Final
"indistinguishable from R5" verdict needs a reference screenshot held
against build/gallery.png -- the one thing this phase cannot self-check.

### Next (S2)
Regions + expose-redraw, tab drag, z-order, focus.  The controls above are
pure drawing, so S2 adds hit-testing and dirty-rect bookkeeping without
touching them.
