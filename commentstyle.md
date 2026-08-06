# Comment and commit style

Comment rules are enforced by `tools/check_comment_style.py`, from `make lint`.
Commit rules are convention, not enforced.

## File header

Licence, author, 2–3 lines on what the file is. Max 15 lines.

```c
/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs.c - tree walker, path resolver, read/write dispatch, watch.
 *
 * Resolves slash-separated paths against a static tree of nodes and dispatches
 * to handler functions -- no malloc, no string copies, no inodes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
```

`.py` and `.sh` use the same shape with `#`, after any shebang.

## Doc comments

Max 3 lines of prose. `@brief`, `@param`, `@return`, `@note` and their
continuations do not count.

```c
/**
 * @brief Walk the VFS tree to resolve a slash-separated path.
 *
 * Descends through directory nodes, matching each component by linear scan.
 * A mismatch, a non-directory intermediate or a NULL root yields NULL.
 *
 * @param path  Absolute path
 * @return The node, or NULL
 */
```

## Banned

| pattern | instead |
|---|---|
| `we`, `our`, `us` | name the thing: "the driver", "the caller", "this port" |
| `(P3e)`, `see A2b` | describe the state, not the plan that reached it |
| `used to`, `shipped because`, `turned out`, `the mistake was` | state what the code does now |

Git holds the history. A comment that narrates how the code got here is a
comment that will be wrong after the next change.

A hazard that is still live is not history. State it in the present tense and
keep it: `FUNCSEL is not uniform on this bus -- GP84-88 use 2, GP156-160 use 0`
is a fact about the hardware. `assuming it was uniform cost a debugging round`
is the story of finding it out. Keep the first, drop the second.

## Where design history and milestones go

Not in the source tree. Plans, milestone numbering, measurements and working
notes live in `kintsugi/` and `experiments/`, both gitignored; conclusions that
outlive a session go in the commit message. The source tree carries what the
code is, not the route taken to it.

## Where longer content goes

- **Reference material** — register maps, memory maps, command tables,
  measurement results — is a `/* */` body comment, not a doc comment. The
  3-line cap is for what sits above a function.
- **Caller contracts** — "ISR context only", "must run before X", ordering
  requirements from a datasheet — go in `@note`.
- **Everything else** — delete it.

## Checking

```
make lint                      # whole tree
tools/check_comment_style.py kernel/vfs     # one subtree
```

Covers `.c` `.h` `.inl` `.ld` `.S` `.m` `.py` `.sh`.

Scope is what git tracks, so anything gitignored -- `kintsugi/`,
`experiments/`, `temp/`, `examples/`, `demos/` -- is out of scope by
construction, and a new scratch directory needs no change here.

AN UNCOMMITTED FILE IS ALSO OUT OF SCOPE, for the same reason and just as
silently: a brand-new source file is untracked until it is added, so a lint
run before the commit passes without opening it. That is the worst moment to
be told nothing -- it is exactly when a file has never been checked. Lint
AFTER staging, or check the file directly by path.

A NESTED REPO IS ALSO OUT OF SCOPE, and silently: `make lint` here reports
success without opening a file in `experiment/`. Such a tree lints itself
with `--root`, which points the walk and the git query at it so it is scoped
by its own tracking:

```
python3 ../tools/check_comment_style.py --root=$(pwd)   # or `make lint` there
```
 Vendor trees
(`arch/*/cmsis`, `arch/*/mdk`, `tools/fat32`) and submodule content
(`drivers/`, `TikuBench/`, `tikukits/`) are tracked but still skipped.

`check_durable_placement.sh` deliberately does NOT match that scope: it covers
`drivers/` too, because misplaced durable data is a correctness fault in the
linked image wherever it is written, not a style preference. `make lint` runs
both and reports both -- a failing check must not hide the other's findings.

## Commit messages

### One commit per capability

A commit is a thing that now works, not a step on the way there. Bring-up
that took six sessions of PLL experiments is one commit when it lands, not
six. Split only when the parts stand alone: a driver and the test suite that
exercises it are two commits; the driver and the four fixes it needed are one.

The question is not "did I do these at different times" but "would someone
reverting this want them separated".

### Subject

`Area: what now works`. Max 72 chars, no trailing period.

Areas are read by people, so they are names: `RA8P1 Port`, `Shell`, `VFS`,
`BASIC`, `Memory`, `TikuBench`, `Docs`. Say the capability plainly.

| no | yes |
|---|---|
| `ra8p1: 1 GHz, and the four-byte store that kept it away` | `RA8P1 Port: 1 GHz core clock with 240/480/1000 MHz ladder` |
| `ra8p1: the sleep rule above 240 MHz` | `RA8P1 Port: Core steps down to ICLK before sleep` |
| `memory: update the code window` | `Memory: One 384 KB code window for the whole fleet` |
| `basic: improve module handling` | `BASIC: Module image is a store file, not a carve` |

Four things that make a subject rot:

- **the activity, not the change** — `finish`, `trim`, `cleanup`, `update`,
  `improve`. They describe the session, not the diff.
- **milestone markers** — `M3.5`, `phase 1 complete`, `(S0-S6)`, `(A2b)`.
  Nobody holds that map later.
- **literary flourish** — `the four-byte store that kept it away`, `the
  plateau confesses`, `locate where 480 dies`. A subject is an index entry,
  not a title.
- **vague scope** — `the four big dirs`, `the standard`. Name them.

### Body

Bullet points. At most 5, one line each, `- ` prefix. What changed and how it
was checked. No prose paragraphs, no narrative.

```
RA8P1 Port: 1 GHz core clock with 240/480/1000 MHz ladder

- SCKDIVCR2 is 16-bit; the 32-bit store clobbered SCKSCR with a disabled
  oscillator select
- Rungs above 240 MHz run at VSCR_1; MRAM notifications retry until they
  read back
- Rung changes run with interrupts masked, PLL stop confirmed via PLLSF
- All six ordered rung pairs verified; shell suite 125/125 at 1000 MHz
- CAC agrees with the configured tree at every rung (0 ppt)
```

Do not write:

- the debugging path, or what was tried and abandoned — git holds it
- a file-by-file list, or anything else the diff already says
- a paragraph where a bullet does

No tool or assistant co-author trailers.
