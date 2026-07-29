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

Covers `.c` `.h` `.inl` `.ld` `.S` `.m` `.py` `.sh`. Vendor trees
(`arch/*/cmsis`, `arch/*/mdk`, `tools/fat32`) and the separate repos
(`drivers/`, `TikuBench/`, `tikukits/`) are out of scope.

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
