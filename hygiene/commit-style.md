# Commit style

`hygiene/` is what runs at commit time: this guide, `check_commit_msg.py`,
the `commit-msg` hook in `githooks/` and `install_hooks.sh`, which points
this repository and every nested one at the hook.  Comments have their own
guide, `comment-style.md` at the root, checked by
`tools/check_comment_style.py` from `make lint`; nothing about comments is
repeated here.

## One commit per capability

A commit is a thing that now works, not a step on the way there. Bring-up
that took six sessions of PLL experiments is one commit when it lands, not
six. Split only when the parts stand alone: a driver and the test suite that
exercises it are two commits; the driver and the four fixes it needed are one.

The question is not "did I do these at different times" but "would someone
reverting this want them separated".

## Subject

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

## Name the work, not the mechanism

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

## Body

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

## Plain language

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

## What a bullet carries

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

## Show the message before committing

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

## Checking

A `commit-msg` hook refuses a message that misses the shape. It lives in
`hygiene/githooks/` and runs `hygiene/check_commit_msg.py`; `core.hooksPath`
is per-repository local config, so a fresh clone needs
`hygiene/install_hooks.sh` once, which points this repo and every nested one
at it.

```
sh hygiene/install_hooks.sh                 # after cloning
hygiene/check_commit_msg.py <file>          # check a message by hand
```

A refused message is fixed, not bypassed: `--no-verify` is not used.

The hook checks shape: subject form, length, milestone markers, bullet bodies,
trailers, and that the author is this repository's configured identity rather
than one substituted by `-c user.email=`. It cannot see flourish, a body that
narrates the debugging path, or five commits that should have been one -- those
stay the author's job, which is what the review above is for.
