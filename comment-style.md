# Comment style

How comments are written in TikuOS. The mechanical rules are checked by
`make lint`; the rest are the author's to keep and review's to catch.

## The rule under the others

A comment says what the code does, a constraint on it, or a hazard that is
still live, in the present tense. It does not tell how the code came to be:
git holds that history in full, and a comment that narrates its own past is
wrong after the next change.

A hazard that is still live is not history. State it and keep it:
`FUNCSEL is not uniform on this bus -- GP84-88 use 2, GP156-160 use 0` is a
fact about the hardware. `assuming it was uniform cost a debugging round` is
the story of finding it out. Keep the first, drop the second.

## Write for a reader who has only the code

A comment is read later, by a person or an agent who has the code in front
of them and nothing else: not the plan, not the alternatives that were
weighed, not the conversation that produced the change. It states facts
about the code as it is, in plain declarative sentences, and it makes sense
on its own.

It does not walk through the author's reasoning:

- **No comparison with a design the code does not use** ("instead of reading
  zeros as data", "rather than a special case"). If the other behaviour is a
  live hazard, state the hazard directly.
- **No narration of what a caller learns, knows or sees**, and no "cleanly",
  "honestly" or "safely" as decoration. Say what the caller gets: the return
  value, the state left behind.
- **No development state**: "yet", "for now", "not implemented yet", "until X
  lands". Say what the code does today. A plan goes in an issue or a commit
  message.
- **No metaphor**: name the thing. "Locks fs->hand, or adds one level if this
  thread already holds it", not "take the hand, or deepen a hold".

| reasoning | statement |
|---|---|
| `No hardware backend yet. Every call fails cleanly so a caller learns the bus is absent instead of reading zeros as data.` | `This port has no ADC driver: every call returns TIKU_ADC_ERR_PARAM, and tiku_adc_arch_read() also stores 0 in *value.` |
| `Every run is bounds-checked and claimed slot by slot, so an overlap between two files is caught here rather than found as corrupted content later.` | `Every run is bounds-checked and claimed slot by slot; two files that claim one slot fail the mount with TFS_ERR_CORRUPT.` |
| `so these platforms take the identical code path rather than a special case` | delete it: the sentence before it already says what happens |

A reason is a fact about the code, not an argument: `The source moves before
the dividers: the dividers count from the new source.` Each sentence should
still be true and useful if every other comment in the file were deleted.

## File header

Licence, author, and two or three lines on what the file is. At most 15
lines.

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

`.py` and `.sh` files use the same shape with `#`, after any `#!` line. An
`.inl` fragment, which a `.c` file includes, carries the same header as a
`.c` file, naming the fragment and what it holds. A file has one header.

## Doc comments

Functions, types and macros other files use carry a Doxygen comment where
they are declared; a static function carries one at its definition. At most
3 lines of prose; `@brief`, `@param`, `@return`, `@note` and their
continuation lines do not count.

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

A struct field or an enumerator carries a trailing `/**< ... */` comment
when its name does not say everything:

```c
typedef struct {
    const tiku_vfs_node_t *parent;   /**< directory the subtree sits in */
    const tiku_vfs_node_t *node;     /**< root of the mounted subtree   */
} vfs_mount_t;
```

Comments are `/* */` and `/** */`; `//` is not used.

## Inside a function

A body comment explains what the code cannot say for itself: why an order
matters, what a register does, what a caller relies on. It does not restate
the line below it, and it does not argue for the code (see Banned).

## Section banners

A longer file may group its parts under banners. The label is a name for the
section, in capitals:

```c
/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/
```

## Where longer content goes

- **Reference material**, such as a register map, a memory map or a command
  table, is a `/* */` body comment, not a doc comment. The 3-line cap is for
  what sits above a function.
- **Caller contracts**, such as "ISR context only", "must run before X" or an
  order a datasheet requires, go in `@note`.
- **Plans, milestones, measurements and working notes** do not belong in the
  source tree. A conclusion that outlives a change goes in that change's
  commit message.
- **Everything else** is deleted.

## Banned

| pattern | instead |
|---|---|
| `we`, `our`, `us` | name the thing: "the driver", "the caller", "this port" |
| `(P3e)`, `see A2b`, `phase 1`, `milestone 4` | describe the state, not the plan that reached it |
| `used to`, `shipped because`, `turned out`, `the mistake was` | say what the code does now |
| `SOURCE FIRST, THEN DIVIDERS` | a sentence: "The source moves before the dividers." |
| `and the order is the whole of it`, `is the whole fix` | delete it; the fact already made the point |
| `Verified, not assumed`, `Zero before copy, always`, `load-bearing`, `deliberately` | say what the code does without insisting on it |

A comment states a constraint; it does not argue for one. Capitals belong to
a register or field name (`SCKDIVCR2`, `RDY`) or a section banner, never to
emphasis. If a sentence reads like the last line of a paragraph, it is
decoration: cut it and check nothing was lost.

## Before every commit: read, don't grep

`make lint` is the mechanical half, and text that breaks every rule above can
pass it. It cannot see a comment that is false about the code beside it, the
register of the prose, or a comment that was true when written and made false
by a later change in the same commit. So before staging, read each new and
changed comment against this page and against the code it sits on:

- does it still describe what the code now does, after every change in the
  commit?
- did a later change make an earlier comment false?
- is any number in it a measurement? It belongs in the commit message.
- is the register plain, per the Banned table?
- does each comment state facts a reader with only the code can use, or does
  it walk through reasoning (see "Write for a reader who has only the code")?
- does every "every", "all", "only" and "never" in it hold on every path, and
  does every name it mentions exist in the code?

## Checking

`make lint` runs the comment checker, `tools/check_comment_style.py`, over the
tracked tree: the header and doc-comment caps, and the Banned vocabulary it
can match. `--strict` adds single capitalised words used for emphasis, plan
labels and the arguing words above, and counts an `@p` line and text on the
`/**` line as prose. A word the code uses as a name (a BASIC keyword, the last
part of a constant such as `TIKU_RESTART_NEVER`, a call such as `NOW()`) is
not emphasis. `make lint` runs `--strict` over the whole tracked tree. A file
git does not yet track is out of scope, so lint after staging. The vendor trees
(`arch/*/cmsis`, `arch/*/mdk`) and the nested repositories (`drivers/`,
`TikuBench/`, `tikukits/`) are skipped; a nested repository lints itself from
its own root. The checker does not read the Makefile or `*.mk` files: read
their comments against this page.

The desktop and its applications (the `applications` repository) follow this
page with denser ceilings: a file header of at most 20 lines and a doc comment
of at most 6 lines of prose. Its `tools/check_style.py` adds two rules: every
function is documented exactly once, by the comment right above one of its
declarations (a one-word group label documents nothing), and a file has at
most one section marker, a banner or a one-line divider. Its `make lint` runs
`--strict` over `system/`, `kits/` and `apps/`.
