# Prose style

How the Markdown in TikuOS is written: `README.md`, the `AGENTS.md` files,
the port READMEs and the contracts. The mechanical rules are checked by
`make lint` (`tools/check_prose_style.py`); the rest are the author's to
keep and review's to catch. Comments in code follow `comment-style.md`
and commit messages `hygiene/commit-style.md`.

## The rule under the others

A sentence states what the system is, what it does or what it is for. It
does not tell the reader how they will find it: not easy, simple, small,
quick, clean, elegant or powerful. Those are the reader's verdicts to reach
from the facts, and a page that reaches them first has nothing left to show.

| reassurance | statement |
|---|---|
| `a TikuOS program is a small, plain piece of C, quick to pick up` | `the kits (networking, TLS, MQTT, sensors, maths) provide the functionality for designing complex systems that interact with the physical world` |
| `BASIC is the simplest way to program a device by hand when a few lines will do` | `With agents writing the C, BASIC is the way for a person to program a device by hand, a few lines at the board rather than a C program` |
| `read and written with the same few commands` | `read and changed with simple shell commands, so agents and people interact with a board the same way` |

## Describe the whole feature

A sentence that introduces an example describes the feature, not the one
path the example shows. `The same blink in BASIC, typed at the board` was
false by omission: BASIC also runs a `.bas` file from `/data`. Before
writing "typed", "only", "always" or "every", check the code for the other
paths.

## State the focus; do not fence it

A focus is one phrase: `designed around the microwatt computer that runs
for years on a coin cell`. It is not followed by a clause that covers the
other cases (`but it runs on larger boards too`): the sentence before it
already named the wider class. If a qualifier is needed, it is a fact
(`the FR6989 has no BASIC`), not a hedge (`in most cases`, `for the most
part`).

## The author's voice

The README is the project's own statement, in the first person plural
where a decision was the project's: `we designed it from scratch`. The
reader is addressed directly when told what to do: `clone the tree`. A
sentence carries an idea and its purpose together: `... so agents and
people interact with a board the same way`.

## Every claim is checked against the code

A command, path, flag or keyword in prose exists in the tree: `basic run
/data/blink.bas` is the shell's form, `LED 0, 2` toggles because the
interpreter treats any value but 0 and 1 as a toggle. A number is one the
code or a build holds. Nothing is written from memory of what the system
probably does.

## What a page holds

- `README.md` says what TikuOS is, which boards run it, how to start, and
  shows the system in use. Build flags, tree layout and test commands live
  in `AGENTS.md`; a port's details in its own README.
- An `AGENTS.md` is written for an agent: facts, rules and proof, no
  introduction, no encouragement.
- Plans, milestones, measurements and session notes stay in `kintsugi/`,
  which is not committed.

## Banned

| pattern | instead |
|---|---|
| `easy`, `simple`, `simply`, `quick`, `quickly`, `just` (as in "just type") | say what the thing does; let the reader judge |
| `small`, `tiny`, `lightweight`, `minimal` as praise (`a minimal, elegant kernel`) | a size: `143 KB of FRAM`; `minimal` stays as a build profile name |
| `clean`, `cleanly`, `elegant`, `powerful`, `robust`, `seamless`, `nice` | the fact the word was standing in for |
| `in most cases`, `for the most part`, `generally`, `typically`, `usually` | the case, or the exception named |
| `phase 1`, `milestone 4`, `(P3e)`, `M6c` | describe the state; the plan is in `kintsugi/` |
| `used to`, `turned out`, `we found that`, `the mistake was` | what the system does now; git holds the history |
| `note that`, `it is worth noting`, `importantly`, `of course`, `obviously`, `basically` | delete the opener; keep the sentence |
| `feel free to`, `don't hesitate`, `happy to` | an instruction, or nothing |

## Checking

`make lint` runs `tools/check_prose_style.py` over every tracked `.md` file
outside the vendor trees and nested repositories. It reports the Banned
vocabulary it can match, a word in a fenced code block or inline code
excepted, and it does not read the sentences for truth: a claim that is
false about the code passes it. So before committing a Markdown change,
read each changed sentence against this page and against the code it
describes.
