# Comment style

Enforced by `tools/check_comment_style.py`, run from `make lint`.

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
