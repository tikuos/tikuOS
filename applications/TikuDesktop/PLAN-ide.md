# TikuIDE — a development environment of connected windows

> **Status: PLAN, not built.** Written 2026-08-26 against desktop
> `d77238f`, tracker `112fe9a`.  Every claim about the tree below was
> verified on that date.  The UI is planned here; the build/deploy
> backend is DELIBERATELY DEFERRED (§7) and nothing in the UI plan may
> depend on how that decision falls.

The simple editor (`apps/tiku_edit.c`) stays what it is: a window that
opens one file, colours it when its name says BASIC, and can run it.
TikuIDE is a SEPARATE application for working on a PROGRAM — several
files, a build, its errors — shaped the way BeIDE shaped it: not one
window with panes, but a small family of windows that know each other.

## 0. Context the executing agent must have

Repos, branches, commit discipline, comment style, and the
per-feature method (proving test → implement → mutation → full
`make test` → commit both repos) are exactly as PLAN-hybrid-apps.md §0
records them; they have not changed.  Two additions since that plan:

- Apps are built as `.so` by NAME (`make build/apps/editor.so`); after
  editing an app, build its target explicitly — `make` output that was
  grep-filtered has hidden a stale `.so` before.
- Script gestures that type letters must pass the lowercase key with
  `shift` held (`key h shift`), never a ready-made capital: the x11 kit
  folds letter keysyms in `event->key` and carries the typed character
  in `event->text` (desktop `d77238f`).  Apps insert from text.

## 1. The shape: three kinds of window, one application

**The project window** is the root.  It shows the project's files as a
list, grouped the way BeIDE grouped them (Sources, Headers, Notes —
groups come from the project file, §3).  Its menus: Project (Add
File…, Remove File, Build, Run, Settings…), Window (one row per open
editor, picking one raises it).  Return or double-click on a file row
opens that file's editor window.  Closing the project window closes
the project — after the same unsaved-changes question the editor
already asks, asked once, naming the dirty files.

**Editor windows**, one per file, each a real kit window with its own
menu bar (menus publish per window id — `tiku_app.h:56`).  Inside:
`tiku_textview` + `tiku_syntax`, exactly the machinery the simple
editor uses; the IDE adds no second text engine.  A dirty editor marks
its row in the project window.  One file opens ONE window: opening an
already-open file raises it.

**The message window**, one per project, appears when a build or run
first says something.  Each message is a row; a row that names a
file and line is a LINK — picking it opens/raises that editor and
places the caret there (`tiku_textview_place` exists).  This window is
the IDE's half of the backend contract (§7): whatever the backend
turns out to be, it feeds messages of the shape `{text, file?, line?}`
and nothing else the UI can see.

## 2. What the kits already give, and the four gaps

Verified present: per-window request/hand-over by id, per-window menu
publication, `tiku_list` (name-addressed rows, selection), textview +
line-local BASIC syntax, the shell's Open/Save panels via
services→pick, alerts inside the app's own window.

The gaps, each a kit change with its own proving test:

- **G1 — two windows from one app.**  CLOSED, see P1.  One correction
  to this plan's premise: there is ONE host, the desktop shell, and
  "both front-ends" is the suite's two script lanes, not two hosts --
  `tiku_trk_app.c` has no window layer at all and cannot host
  applications.  "Both placements" is real and both are done:
  `apps/trusted/*.so` runs in process, `apps/*.so` through the runner.
- **G2 — list group headers.**  CLOSED 2026-08-27 (kits `38744f3`).
  `tiku_list_set_heading()`: a heading keeps its place in the one index
  space but cannot be picked by any road, travel steps over it,
  spelling passes it by, and it crosses the wire as WORDS rather than
  as a row a reader would try to open.
- **G3 — stateful syntax for C.**  `tiku_syntax.h` documents its
  line-local contract as a fact about BASIC and says a language with
  block comments could not use the signature.  It is right.  Add a
  second signature that takes and returns a small carry state (inside
  a block comment, or not), have textview cache each line's entry
  state and re-classify forward from an edit only until the state
  stops changing.  BASIC keeps the line-local road untouched.
- **G4 — app-internal window addressing.**  CLOSED.  The table of open
  editors by path is in `tiku_ide.c`; raising needed kit API after all
  (`raise_window()`, P2); and "open file F, then place the caret" is
  `message_follow()` (P3), which opens or raises and then reveals.

## 3. The project IS a folder, and the project file is facts

A project is a directory holding a `project.tiku` — plain text, wire
native, the same discipline as the panel descriptions:

    title   Blinker
    group   Sources
    file    blink.bas
    file    util.bas
    group   Notes
    file    README

Paths are relative to the project's own directory; a line the parser
does not know is skipped and counted, exactly as `tiku_form_parse`
does.  Lines the BACKEND will need later (`target …`, `board …`) are
unknown lines today — the format grows without the UI caring.  Open
Project… is the shell's ordinary Open panel pointed at a
`project.tiku`; no registry, no workspace state outside the folder.

## 4. Phases, in order, each ending green and committed

- **P1 — the two-window spike (G1).**  DONE 2026-08-27 (kits `4224966`,
  tracker `78f9ccf`).  `examples/twowin.c` holds two windows;
  `twowin.script` proves them in process and `twowinremote.script`
  through the runner, over the wire.  What it found, all of it fixed:
  `event()` carried no window id, so an application could not tell its
  own windows' keys apart (`event_in()` appended, plus `closed()` for
  the window the runtime takes); BOTH hosts tagged the workspace window
  with the APPLICATION, so a second `open()` silently returned the
  first window wearing a second name; the embed host discarded the id
  in frame/menus/place/resize, and passed a hardcoded `1` to `pick()`;
  `close()` was a no-op in process and "closing is leaving" out of it;
  a session WAS a window, so out of process the second window quietly
  replaced the first.  Two facts worth keeping: windows of one
  application are placed side by side because overlapping tabs cannot
  both be clicked, and Cmd-W is the runtime's close for EMBEDDED
  windows only -- a remote window gets the key, which is why the
  terminal still has Ctrl-W.
- **P2 — the project window.**  DONE 2026-08-27 (kits `e71d4eb`,
  tracker `51cb555`).  `apps/tiku_ide.c` is the application, built as
  `IDE.so` (the name matters: the Applications menu labels a row from
  the FILE name, upper-casing only its first letter).
  `ideproject.script` proves it out of process, through the runner.
  One gap it turned up and closed: an application could not bring its
  OWN window forward, so a file already open could only be opened
  twice -- `raise_window()` was appended to the services and `RAISE`
  added to the wire (kits `4224966`..).  Facts worth keeping: the
  remote cap is 4 windows a session, so the IDE holds at most three
  editors; a script drives the project list by KEYBOARD (Down skips
  the group name, Return opens), which needs no coordinates and proves
  the heading rule end to end; and an editor window's tab can sit under
  the shell's own folder window, so the Window menu -- not
  `click-window` -- is how a script reaches one.
- **P3 — the message window.**  DONE 2026-08-27 (kits `a6ed5bc`,
  tracker `5244524`).  Project → Run puts the chosen file on the
  interpreter a board runs, as a child on a pipe pumped from `tick()`
  (which IS called out of process, so the IDE can do this through the
  runner).  `idemessages.script` proves it.  The contract is closed:
  a message is `{text, file, line}` and nothing else.  What it took:
  the interpreter names no FILE and reports an error as TWO lines --
  `? syntax` then `at line 250` -- so the annotation is folded onto the
  error row (only onto a row that began with `?`, so a program printing
  those words keeps them) and the file comes from what was run; a BASIC
  line WEARS its number, so the text line is found by scanning rather
  than by a table that could drift.  Two faults found and fixed while
  building: `tiku_textview_place()` does not move the page, so a caret
  below the window looked like nothing happening; and a program waiting
  on `INPUT` never ends -- the interpreter's own guard cannot fire --
  so a run is bounded and the stop is SAID.  The caret is proven
  without inventing UI for the test: type a letter, save, and read
  where it landed.
- **P4 — C colouring (G3).**  Stateful classifier, C table (keywords,
  strings, `//` and `/* */`, numbers), editor windows colour `.c`/`.h`.
  Colouring only — no compile, no promises the backend hasn't made.
- **P5 — the backend decision (§7), then Build.**  Not planned here.

Each phase is small enough for the house method to hold whole:
proving script first, mutation by name, full suite, both repos.

## 5. What is deliberately NOT in this plan

No toolbar, no function popup, no search-in-project, no debugger, no
tabs (windows are the tabs — that is the BeIDE position), no project
templates.  Each is a later argument, not a forgotten one.

## 6. Naming

The application is **TikuIDE** (`apps/tiku_ide.c`, menu row "IDE")
until DECISIONS.md says otherwise.  The simple editor keeps its name
and its job.

## 7. The deferred decision, stated so it stays visible

What Build MEANS is not decided: native board modules (the Tier-3
TMOD road — `kernel/shell/basic/tiku_basic_module.h`, per-board `.ld`,
cross-gcc the top Makefile already detects, image installed from a
store file by `MODLOAD`), or a host-side C harness first, or BASIC
only for a while.  The UI above touches that decision at exactly one
point — the message shape `{text, file?, line?}` — and at no other.
When the decision is taken it goes in DECISIONS.md, and P5 gets its
own plan.
