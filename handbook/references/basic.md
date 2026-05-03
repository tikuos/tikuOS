# Tiku BASIC — Definitive Guide

**Version 0.04**

Tiku BASIC is an opt-in interactive interpreter that runs as a single
shell command (`basic`) on TikuOS. It is intentionally tiny — small
enough to fit on a microcontroller — but covers enough of classic
BASIC plus a handful of TikuOS-specific bridges (GPIO, ADC, I2C, VFS,
LED) that you can write real, runnable embedded programs in it.

The interpreter is a single C file
(`kernel/shell/commands/tiku_shell_cmd_basic.c`) and uses the kernel's
arena allocator, persist store, MPU helpers, and clock — it does not
introduce a new memory subsystem. Its on-device cost is roughly
**~25 KB of code + 2.3 KB of arena memory** at default sizing.

---

## Table of Contents

- [When to use it](#when-to-use-it)
- [Hardware support matrix](#hardware-support-matrix)
- [Building](#building)
- [Sessions](#sessions)
- [Language reference](#language-reference)
  - [Syntax model](#syntax-model)
  - [Numeric types](#numeric-types)
  - [Fixed-point math](#fixed-point-math)
  - [Strings](#strings)
  - [Variables and arrays](#variables-and-arrays)
  - [Constants](#constants)
  - [Operators](#operators)
  - [Expressions](#expressions)
- [Statements](#statements)
  - includes WHILE/WEND, REPEAT/UNTIL, SWAP, multi-line IF, ON ERROR / RESUME, ON CHANGE, EVERY
- [Direct commands](#direct-commands)
  - includes AUTO, RENUM
- [Built-in functions](#built-in-functions)
  - includes SQR, SIN, COS, TAN, HEX$, BIN$
- [Hardware bridges](#hardware-bridges)
- [VFS bridge](#vfs-bridge)
- [Persistence](#persistence)
- [Autorun at boot](#autorun-at-boot)
- [Tracing and debugging](#tracing-and-debugging)
- [Configuration macros](#configuration-macros)
- [Error reference](#error-reference)
- [Limits and gotchas](#limits-and-gotchas)
- [Memory budget](#memory-budget)
- [Worked examples](#worked-examples)
- [Implementation notes](#implementation-notes)
- [Keyword index](#keyword-index)

---

## When to use it

Pick BASIC when you want any of:

- A **REPL** for poking at the running kernel — read sensors, blink
  LEDs, write VFS nodes interactively without rebuilding firmware.
- A **persistent script** that boots automatically (saved program
  + `basic run` registered with the init system).
- A **teaching surface** — students or collaborators who want to
  drive the hardware in 5 lines instead of writing C.
- An **on-device data sketcher** — small loops that read ADC, do a
  bit of math, and write results back to a VFS node.

Pick something else (C, an example app) when you need:

- Sub-millisecond response times — BASIC dispatches per statement
  through a recursive-descent parser, so a tight loop runs at maybe
  ~5 KIPS at 8 MHz.
- Multi-process / event-driven structure — BASIC programs are
  single-threaded and run synchronously inside the shell command.
- Strict memory budgets — the interpreter alone is ~25 KB of code.

---

## Hardware support matrix

BASIC requires the **large memory model** (`MEMORY_MODEL=large`)
because the interpreter pushes most builds past the 48 KB lower-FRAM
ceiling. Large mode in turn requires the chip to have HIFRAM:

| MCU | FRAM | HIFRAM | BASIC supported |
|---|---|---|---|
| MSP430FR2433 | 16 KB | no | **No** |
| MSP430FR5969 | 64 KB | no | **No** |
| MSP430FR5994 | 256 KB | yes | **Yes** |
| MSP430FR6989 | 128 KB | yes | **Yes** |

The Makefile rejects unsupported combinations at parse time with an
actionable error. The override hatch
(`TIKU_SHELL_BASIC_ALLOW_SMALL=1`) bypasses the BASIC-needs-large
check, but the underlying HIFRAM check stays — it is structurally
impossible to compile BASIC for a part that has nowhere to spill
text into.

---

## Building

```bash
# Recommended invocation
make flash MCU=msp430fr5994 \
     TIKU_SHELL_ENABLE=1 \
     TIKU_SHELL_BASIC_ENABLE=1 \
     MEMORY_MODEL=large \
     TIKU_SHELL_COLOR=1                   # optional ANSI color
```

For more comfortable program sizes (default line width is 48 chars
and program holds 24 lines), pass:

```bash
EXTRA_CFLAGS="-DTIKU_BASIC_LINE_MAX=80 -DTIKU_BASIC_PROGRAM_LINES=48"
```

Then connect:

```bash
make monitor PORT=/dev/ttyACM1 BAUD=9600
```

(The default UART baud is 9600; pass `UART_BAUD=115200` at build time
and `BAUD=115200` at monitor time if you want faster.)

At the shell prompt, type:

```
tikuOS> basic
```

You'll see the BASIC banner and the `ok>` prompt.

---

## Sessions

There are three ways to enter BASIC:

### Interactive REPL
```
tikuOS> basic
Tiku BASIC ready. HELP / BYE.
ok> PRINT "hello"
hello
ok> BYE
bye.
tikuOS>
```

### Run a saved program
```
tikuOS> basic run
[basic] autorun
... output ...
tikuOS>
```

This is `LOAD` + `RUN` in one shot, no REPL, no banner. Suitable for
scripts and for the init system.

### Autorun at boot
Add `basic run` to the init table; the saved program runs every boot
before the regular shell prompt comes up.

```
tikuOS> init add 50 basic basic run
OK: 'basic' at seq 50
```

Reboot. BASIC's saved program executes during the init phase.

---

## Language reference

### Syntax model

BASIC programs are **line-numbered**:

```
10 LET A = 5
20 PRINT A
30 GOTO 20
```

Lines are 1..65534 (decimal, hex via `0x` prefix, or binary via `0b`).
Re-typing a line number replaces it. Typing just a line number deletes
it. Lines without a number execute immediately:

```
ok> PRINT 2 + 3
5
```

The maximum line length is `TIKU_BASIC_LINE_MAX` chars (default 48,
overridable at build time). The maximum program size is
`TIKU_BASIC_PROGRAM_LINES` lines (default 24).

A line may carry multiple statements separated by colons:

```
10 LET A = 5 : LET B = 7 : PRINT A + B
```

Lines beginning with a `letter:` token are labels that `GOTO` and
`GOSUB` can target by name:

```
10 GOTO finish
20 PRINT "skipped"
30 finish: PRINT "reached"
```

### Numeric types

All numbers are **32-bit signed integers** internally (range
±2,147,483,647).

Literals:

| Form | Example | Value |
|---|---|---|
| Decimal | `42` | 42 |
| Hex (C-style) | `0xFF`, `0X1A` | 255, 26 |
| Binary (C-style) | `0b1010`, `0B11` | 10, 3 |
| Hex (BASIC-style) | `&HFF`, `&hFf` | 255 |
| Binary (BASIC-style) | `&B1010`, `&b1` | 10, 1 |
| Decimal with `.` | `1.5`, `0.001` | 1500, 1 (Q.3 — see below) |

Line numbers accept the C-style hex/binary prefixes (a leading `0`
makes them look numeric to the line-storage path), but not the BASIC
`&H` / `&B` style.

### Fixed-point math

Numbers with a `.` are **Q.3 fixed-point** (×1000) by convention. The
parser scales them on input:

| Source | Stored as |
|---|---|
| `1.5` | `1500` |
| `0.001` | `1` |
| `1.` | `1000` |
| `1.5555` | `1555` (4th frac digit truncated) |
| `3.14159` | `3141` |
| `-2.5` | `-2500` (unary minus on `2500`) |

Addition and subtraction work directly:

```basic
LET A = 1.5 + 0.5      ' 1500 + 500 = 2000 = 2.0
LET B = 1.5 - 0.25     ' 1500 - 250 = 1250 = 1.25
```

Multiplication / division by a pure integer also work:

```basic
LET C = 1.5 * 2        ' 3000 = 3.0
LET D = 3.0 / 2        ' 1500 = 1.5
```

Multiplication / division between **two** fixed-point operands needs
explicit helper functions because the scale would otherwise compound:

```basic
LET E = FMUL(1.5, 2.0)     ' (1500 * 2000) / 1000 = 3000 = 3.0
LET F = FDIV(3.0, 2.0)     ' (3000 * 1000) / 2000 = 1500 = 1.5
```

To display a fixed-point value with the decimal in place:

```basic
PRINT FSTR$(1500)             ' "1.500"
PRINT FSTR$(PI)               ' "3.142"
PRINT FSTR$(-2.5)             ' "-2.500"
```

Or use `PRINT USING` with a literal `.`:

```basic
PRINT USING "##.###"; 1500    ' " 1.500"
```

`PI` is `3142` — the existing constant, which is exactly Q.3 π.

**Range and precision**: Q.3 gives 3 decimal places (precision
0.001) over a range of ±2,147,483.647. Multiplication has a 64-bit
intermediate, so each operand can be up to ~46,341 (~46.341 in
Q.3) without overflow. For typical sensor / geometry math this is
ample.

### Strings

26 string variables `A$..Z$`. Each is either unbound (reads as `""`)
or points into a 512-byte heap that **resets at every `RUN`**.

String literals use `"..."` and recognise these escapes:

| Escape | Result |
|---|---|
| `\n` | newline |
| `\t` | tab |
| `\r` | carriage return |
| `\"` | literal `"` |
| `\\` | literal `\` |
| `\<other>` | `<other>` literally |

Concatenation is `+`:

```basic
LET A$ = "Hello"
LET B$ = A$ + ", world!"      ' "Hello, world!"
```

The maximum length of any single string-expression result is
`TIKU_BASIC_STR_BUF_CAP` (default 64 bytes). The heap budget is
`TIKU_BASIC_STR_HEAP_BYTES` (default 512 bytes per RUN).

### Variables and arrays

| Kind | Notation | Default | Reset by |
|---|---|---|---|
| Numeric variables (26) | `A`..`Z` | 0 | `RUN` start |
| String variables (26) | `A$`..`Z$` | empty (NULL) | `RUN` start |
| Integer arrays (up to 26) | `DIM A(10)` then `A(i)` | 0 | `RUN` start |
| 2D integer arrays | `DIM A(m, n)` then `A(i, j)` | 0 | `RUN` start |
| String arrays (up to 26) | `DIM A$(10)` then `A$(i)` | empty | `RUN` start |
| 2D string arrays | `DIM A$(m, n)` then `A$(i, j)` | empty | `RUN` start |

`DIM` allocates from the BASIC arena. Numeric `A` and string `A$`
are independent slots — you can DIM both with the same letter.
Re-DIMming the same name in one session is an error:

```basic
DIM A(10)         ' OK -- numeric array A
DIM A$(10)        ' OK -- string array A$ (separate slot)
DIM A(20)         ' ? array A already DIMmed
```

1D arrays are indexed from 0:

```basic
DIM B(5)
B(0) = 100
B(1) = 200
PRINT B(0) + B(1)   ' 300
```

2D arrays are stored row-major; access via `A(row, col)`:

```basic
DIM M(3, 3)
M(1, 2) = 42
PRINT M(1, 2)       ' 42
```

String arrays use the same string heap as scalar string vars:

```basic
DIM N$(3)
N$(0) = "alice"
N$(1) = "bob"
N$(2) = "carol"
FOR I = 0 TO 2
  PRINT N$(I)
NEXT I
```

Maximum elements per array: `TIKU_BASIC_ARRAY_MAX` (default 128).
Total element count (m × n for 2D) must also fit within the cap.

### Constants

| Name | Value |
|---|---|
| `TRUE` | 1 |
| `FALSE` | 0 |
| `PI` | 3142 (= 3.142 in Q.3) |

These are matched as identifiers before single-letter variables, so
`T` / `F` / `P` remain available as plain numeric vars.

### Operators

Operators in **decreasing** precedence:

| Level | Operators | Notes |
|---|---|---|
| Function call / array | `name(...)`, `A(i)` | tightest |
| Primary | literal, paren, var | |
| Unary | `-`, `+`, `NOT` | bitwise NOT |
| Multiplicative | `*`, `/` | integer division truncates |
| Additive | `+`, `-` | |
| Relational | `=`, `<`, `>`, `<=`, `>=`, `<>` | yields 1 / 0 |
| Logical AND | `AND` | bitwise (matches MS BASIC integer dialect) |
| Logical OR | `OR`, `XOR` | bitwise |

There is no boolean type — relational operators yield `1` (true) or
`0` (false), and `AND`/`OR`/`XOR`/`NOT` operate bitwise on integers.

### Expressions

Expressions are parsed by recursive descent. The grammar (informal):

```
parse_expr  := expr_or
expr_or     := expr_and ((OR | XOR) expr_and)*
expr_and    := expr_rel (AND expr_rel)*
expr_rel    := expr_sum [relop expr_sum]
expr_sum    := expr_term ((+|-) expr_term)*
expr_term   := expr_unary ((*|/) expr_unary)*
expr_unary  := (-|+|NOT)? expr_prim
expr_prim   := number | TRUE | FALSE | PI | call | array | var | (expr)
```

String expressions live in a parallel grammar reached from PRINT,
LET, INPUT, and IF-condition contexts:

```
strexpr  := strprim (+ strprim)*
strprim  := strliteral | strvar | strfn(...)
```

String comparison (`A$ = B$`, etc.) is supported only at the
top level of an `IF` condition.

---

## Statements

### Assignment

```basic
LET A = 5            ' explicit
A = 5                ' implicit (LET keyword optional)
LET A$ = "hi"        ' string variable
A(3) = 99            ' array element (after DIM A(...))
```

### `PRINT` / `?`

```basic
PRINT "hello"             ' literal
PRINT 2 + 3 * 4           ' arithmetic
PRINT A; B; C             ' no separator between values
PRINT A, B                ' single space between values
PRINT "x ="; X            ' mixed string and numeric
PRINT A$ + " world"       ' string concatenation
?  "alias"                ' '?' is an alias for PRINT
```

`PRINT` ends with a newline unless the line ends in `;` or `,`.

### `PRINT USING`

```basic
PRINT USING "###"; 42        ' " 42"
PRINT USING "#####"; 7       ' "    7"
PRINT USING "##"; 9999       ' "**"     -- overflow
PRINT USING "##.###"; 1500   ' " 1.500"  -- literal '.'
```

`#` positions are right-aligned digit slots, space-padded. Anything
else is taken literally. Negative values use one digit position for
the leading `-`.

### `IF` / `THEN` / `ELSE` / `END IF`

**Single-line form** (existing behaviour):

```basic
IF A > 5 THEN PRINT "big"
IF A > 5 THEN PRINT "big" ELSE PRINT "small"
IF A > 5 THEN 100               ' shorthand for GOTO 100
IF A = 1 THEN A = 5 : B = 10    ' multi-stmt body via ':'
```

The THEN / ELSE bodies absorb everything up to the next ELSE / line
end. Quoted strings are scanned for an unquoted `ELSE`, so a literal
`"... ELSE ..."` doesn't trigger the keyword.

**Multi-line form** — when `IF cond THEN` is followed by nothing
else on the line (only whitespace before EOL or `:`), the body
spans subsequent lines until a matching `ELSE` and / or `END IF`
keyword on its own line:

```basic
10 IF A > 5 THEN
20   PRINT "big"
30 ELSE
40   PRINT "small"
50 END IF
```

`END IF` and `ENDIF` are both accepted. Multi-line IFs nest cleanly:
the depth-aware scanner matches each `ELSE` / `END IF` to its
opening `IF`. Multi-line form requires RUN context (the runner has
to consult subsequent lines).

**String comparisons inside IF:**

```basic
IF A$ = "admin" THEN GOTO 100
IF A$ <> "" THEN PRINT "got: " + A$
IF "abc" < "abd" THEN PRINT "lex"
```

These work only at the top level of the IF condition — not as
subexpressions of mixed-type formulas.

### `GOTO` / `GOSUB` / `RETURN`

```basic
10 GOTO 50                  ' jump to line 50
20 GOSUB 100                ' call line 100, RETURN comes back
30 GOTO done                ' jump to label
40 RETURN                   ' from a GOSUB
```

Labels are `name:` at the start of a line. Names need at least 2
characters (single letters collide with variable names).

### `FOR` / `TO` / `STEP` / `NEXT`

```basic
10 FOR I = 1 TO 10
20 PRINT I
30 NEXT I
```

```basic
10 FOR I = 100 TO 0 STEP -10     ' negative step
20 PRINT I
30 NEXT I
```

The loop body **must occupy separate lines** from `FOR` and `NEXT`.
A same-line body like:

```basic
10 FOR I = 1 TO 5 : PRINT I : NEXT I       ' BUG: body skipped
```

is *not* supported — the loop-back line pointer is set to the line
*after* the FOR statement, so a same-line body never runs. This is a
known limitation of the simple loop-frame design.

`FOR` depth: `TIKU_BASIC_FOR_DEPTH` (default 4).

### `WHILE` / `WEND`

Pre-tested loop. Body runs as long as the condition is non-zero;
WEND jumps back to the WHILE for re-evaluation. Cleanly nested and
condition can be any numeric expression.

```basic
10 LET I = 0
20 WHILE I < 5
30   PRINT I
40   I = I + 1
50 WEND
```

`WHILE` and `WEND` must be on separate lines from the body (same
constraint as `FOR` / `NEXT`).

`WHILE` / `REPEAT` share a common loop-frame stack of depth
`TIKU_BASIC_LOOP_DEPTH` (default 4).

### `REPEAT` / `UNTIL`

Post-tested loop. The body always runs at least once; `UNTIL`
evaluates a condition and either pops (cond non-zero, loop
finished) or jumps back to the line after `REPEAT`.

```basic
10 LET I = 0
20 REPEAT
30   PRINT I
40   I = I + 1
50 UNTIL I = 3
```

### `SWAP`

```basic
SWAP A, B          ' exchange two numeric vars
SWAP A$, B$        ' exchange two string vars
```

Both operands must be the same type — mixed numeric / string is
an error. Cannot SWAP array elements; only scalar variables.

### `INPUT`

```basic
INPUT N                       ' numeric: prompts "? " and reads
INPUT "Name"; A$              ' string with prompt
INPUT "x"; X                  ' numeric with prompt
```

The `?`-prompt is fixed; the optional quoted prefix is printed before
it. Reading happens via the shell I/O backend (UART by default).

### `END` / `STOP`

Both terminate a running program and return to the REPL. They are
synonyms.

### `REM` and `'`

Both are line-end comments — they consume the rest of the line,
including any colons:

```basic
10 REM this is a comment : PRINT "not run"
20 LET A = 5 ' inline comment
```

### `DIM`

```basic
DIM A(10)
DIM B(5), C(20)              ' multiple in one stmt
```

Element count is `TIKU_BASIC_ARRAY_MAX` max. Elements zero-init.

### `DEF FN`

Single-line user functions, up to **4 arguments** each:

```basic
10 DEF FN sq(x) = x * x
20 DEF FN c2f(c) = c * 9 / 5 + 32
30 DEF FN add(x, y) = x + y
40 DEF FN clamp(v, l, h) = MIN(MAX(v, l), h)
50 PRINT sq(7), c2f(100), add(3, 4), clamp(50, 0, 10)
```

Up to `TIKU_BASIC_DEFN_MAX` definitions (default 4); each may take
up to `TIKU_BASIC_DEFN_ARGS` arguments (default 4). Function names
must be ≥ 2 characters and arguments must be single-letter
variables. Function bodies are stored as text and re-parsed on
each call. All argument variables' previous values are saved and
restored, so `DEF FN inc(X)` doesn't clobber a caller's `X`.

### `DATA` / `READ` / `RESTORE`

```basic
10 DATA 10, 20, 30
20 DATA "alpha", "beta"
30 READ A, B, C
40 READ N$, M$
50 PRINT A; B; C; N$; M$
60 RESTORE
70 READ X
80 PRINT X                 ' 10 again
```

DATA is parsed in line-number order. Numeric and string DATA items
can be mixed. Reading past the last item is an error
(`? out of DATA`).

### `ON expr GOTO/GOSUB`

```basic
10 LET A = 2
20 ON A GOTO 100, 200, 300       ' selects target N
```

`A=1` jumps to line 100, `A=2` to 200, etc. Out-of-range (including
0 and negative) silently falls through, matching MS BASIC.

`ON expr GOSUB` works the same way but pushes a return address.

### `ON ERROR GOTO line` / `RESUME`

Install a run-time error handler. When any subsequent statement
errors, instead of aborting the run, the error message prints and
control jumps to `line`.

```basic
10 ON ERROR GOTO 100
20 LET A = 5 / 0       ' fires the handler instead of stopping
30 PRINT "after"
40 END
100 PRINT "caught"
110 RESUME NEXT        ' continue from the line AFTER the offender (line 30)
```

Forms of `RESUME`:

| Form | Effect |
|---|---|
| `RESUME` | Re-execute the line that errored |
| `RESUME NEXT` | Continue from the line after the one that errored |
| `RESUME line` | Jump to a specific line |

Disable the handler with:

```basic
ON ERROR GOTO 0
```

The handler is not re-entrant — an error inside the handler is
fatal (avoids infinite loops on broken handlers).

### `ON CHANGE "/path" GOTO/GOSUB line`

Reactive VFS-watch handler. The RUN loop polls the path between
program statements, and when its integer value changes, jumps to
the handler line. With `GOSUB`, the handler is expected to
`RETURN`.

```basic
10 ON CHANGE "/dev/button" GOSUB 200
20 PRINT "watching..."
30 GOTO 30
200 PRINT "button pressed"
210 RETURN
```

Up to `TIKU_BASIC_ONCHG_MAX` registrations (default 4). The
"baseline" value is captured at register time so a newly-registered
watch never fires on its own first read. Cleared at every RUN start.

### `EVERY ms : stmt`

Recurring statement — fires every `ms` milliseconds, polled by
the RUN loop between program lines.

```basic
10 LET COUNT = 0       ' use a single letter; multi-char shown for clarity
20 EVERY 1000 : C = C + 1
30 GOTO 30             ' main program: idle
```

Up to `TIKU_BASIC_EVERY_MAX` registrations (default 4). Statement
text is captured up to `TIKU_BASIC_EVERY_STMT_LEN` chars (default
32). Errors inside an EVERY-registered statement deactivate that
particular registration (rather than firing repeatedly forever).
All registrations clear at every RUN start.

### `TRACE ON` / `TRACE OFF`

```basic
TRACE ON
RUN
```

Each line is echoed (in dim cyan, if colors are enabled) before
execution. Useful for understanding control flow in saved programs.

### `DELAY ms` / `SLEEP s`

Busy-wait, polling Ctrl-C between iterations:

```basic
DELAY 500                ' wait ~500 ms
SLEEP 2                  ' wait ~2 seconds
```

Both honour the system clock at `TIKU_CLOCK_SECOND` Hz (default 128
on MSP430). `DELAY` accepts up to ~32 s safely (16-bit tick wrap);
`SLEEP` is capped at 60 s. For longer waits, chain them.

### `CLS`

Sends ANSI clear-screen + cursor-home (`\033[2J\033[H`). On non-VT100
backends the bytes are harmless noise.

### `REBOOT`

Configures the watchdog for a short interval and spins. The chip
resets within ~2 ms. Code after `REBOOT` is unreachable; the next
boot is normal.

(REBOOT auto-disables on host harness builds since the spin would
hang the test driver.)

---

## Direct commands

These work only at the REPL prompt, not inside a stored program.

| Command | Effect |
|---|---|
| `LIST` | Print the current program in line order |
| `RUN` | Execute the stored program from the lowest line |
| `NEW` | Clear the program and string heap |
| `SAVE` | Save program to the default FRAM slot ("prog") |
| `SAVE "name"` | Save to a named slot (3 slots, 192 B each) |
| `LOAD` | Load the default-slot program |
| `LOAD "name"` | Load a named slot |
| `DIR` | List all named slots with their byte counts |
| `AUTO [start [, step]]` | Auto-line-number prompt: e.g. `AUTO 100, 5` types `100 ` for the next line, then `105 `, then `110 `... |
| `AUTO OFF` (or empty line) | Exit auto-line mode |
| `RENUM [start [, step]]` | Renumber all program lines; rewrites `GOTO` / `GOSUB` / `THEN` / `ELSE` references quote-aware (defaults: 100, 10) |
| `HELP` | Print the keyword summary |
| `BYE` / `EXIT` / `QUIT` | Leave BASIC, return to the shell |

`RENUM` is best-effort: if a rewritten line would exceed
`TIKU_BASIC_LINE_MAX`, the operation aborts with an error and the
program is left unchanged.  Lines inside `"..."` strings (e.g. a
literal `"go to 50"`) are scanned but not rewritten.

---

## Built-in functions

Numeric (return long):

| Function | Description |
|---|---|
| `RND(n)` | Pseudo-random integer in `[0, n)`, seeded from `tiku_clock_time()` on first call |
| `ABS(x)` | Absolute value |
| `INT(x)` | Identity for integer dialect (forward hook for fixed/float) |
| `SGN(x)` | -1 / 0 / 1 |
| `MIN(a, b)` | Smaller of two |
| `MAX(a, b)` | Larger of two |
| `MOD(a, b)` | `a % b` (errors if `b == 0`) |
| `SHL(a, n)` | Logical left shift; clamps `n` to `[0, 32)` |
| `SHR(a, n)` | Logical right shift; clamps `n` to `[0, 32)` |
| `MILLIS()` | Milliseconds since boot (16-bit-tick wraparound; ~32 s before reset) |
| `SECS()` | Seconds since boot (32-bit, no practical wrap) |
| `LEN(s$)` | Length of a string |
| `ASC(s$)` | First character's code (0 if empty) |
| `VAL(s$)` | `strtol(s, base=0)` — auto-detects `0x` prefix |
| `FMUL(a, b)` | Q.3 multiplication, 64-bit intermediate |
| `FDIV(a, b)` | Q.3 division (errors on `b == 0`) |
| `SQR(x)` | Q.3 square root (bit-by-bit isqrt). Negative input → 0 |
| `SIN(x)` / `COS(x)` / `TAN(x)` | Q.3 trig with 65-entry quarter-circle LUT + linear interp. Input in radians × 1000. `TAN` errors at the singularities (cos = 0). |
| `PEEK(addr)` | Read one byte at `addr` (volatile cast) — gated on `TIKU_BASIC_PEEK_POKE_ENABLE` |
| `DIGREAD(p, n)` | GPIO pin read (0 / 1) |
| `ADC(ch)` | ADC channel raw read (12-bit at AVCC by default) |
| `I2CREAD(addr, reg)` | Write one byte (the register), then read one byte |
| `VFSREAD("/path")` | Read a VFS node, parse leading int (auto base) |

String-returning (require parentheses):

| Function | Description |
|---|---|
| `LEFT$(s, n)` | First `n` characters |
| `RIGHT$(s, n)` | Last `n` characters |
| `MID$(s, i, n)` | Substring starting at 1-based `i`, `n` chars (or rest if `n` omitted) |
| `CHR$(n)` | Single-character string with code `n` |
| `STR$(n)` | Decimal string of an integer |
| `HEX$(n)` | 32-bit two's-complement hex (no `0x` prefix). `HEX$(255) = "FF"`, `HEX$(-1) = "FFFFFFFF"` |
| `BIN$(n)` | Binary, leading zeros stripped. `BIN$(10) = "1010"` |
| `FSTR$(x)` | Decimal string of a Q.3 fixed-point: `1500 → "1.500"` |

---

## Hardware bridges

These call into the kernel's HAL directly. All are gated by the
matching `TIKU_BASIC_<x>_ENABLE` flag (default 1) so a slim BASIC
build can drop them.

### LED

```basic
LED 0, 1                      ' LED 0 on
LED 1, 0                      ' LED 1 off
LED 0, 2                      ' LED 0 toggle (any val ≠ 0/1)
```

Bridges to `tiku_led_*`. The LED count is board-defined
(`TIKU_BOARD_LED_COUNT`); reading past it errors.

### GPIO

```basic
PIN 4, 6, 1                   ' P4.6 as output
PIN 1, 0, 0                   ' P1.0 as input
DIGWRITE 4, 6, 1              ' P4.6 high
DIGWRITE 4, 6, 2              ' P4.6 toggle
LET V = DIGREAD(1, 0)         ' read P1.0
```

Mode: 0 = input, 1 = output. Write value: 0 / 1, anything else
toggles. Bad port/pin reports `? bad GPIO Px.y`.

### ADC

```basic
LET T = ADC(10)               ' read channel 10
```

The first call lazy-initialises the ADC at 12-bit / AVCC reference.
Subsequent calls re-init the channel registration (idempotent under
the kernel API). Range 0..31; out-of-range errors.

### I2C

```basic
I2CWRITE 0x48, 0x01, 0xFF     ' write [reg=0x01, val=0xFF] to addr 0x48
LET V = I2CREAD(0x48, 0x05)   ' read register 0x05
```

The first call lazy-initialises the I2C bus at 100 kHz. Errors on
bus failure (`? I2C write/read failed`).

### Direct memory access

```basic
POKE 0x1234, 0xFF             ' write byte
LET V = PEEK(0x1234)          ' read byte
```

Use to poke SFRs from BASIC. Available only on real hardware
(gated by `PLATFORM_MSP430` even when the host harness has the
flag enabled, in case you need a flat byte buffer for testing).

---

## VFS bridge

BASIC can read and write any VFS node by quoted-literal path:

```basic
VFSWRITE "/dev/led0", 1                 ' turn LED 0 on via VFS
LET F = VFSREAD("/sys/cpu/freq")        ' 8000000
LET T = VFSREAD("/dev/adc/temp")        ' on-chip temp
LET V = VFSREAD("/sys/uptime")
PRINT V; " seconds"
```

`VFSREAD` parses the leading integer (decimal, or `0x`-prefixed hex
via `strtol(base=0)`). If the node returns non-numeric text (e.g.
`"running"`), `VFSREAD` returns 0 without setting an error — the
program can keep going.

`VFSWRITE` writes the integer rendered in decimal.

The path is parsed as an inline string literal — no escape
handling, no string-variable substitution. For path 48 chars max.

---

## Persistence

### Default slot

`SAVE` (no name) writes the current program to the FRAM-backed
persist key `"prog"`. `LOAD` (no name) reads it back. The buffer
size is `TIKU_BASIC_SAVE_BUF_BYTES` (default ~1.3 KB).

```basic
ok> 10 PRINT "saved"
ok> SAVE
saved 14 bytes
ok> NEW
ok> LIST                       ' (empty)
ok> LOAD
loaded 14 bytes
ok> LIST
10 PRINT "saved"
```

### Named slots

Three additional slots, each 192 bytes:

```basic
ok> 10 PRINT "blink"
ok> SAVE "blink"
saved 14 bytes to 'blink'

ok> 10 PRINT "scope"
ok> SAVE "scope"
saved 14 bytes to 'scope'

ok> DIR
  name      size
  blink        14 B
  scope        14 B

ok> LOAD "blink"
loaded 14 bytes from 'blink'
ok> RUN
blink
```

Slot names are up to 7 characters. Re-saving an existing name
replaces; saving when all 3 slots are full errors with
`? all slots in use`. There is no `KILL "name"` yet — clear the
slot by `NEW`-ing the program and re-saving.

The slot table lives in the `.persistent` FRAM section; programs
survive power cycles and reboots.

---

## Autorun at boot

Combine the saved program with the init system:

```
tikuOS> init add 50 myprog basic run
OK: 'myprog' at seq 50
```

On every boot, `init_run_all()` fires `basic run`, which loads the
default-slot program and executes it. Disable without removing:

```
tikuOS> init disable myprog
```

The init table is FRAM-backed too, so a fresh-built firmware on the
same device picks up the existing autorun configuration.

---

## Tracing and debugging

`TRACE ON` echoes each line before executing it:

```
ok> 10 FOR I = 1 TO 3
ok> 20 PRINT I*I
ok> 30 NEXT I
ok> TRACE ON
ok> RUN
[10] FOR I = 1 TO 3
[20] PRINT I*I
1
[30] NEXT I
[20] PRINT I*I
4
[30] NEXT I
[20] PRINT I*I
9
[30] NEXT I
ok>
```

`TRACE OFF` disables. The flag persists across REPL iterations
within a single BASIC session; it resets to OFF on each new
`basic` invocation.

Errors print the offending line:

```
? division by zero
at line 30
```

Ctrl-C during RUN prints `^C break at line N` and returns to the
prompt with the program halted (variables retain their values).

---

## Configuration macros

All compile-time, all wrapped in `#ifndef` so `EXTRA_CFLAGS` overrides
win.

### Sizing
| Macro | Default | Purpose |
|---|---|---|
| `TIKU_BASIC_LINE_MAX` | 48 | Max chars per line |
| `TIKU_BASIC_PROGRAM_LINES` | 24 | Max program lines |
| `TIKU_BASIC_GOSUB_DEPTH` | 8 | Max GOSUB nesting |
| `TIKU_BASIC_FOR_DEPTH` | 4 | Max FOR nesting |
| `TIKU_BASIC_LOOP_DEPTH` | 4 | Max WHILE / REPEAT nesting (shared) |
| `TIKU_BASIC_DEFN_MAX` | 4 | Max DEF FN slots |
| `TIKU_BASIC_DEFN_ARGS` | 4 | Max DEF FN arguments |
| `TIKU_BASIC_DEFN_BODY` | 40 | Max chars per DEF FN body |
| `TIKU_BASIC_ARRAY_MAX` | 128 | Max elements per DIM (1D or 2D total) |
| `TIKU_BASIC_ARRAY_TOTAL_LONGS` | 128 | Arena reservation for array data |
| `TIKU_BASIC_STR_HEAP_BYTES` | 512 | String heap per RUN |
| `TIKU_BASIC_STR_BUF_CAP` | 64 | Max chars in one string-expr result |
| `TIKU_BASIC_EVERY_MAX` | 4 | Max active EVERY registrations |
| `TIKU_BASIC_EVERY_STMT_LEN` | 32 | Max chars per EVERY statement |
| `TIKU_BASIC_ONCHG_MAX` | 4 | Max active ON CHANGE registrations |

### Persistence
| Macro | Default | Purpose |
|---|---|---|
| `TIKU_BASIC_SAVE_BUF_BYTES` | derived | Default-slot program buffer |
| `TIKU_BASIC_NAMED_SLOTS` | 3 | Number of named slots |
| `TIKU_BASIC_NAMED_SLOT_BYTES` | 192 | Bytes per named slot |

### Numeric
| Macro | Default | Purpose |
|---|---|---|
| `TIKU_BASIC_FIXED_ENABLE` | 1 | Compile fixed-point support |
| `TIKU_BASIC_FIXED_SCALE` | 1000 | Q-format scale (1000 = Q.3) |
| `TIKU_BASIC_PI_Q3` | 3142 | π in Q.3 |

### Bridges
| Macro | Default | Purpose |
|---|---|---|
| `TIKU_BASIC_PEEK_POKE_ENABLE` | 1 | PEEK / POKE statements |
| `TIKU_BASIC_GPIO_ENABLE` | 1 | PIN / DIGWRITE / DIGREAD |
| `TIKU_BASIC_ADC_ENABLE` | 1 | ADC() function |
| `TIKU_BASIC_I2C_ENABLE` | 1 | I2CWRITE / I2CREAD |
| `TIKU_BASIC_LED_ENABLE` | 1 | LED statement |
| `TIKU_BASIC_VFS_ENABLE` | 1 | VFSREAD / VFSWRITE |
| `TIKU_BASIC_REBOOT_ENABLE` | 1 (target) / 0 (host) | REBOOT |
| `TIKU_BASIC_STRVARS_ENABLE` | 1 | String variables and functions |
| `TIKU_BASIC_DEFN_ENABLE` | 1 | DEF FN |
| `TIKU_BASIC_ARRAYS_ENABLE` | 1 | DIM / array access |

Override examples:

```bash
# Bigger line / program limits for comfortable demos
EXTRA_CFLAGS="-DTIKU_BASIC_LINE_MAX=80 -DTIKU_BASIC_PROGRAM_LINES=64"

# Tiny BASIC: drop bridges, smaller heap
EXTRA_CFLAGS="-DTIKU_BASIC_GPIO_ENABLE=0 -DTIKU_BASIC_I2C_ENABLE=0 \
              -DTIKU_BASIC_VFS_ENABLE=0 -DTIKU_BASIC_STR_HEAP_BYTES=128"

# Q.4 fixed-point (×10000) for finer precision (range halves)
EXTRA_CFLAGS="-DTIKU_BASIC_FIXED_SCALE=10000"
```

---

## Error reference

Every error message starts with `?` and is shown in red (when colors
are enabled). The interpreter aborts the current statement and, in
RUN mode, prints `at line N` on the next line in dim red.

| Message | Meaning |
|---|---|
| `? syntax` | Statement didn't match any keyword and isn't a valid implicit-LET |
| `? expected number or variable` | Expression expected |
| `? variable expected` | LET / READ / INPUT / FOR / NEXT / DIM saw something else |
| `? '(' expected` / `? ')' expected` / `? ',' expected` / `? '=' expected` | Punctuation |
| `? '(' or ')' expected` | String-function call has malformed parens |
| `? bad variable` | Identifier looks like a multi-letter name (not allowed for vars) |
| `? bad relop` | Mismatched relational operator chars |
| `? bad string relop` | String comparison operator that's not =/<>/</>/<=/>= |
| `? string relop expected` | String op missing in IF condition |
| `? string expected` | Expected a string atom but got something else |
| `? string too long` | String-expression result exceeds `TIKU_BASIC_STR_BUF_CAP` |
| `? unterminated path` / `? quoted path expected` | VFS-path parser saw bad input |
| `? path too long` | Path > 48 chars |
| `? trailing junk` | After a statement, extra characters that aren't `:` or end-of-line |
| `? GOTO / GOSUB / FOR / NEXT / READ / ON outside RUN` | Statement only meaningful inside RUN |
| `? line jump outside RUN` | IF-bare-line shorthand outside RUN |
| `? GOSUB stack overflow` / `? FOR stack overflow` | Frame depth limit hit |
| `? RETURN without GOSUB` | RETURN with no matching GOSUB |
| `? NEXT without FOR` / `? NEXT mismatch` | NEXT couldn't find its frame |
| `? STEP cannot be 0` | FOR with explicit step of 0 |
| `? unknown label X` | GOTO / GOSUB to a name that doesn't exist |
| `? out of DATA` | READ past the last DATA item |
| `? bad line number` | Line number 0 or > 65533 |
| `? program full (N lines)` | Program slot table is full |
| `? bad array size N` | DIM size out of [1, TIKU_BASIC_ARRAY_MAX] |
| `? array X already DIMmed` | Re-DIM of the same letter |
| `? array X not DIMmed` | Read/write of an undimmed array |
| `? array index N out of range` | Element index out of [0, size) |
| `? out of memory for array` | Arena exhausted during DIM |
| `? out of string heap` | String heap exhausted |
| `? FN expected` | DEF without FN |
| `? function name >= 2 chars` | Single-letter DEF FN name (collides with vars) |
| `? argument variable expected` | DEF FN argument isn't a single letter |
| `? DEF FN table full` | Too many DEF FN definitions |
| `? DEF body too long` | DEF FN body exceeds TIKU_BASIC_DEFN_BODY |
| `? bad GPIO Px.y` | GPIO port/pin out of range or not configured |
| `? bad LED N (count=M)` | LED index ≥ board's LED count |
| `? ADC channel out of range` | ADC channel > 31 |
| `? ADC init/read failed` | HAL returned error |
| `? I2C init failed` / `? I2C read/write failed (...)` | HAL error |
| `? VFS read/write failed: path` | VFS resolve failure or callback error |
| `? MOD by zero` / `? FDIV by zero` / `? division by zero` | Self-explanatory |
| `? format string expected` | PRINT USING saw no `"..."` |
| `? value render failed` | Internal snprintf failure (shouldn't happen) |
| `? slot too small for program` | Named-slot SAVE: program > 192 B |
| `? all slots in use` | Named-slot SAVE: no empty slot and name not already present |
| `? 'name' not found` | LOAD of a non-existent named slot |
| `? load: no saved program` | Default-slot LOAD with nothing saved |
| `? save/load: persist init failed` | Kernel persist API error |
| `? save: program too large for buffer` | Program exceeds default-slot buffer |
| `? basic: out of memory (need N B in AUTO tier)` | First-call arena allocation failed |
| `? iteration cap reached` | RUN loop hit its 100,000-iteration safety cap |

---

## Limits and gotchas

### `FOR` body must be on separate lines

```basic
' WORKS:
10 FOR I = 1 TO 5
20 PRINT I
30 NEXT I

' DOES NOT WORK (body skipped):
10 FOR I = 1 TO 5 : PRINT I : NEXT I
```

The loop-back line pointer is set to the line after the FOR line,
so a same-line body is unreachable.

### String comparisons only at the IF top level

```basic
' WORKS:
IF A$ = "hi" THEN PRINT "match"

' DOES NOT WORK:
LET X = (A$ = "hi")
IF (A$ = "hi") AND B > 5 THEN ...
```

Mixing strings and numerics in arbitrary subexpressions would need
the full type system; this restriction keeps the interpreter small.

### String heap resets per `RUN`

Within one RUN, every `LET A$ = ...` allocates new heap memory; old
strings are not reclaimed. Once the heap is full, you'll hit
`? out of string heap`. Plan for ~50–100 string assignments per
program at default sizes; reset by re-running. The heap and var
table are zeroed at every RUN start.

### `RUN` clears variables

Numeric vars, array contents, and string vars all reset to zero /
NULL at every `RUN` start. If you want values to survive, save them
to FRAM via `VFSWRITE` or use the named-slot mechanism for state.

### Loop-body lines must be separate from `FOR`/`NEXT`/`WHILE`/`WEND`

The same-line constraint applies to all three loop forms. `FOR I =
1 TO 5 : PRINT I : NEXT I` skips the body. Always put the body on
separate lines from the loop boundary keywords.

### `SWAP` is scalars-only

You can swap `A` ↔ `B` or `A$` ↔ `B$` but not `A(i)` ↔ `A(j)`. For
array swaps use a temporary variable.

### Same-line `IF` body absorbs everything

```basic
IF cond THEN A=1 : B=2 : C=3
```

The `:` belongs to the IF body. Once cond is checked, A=1, B=2, and
C=3 all run if cond is true; all are skipped if false. There is no
way to have unconditional statements after an IF on the same line —
use a separate line.

### Line numbers are sparse

You can use any line numbers (e.g., 100, 200, 1000) — the storage
is unsorted. Just stay below 65533. Re-typing a line number replaces
that line; typing just the number deletes it.

### `MILLIS()` wraps fast

The system tick is 16-bit, so at 128 Hz it wraps every ~512 s. The
ms math wraps proportionally. Use `SECS()` for long-running
intervals.

### `DELAY` / `SLEEP` precision

These busy-wait at the system tick rate (default 128 Hz, ~7.81 ms
per tick). `DELAY 8` is one tick; `DELAY 1000` is ~128 ticks. For
microsecond-class waits, use the kernel's `tiku_htimer` from C
instead.

### `REBOOT` is final

It configures the watchdog and spins. Save anything you care about
before calling.

---

## Memory budget

At default sizing on FR5994 with `MEMORY_MODEL=large` and colors
enabled (the full feature set):

| Category | Bytes |
|---|---|
| **Code (.text + .upper.text)** | ~38 KB |
| .upper.rodata (string tables, format strings, sin LUT) | ~4 KB |
| **Data** | |
| Default-slot save buffer (FRAM) | ~1.3 KB |
| Persist store metadata (FRAM) | ~50 B |
| 3 named slots × 204 B (FRAM) | ~612 B |
| Arena control block + pointers (BSS) | ~80 B |
| **Runtime arena** (allocated lazily on first `basic`) | |
| Program lines table (24 × ~50 B) | ~1.2 KB |
| Numeric var table (26 × 4 B) | 104 B |
| String var table (26 × 4 B) | 104 B |
| String heap | 512 B |
| GOSUB stack (8 × 2 B) | 16 B |
| FOR stack (4 × 12 B) | 48 B |
| WHILE/REPEAT loop stack (4 × 2 B) | 8 B |
| DEF FN slots (4 × ~52 B) | ~208 B |
| Array metadata (26 × 12 B + 26 × 12 B for strings) | ~624 B |
| Array data reserve | 512 B |
| EVERY slots (4 × 48 B) | ~192 B |
| ON CHANGE slots (4 × 52 B) | ~208 B |
| Alignment headroom | 128 B |
| **Total arena** | ~3.9 KB |

The arena comes out of the kernel's AUTO memory tier — on FR5994 in
large mode that means HIFRAM, which has plenty of headroom.

**Total cost on FR5994** vs a no-BASIC build: roughly **+38 KB
text**, **+3.5 KB data**, **+36 KB bss** (most of the bss is the
shared 32 KB HIFRAM tier pool, which BASIC is the first user of —
not "BASIC's overhead" in any meaningful sense).

---

## Worked examples

### Number guessing
```basic
NEW
10 LET T = RND(100) + 1
20 LET G = 0
30 PRINT "Guess 1-100"
40 INPUT N
50 G = G + 1
60 IF N = T THEN GOTO 100
70 IF N < T THEN PRINT "higher"
80 IF N > T THEN PRINT "lower"
90 GOTO 40
100 PRINT "Got it in"; G; "guesses!"
```

### Sieve of Eratosthenes
```basic
NEW
10 DIM P(100)
20 FOR I = 2 TO 99
30 P(I) = 1
40 NEXT I
50 FOR I = 2 TO 9
60 IF P(I) = 0 THEN GOTO skip
70 FOR J = I + I TO 99 STEP I
80 P(J) = 0
90 NEXT J
100 skip: NEXT I
110 PRINT "Primes <= 100:"
120 FOR I = 2 TO 99
130 IF P(I) = 1 THEN PRINT I;
140 NEXT I
150 PRINT
```

### Caesar cipher
```basic
NEW
10 INPUT "Text"; T$
20 INPUT "Shift"; S
30 LET R$ = ""
40 FOR I = 1 TO LEN(T$)
50 LET C = ASC(MID$(T$, I, 1))
60 IF C >= 65 THEN IF C <= 90 THEN C = ((C - 65 + S) MOD 26) + 65
70 IF C >= 97 THEN IF C <= 122 THEN C = ((C - 97 + S) MOD 26) + 97
80 LET R$ = R$ + CHR$(C)
90 NEXT I
100 PRINT R$
```

### Fibonacci with DEF FN
```basic
NEW
10 DEF FN dbl(x) = x + x
20 LET A = 0
30 LET B = 1
40 FOR I = 1 TO 10
50 PRINT B,
60 LET C = A + B
70 LET A = B
80 LET B = C
90 NEXT I
100 PRINT
```

### Fixed-point: circle math
```basic
NEW
10 INPUT "diameter (cm)"; D
20 LET R = D / 2
30 LET C = FMUL(D, PI)
40 LET A = FMUL(FMUL(R, R), PI)
50 PRINT "circ = " + FSTR$(C) + " cm"
60 PRINT "area = " + FSTR$(A) + " cm^2"
```

### Sensor-reactive blink
```basic
NEW
10 INPUT "Threshold"; THR
20 LET T = ADC(10)
30 IF T > THR THEN LED 0, 1 : LED 1, 0
40 IF T <= THR THEN LED 0, 0 : LED 1, 1
50 IF MILLIS() MOD 1000 < 100 THEN PRINT FSTR$(T)
60 DELAY 200
70 GOTO 20
```

### Persistent counter
```basic
NEW
10 LET C = VFSREAD("/sys/boot/count")
20 PRINT "Boot #" + STR$(C)
30 ' next boot, use init add to autorun this:
40 ' init add 50 ctr basic run
```

### Bubble-sort with SWAP and 1D array
```basic
NEW
10 DIM A(10)
20 FOR I = 0 TO 9
30   A(I) = RND(100)
40 NEXT I
50 LET N = 10
60 REPEAT
70   LET S = 0
80   FOR I = 0 TO N - 2
90     IF A(I) > A(I + 1) THEN
100      LET T = A(I)
110      A(I) = A(I + 1)
120      A(I + 1) = T
130      LET S = 1
140    END IF
150  NEXT I
160  LET N = N - 1
170 UNTIL S = 0
180 FOR I = 0 TO 9 : PRINT A(I); : NEXT I
190 PRINT
```

### WHILE-driven sine table
```basic
NEW
10 LET A = 0
20 WHILE A < 6283
30   PRINT FSTR$(A); " "; FSTR$(SIN(A))
40   A = A + 200
50 WEND
```

### ON ERROR demo
```basic
NEW
10 ON ERROR GOTO 100
20 FOR I = 5 TO -1 STEP -1
30   PRINT FSTR$(FDIV(10.0, I))
40 NEXT I
50 END
100 PRINT "skip: divide by zero at I = "; I
110 RESUME NEXT
```

### EVERY-driven blink with ON CHANGE button watch
```basic
NEW
10 PIN 4, 6, 1                       ' LED pin output
20 ON CHANGE "/dev/gpio/1/0" GOSUB 200
30 EVERY 500 : DIGWRITE 4, 6, 2      ' toggle every 500 ms
40 PRINT "running; press button..."
50 GOTO 50
200 PRINT "button changed at " + STR$(SECS()) + "s"
210 RETURN
```

---

## Implementation notes

### Architecture

```
tiku_shell_cmd_basic.c    one ~2500-line C file containing:
 ├─ State (arena, vars, prog table, str heap, ...)
 ├─ Lexer helpers (skip_ws, parse_unum, match_kw)
 ├─ Recursive-descent expression parser
 ├─ String-expression sub-grammar
 ├─ Statement dispatch (exec_stmt)
 ├─ Multi-statement walker (exec_stmts)
 ├─ Per-statement implementations (exec_let, exec_print, ...)
 ├─ RUN loop (exec_run)
 ├─ Persistence helpers (default + named slots)
 └─ Hardware bridges (GPIO, ADC, I2C, LED, VFS)
```

### Memory tier

The arena is allocated on the first `basic` invocation from the
kernel's `TIKU_MEM_AUTO` tier. On FR5994 with large mode, AUTO
routes to HIFRAM (above the 1 KB threshold). Re-invocations reset
the arena and re-allocate, ensuring fresh state.

### Expression parser

A flat recursive-descent stack with one function per precedence
level: `parse_expr` → `expr_or` → `expr_and` → `expr_rel` → `expr_sum`
→ `expr_term` → `expr_unary` → `expr_prim`. The string sub-grammar
(`parse_strexpr` → `parse_strprim`) is parallel and reachable from
PRINT, LET, INPUT, IF, and the `LEN/ASC/VAL/FSTR$` numeric functions.

### String heap

Bump allocator inside the BASIC arena. No GC; resets at every
`RUN` start. Each `LET A$ = ...` allocates a new copy and updates
the var pointer; the old string becomes unreachable but not
reclaimed.

### Persistence

Two paths:

1. **Default slot** uses `tiku_persist_*` (the kernel's magic-
   number-validated key/value store). Survives across firmware
   re-flashes if the FRAM region isn't erased.

2. **Named slots** are direct `.persistent` arrays with MPU-bracketed
   writes. Simpler, no validation magic — relies on the FRAM
   retaining its contents.

### Performance

Each statement re-parses from text on every execution. There is
**no bytecode** and **no pre-tokenization** — the simplicity is
deliberate to keep the interpreter readable and bounded.

Rough numbers on FR5994 at 8 MHz:
- ~5,000 simple statements/second
- A `FOR I = 1 TO 1000` loop with one `PRINT I` per iteration runs
  in ~0.5 s.

If you need faster execution, the architecture is amenable to a
pre-tokenization pass (planned, not implemented) that would store
token sequences alongside line text and roughly halve the per-
statement cost.

### Test harness

The host harness (`tests/host/test_basic_smoke.c`) compiles the
BASIC source directly via `#include`, with platform-specific
kernel headers blocked by macros and replaced by stubs (GPIO map,
ADC counter, I2C register simulation, persist store, MPU no-ops,
VFS key/value map, LED state). 169 tests cover every statement,
function, and error path. Run via:

```bash
cd tests/host && make test_basic_smoke && ./test_basic_smoke
```

---

## Keyword index

```
Statements    DATA       FOR       LET       POKE      SLEEP
              DEF        GOSUB     LIST      PRINT     STEP
              DELAY      GOTO      LOAD      READ      STOP
              DIGWRITE   I2CWRITE  NEXT      REBOOT    SWAP
              DIM        IF        NEW       REM       THEN
              ELSE       INPUT     ON        RESTORE   TO
              END        LED       PIN       RESUME    TRACE
              END IF     ENDIF     ELSE      RETURN    UNTIL
              EVERY      WEND      WHILE     REPEAT    USING
              SAVE       VFSWRITE
                         (ON ERROR / ON CHANGE / ON GOTO / ON GOSUB
                          all dispatch via the ON keyword)

Functions     ABS    ASC    BIN$    CHR$    COS     DIGREAD
              FDIV   FMUL   FSTR$   HEX$    I2CREAD INT
              LEFT$  LEN    MAX     MID$    MILLIS  MIN
              MOD    PEEK   RIGHT$  RND     SECS    SGN
              SHL    SHR    SIN     SQR     STR$    TAN
              VAL    VFSREAD

Direct        AUTO   BYE    DIR     EXIT    HELP    LIST
              LOAD   NEW    QUIT    RENUM   RUN     SAVE

Operators     +  -  *  /        =  <  >  <=  >=  <>
              AND  OR  XOR  NOT
              :   (multi-stmt)

Constants     TRUE  FALSE  PI

Variables     A..Z              numeric scalars
              A$..Z$            string scalars
              DIM A(n)          1D integer array
              DIM A(m, n)       2D integer array
              DIM A$(n)         1D string array
              DIM A$(m, n)      2D string array

Comments      REM rest-of-line
              ' rest-of-line

Numeric       42  -3  0xFF  0b1010  &HFF  &B1010  1.5  0.001
literals
```

---

## See Also

- [Shell guide](shell.md) — the parent shell that hosts BASIC
- [Core API reference](api-reference.md) — kernel APIs the bridges call
- `kernel/shell/commands/tiku_shell_cmd_basic.c` — the implementation
- `tests/host/test_basic_smoke.c` — the test harness, 225 cases
