# Comment and commit style

This whole folder is gitignored. The rules and their checkers are house
convention for this working copy, so a clone of TikuOS carries no opinion
about how to write a comment or a commit; `make lint` says so out loud when
`hygiene/` is absent rather than reporting a check that did not run.
`check_durable_placement.sh` deliberately stays in `tools/`: misplaced durable
data is a correctness fault in the linked image, not a style preference.

Comment rules are enforced by `hygiene/check_comment_style.py`, from `make lint`.
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

Max 3 lines of prose in the kernel; the applications tree, whose comments
are written denser, runs the checker with `--prose=6 --header=20` from its
own `make lint`. `@brief`, `@param`, `@return`, `@note` and their
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
| `SOURCE FIRST, THEN DIVIDERS`, `THE PAGE IS SPLIT BY WRITER` | a sentence: "The source moves before the dividers." |
| `and the order is the whole of it`, `is the whole fix` | delete it; the fact already carried the point |
| `Verified, not assumed`, `Zero before copy, always`, `load-bearing`, `deliberately` | say what the code does without insisting on it |

A comment states a constraint; it does not argue for one. ALL-CAPS openers,
aphoristic closers and emphasis words are one reflex -- writing to persuade a
reader that the ordering matters, instead of writing down what the code
requires. Capitals belong to a register or field name (`SCKDIVCR2`, `RDY`),
never to emphasis. If a sentence reads like the last line of a paragraph, it
is decoration: cut it and check nothing was lost. The ALL-CAPS half of this
is checked mechanically; the rest is not.

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

## Audit before every commit -- not `make lint`

`make lint` is the mechanical half, and it passes on text that breaks every
rule above.  It cannot see a comment that is FALSE about the code beside it,
it cannot see the register of the prose, and -- the case that keeps
recurring -- it cannot see a comment that was true when written and made
false by a later change in the same session.

This applies to the commit message as much as the source. Reading the
comment half of this document and then drafting a message from memory of the
rest is how the same flourish comes back: open the section you are about to
write in, every time.

So before staging, READ the new and changed comments against this document
and against the code they sit on:

- does each comment still describe what the function now does, after every
  change in this commit?
- did a later change falsify an earlier one?  (a payload that became
  position-dependent, a core whose caches were turned on, a fault path that
  stopped ending in LOCKUP -- all three shipped in one session)
- is any number in a comment a measurement?  it belongs in `kintsugi/`
- is the register plain, per the Banned table?

Grepping for banned words is not this audit: it finds the words it was given
and nothing else.

## Checking

```
make lint                      # whole tree
hygiene/check_comment_style.py kernel/vfs   # one subtree
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
python3 ../hygiene/check_comment_style.py --root=$(pwd)  # or `make lint` there
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

- **the session, not the work** — `finish`, `trim`, `cleanup`, `wip`, `more
  work on`. These date the subject to the sitting that produced it. (`Support
  for`, `Improvements to` and `Fixes for` are NOT in this class — see below.)
- **milestone markers** — `M3.5`, `phase 1 complete`, `(S0-S6)`, `(A2b)`.
  Nobody holds that map later.
- **literary flourish** — `the four-byte store that kept it away`, `the
  plateau confesses`, `locate where 480 dies`. A subject is an index entry,
  not a title.
- **vague scope** — `the four big dirs`, `the standard`. Name them.

### Name the work, not the mechanism

Subjects come back from review rewritten in one direction: a predicate saying
what the code now does becomes a noun phrase naming the work.

| written | accepted |
|---|---|
| `RA8P1 Port: Cortex-M33 coprocessor runs a payload` | `RA8P1 Port: Support for Co-processor Cortex-M33` |
| `RA8P1 Port: Kernel tick keeps real time at every clock rung` | `RA8P1 Port: Improvements to kernel tick` |
| `Arch: Comments match the code on RA8P1 and STM32N6` | `Comments: Fixes for Port on RA8P1 and STM32N6` |

`Support for X`, `Improvements to X`, `Fixes for X` are the right shape when
the commit is several related changes no single predicate covers -- a
subsystem arriving, a sweep, a set of repairs to one area. Reach for the
predicate only when one crisp capability genuinely IS the commit: `RA8P1 Port:
1 GHz core clock with 240/480/1000 MHz ladder`.

Either shape, the subject carries no mechanism. `INITVTOR`, `preserve_mask`,
`SCKDIVCR2` are body material at most. A subject that needs a register name to
parse was written for its author.

The line the hook draws is bare imperative versus noun: `Fix kernel tick` and
`Improve kernel tick` are refused, `Fixes for kernel tick` and `Improvements
to kernel tick` pass. A bare verb reports the sitting; the noun names the
work, so it still reads as an index entry a year later.

### Body

Bullet points. At most 3, `- ` prefix, plain English. What changed, why, and
how it was checked. No prose paragraphs, no narrative.

Three is a ceiling, not a target, and bullets get merged in review far more
often than they get added. Two bullets about the same area of work are one
bullet. A sweep of many small edits is one bullet and no tally: 129 comment
corrections were accepted as `Misc updates to the comments within the code for
consistency with the codebase`, not as a count by category. Numbers,
percentages and per-file breakdowns are what the diff is for.

A verification line earns its place when the claim is about hardware
behaviour -- `shell suite 125/125 at 1000 MHz` is worth a bullet because
nothing else records it. On a routine sweep it reads as padding.

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

### Plain language

Bullets are plain English facts a reviewer needs: what changed, why, how
it was checked.  Two or three of them.  No metaphor, no aphorism, no
turn of phrase, no `--` asides, no sentence that would read as prose from
a novel.  The test: could a stranger say what the diff does from the
bullet alone, without admiring it?

| no | yes |
|---|---|
| `A look asked during a look begins when it ends and its asker hears` | `A "Look again" clicked while a look is running is queued instead of dropped` |
| `The boards' wire leaves the loop` | `Board I/O runs on worker threads instead of the event loop` |
| `A hand in the session and one in the backend, since the loop reaches a board from dozens of places` | `Serial session and board backend are protected by a mutex` |
| `Two runs on two boards: the longest turn 4081 ms to 152 ms` | `Longest event-loop stall during a board run: 4081 ms before, 152 ms after` |

The same goes for plan and design documents: state the fact, then the
reason.  Voice is for comments that explain a hazard, and even there it
is one sentence.

### What a bullet carries

Density follows the subject shape. `Fixes for X` is about a mechanism, so the
body may name the register and the defect. `Support for X` is a subsystem
arriving, and a body that lists registers reads as a log of the author's week
rather than a description of the thing: say what the subsystem now does.

| subject | first bullet |
|---|---|
| `Fixes for the console printf` | `tiku_uart_printf() parses the '0' flag and field width; an unmatched conversion printed its specifier without consuming its argument` |
| `Support for the Ethos-U55 accelerator` | `Driver releases the NPU from its power domain and module stop, and runs Vela-compiled command streams on it` |

Both are an opening bullet. The first carries the register because the
register is the defect; under the second subject the same density says nothing
about what arrived.

A clause after `;` earns its place when it names the DEFECT -- that is the
change. It does not when it justifies a chosen value: the value is the change
and the tuning argument is not.

| no | yes |
|---|---|
| `TIKU_TIER_SRAM_SIZE is 256 KB; the 128-byte default sent BASIC's arena to NVM` | `TIKU_TIER_SRAM_SIZE is 256 KB` |
| `BASIC runs the BIG tier at 512 lines; the 50-line host profile could not hold a SHA256$ result` | `BASIC runs the BIG tier at 512 lines` |

A factual anchor is not a justification: `TFS floor is 96 slots against the 106
the 464 KB carve mounts` keeps its comparison because the comparison is what
makes 96 a number rather than a guess.

On the verification bullet the Body rule above has a second edge: one result
that is the only record of a claim is a bullet, but a LIST of them is a tally.
`filestore 21/21, basic 222/222, common 11/11` falls to the same rule as a
per-file breakdown.

Controls and negative results stay out entirely. That a marker is lost when the
region is left cacheable, that a corrupted weight changes the answer -- that is
how a change was proven, not what it is. Evidence lives in `kintsugi/`.

### Show the message before committing

An agent presents the subject, the author identity and the body for review
BEFORE running `git commit` -- not the diff, which the author already knows,
but the three things that are hard to change afterwards. A message is cheap to
fix in the thirty seconds before the commit and expensive after it is pushed.

```
Subject: RA8P1 Port: 1 GHz core clock with a 240/480/1000 ladder
Author:  Ambuj Varshney <ambuj.varshney@proton.me>
Body:
  - SCKDIVCR2 is 16-bit; the 32-bit store clobbered SCKSCR
  - All six ordered rung pairs verified; shell 125/125 at 1000 MHz
```

Show it for every commit, including one that only amends a message. Batch it
when several commits land together: all the subjects at once, then commit.

### Checking

A `commit-msg` hook refuses a message that misses the shape. It lives in
`hygiene/githooks/` and runs `hygiene/check_commit_msg.py`; `core.hooksPath`
is per-repository local config, so a fresh clone needs
`hygiene/install_hooks.sh` once, which points this repo and every nested one
at it.

```
hygiene/install_hooks.sh                    # after cloning
hygiene/check_commit_msg.py <file>          # check a message by hand
git commit --no-verify                      # bypass, for a fixup and no more
```

The hook checks shape: subject form, length, milestone markers, bullet bodies,
trailers, and that the author is this repository's configured identity rather
than one substituted by `-c user.email=`. It cannot see flourish, a body that
narrates the debugging path, or five commits that should have been one -- those
stay the author's job, which is what the review above is for.
