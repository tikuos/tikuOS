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

### Subject

`area: the new state`. Max 72 chars. Areas are the subsystem — `basic`
`memory` `shell` `vfs` `process` `hal` `docs` — or the port — `nordic`
`ambiq` `rp2350` `msp430`.

Name what is now true, with the identifier or the number that makes it
checkable. A good subject still means something in a year, read alone.

| no | yes |
|---|---|
| `comments: finish arch/ambiq` | `ambiq: doc comments to 3 lines, 262 blocks` |
| `basic: improve module handling` | `basic: the module image is a store file, not a carve` |
| `memory: update the code window` | `memory: one 384 KB code window for the whole fleet` |
| `msp430: fix the ADC` | `msp430: fix ADC12_B channel->pin map, not common across the family` |

Four things that make a subject rot:

- **the activity, not the change** — `finish`, `trim`, `cleanup`, `update`,
  `improve`. They describe the session, not the diff.
- **milestone markers** — `M3.5`, `phase 1 complete`, `(S0-S6)`, `(A2b)`.
  Nobody holds that map later.
- **first person and flourish** — `three registers we never wrote`,
  `the plateau confesses`, `I had the wrong schematic`.
- **vague scope** — `the four big dirs`, `the standard`. Name them.

### Body

At most 5 lines: what it does and how it was checked.

```
memory: report app-usable SRAM, not the bank size

`free` printed the 256 KB bank, but the top 16 KB is the FLPR carve, so every
Nordic board over-reported by 16 KB. Devices now declare
TIKU_DEVICE_RAM_USABLE when it differs, falling back to RAM_SIZE.
Verified on the LM20: `free` reads 245760. Seven targets build.
```

Do not write:

- the debugging path, or what was tried and abandoned — git holds it
- a file-by-file list, or anything else the diff already says
- a section per concern, when one paragraph carries the change

No tool or assistant co-author trailers.
