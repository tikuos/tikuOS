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

- **G1 — two windows from one app.**  The descriptor API is per-id
  everywhere, but no shipped app opens a second window; the desktops'
  bookkeeping has never been exercised past one.  Prove it FIRST with
  a spike (a throwaway two-window app + script) before any IDE code
  exists, in both front-ends and both placements (in-process, .so).
- **G2 — list group headers.**  `tiku_list` has no notion of a
  heading row.  Add non-selectable heading rows (drawn in the label
  style, skipped by arrow travel) to the interface kit.
- **G3 — stateful syntax for C.**  `tiku_syntax.h` documents its
  line-local contract as a fact about BASIC and says a language with
  block comments could not use the signature.  It is right.  Add a
  second signature that takes and returns a small carry state (inside
  a block comment, or not), have textview cache each line's entry
  state and re-classify forward from an edit only until the state
  stops changing.  BASIC keeps the line-local road untouched.
- **G4 — app-internal window addressing.**  The Window menu and the
  message-row links need "raise window N of mine" and "open file F,
  then place the caret" as ordinary operations inside one app.  This
  is IDE-internal bookkeeping (a table of open editors by path), not
  new kit API — listed because it is where multi-window apps rot.

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

- **P1 — the two-window spike (G1).**  Throwaway app, script proving
  both windows draw, take keys separately, publish separate menus, and
  close separately.  What breaks in the desktops gets fixed HERE.
- **P2 — the project window.**  Parse `project.tiku` (G2 headers),
  Open Project… through the panel, rows open editor windows (one per
  file, re-pick raises), Window menu tracks them, dirty markers,
  the one closing question.  Scripts: open, edit, mark, raise, close.
- **P3 — the message window.**  Fed by the one backend that already
  exists and is proven: Run of a BASIC file through `tiku-basic`
  (absolute `$TIKU_BASIC`, as runbas.script does).  Output lines
  become rows; a planted syntax error proves the file:line link jumps.
  This closes the message contract (§1) before any C exists.
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
