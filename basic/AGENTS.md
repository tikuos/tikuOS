# basic/

Tiku BASIC is the interpreter behind the shell's `basic` command: numeric
and string variables, arrays, `DEF FN`, multi-line `IF`, `SELECT CASE`,
subroutines with parameters and locals, `ON ERROR`, reactive `EVERY` and
`ON ... CHANGE`, `SAVE`/`LOAD` to `/data`, and builtins for HTTP(S), MQTT,
BLE, JSON, SHA-256/HMAC/Base64 and the clock.  `make
TIKU_SHELL_BASIC_ENABLE=1` builds it (on MSP430 with `MEMORY_MODEL=large`,
enforced by the Makefile; Ambiq parts default it on).

## Layout

`tiku_basic.c` includes the `.inl` files that make up the interpreter;
each `.inl` is one concern and its header comment names it:

```
tiku_basic_state.inl        core types and module-level state
tiku_basic_cursor.inl       the parse cursor: peek, consume, match, probe
tiku_basic_lex.inl          lexical helpers
tiku_basic_token.inl        keyword crunching
tiku_basic_expr.inl         numeric expression parser
tiku_basic_string.inl       string heap and string expressions
tiku_basic_stmt.inl         one exec_<keyword> per statement
tiku_basic_dispatch.inl     exec_if and the keyword switch
tiku_basic_program.inl      the line table
tiku_basic_renum.inl        RENUM with line-reference rewriting
tiku_basic_run.inl          the RUN loop as a resumable step machine
tiku_basic_mode.inl         BASIC as a non-blocking shell-loop mode
tiku_basic_debug.inl        the line debugger of that mode
tiku_basic_ckpt.inl         power-failure-transparent RUN, RUN RESUME
tiku_basic_persist.inl      default-slot SAVE and LOAD
tiku_basic_named_slots.inl  named slots and DIR
tiku_basic_arena.inl        arena allocation for the working set
tiku_basic_reclaim*.inl     BASIC as a memory-reclaim owner
tiku_basic_net.inl          networking statements
tiku_basic_https*.inl       the HTTPS client and its trust store from /data
tiku_basic_ble.inl          BLE words
tiku_basic_hw.inl           ADC and I2C bridges
tiku_basic_peek_poke.inl    PEEK and POKE
tiku_basic_ext*.inl         the native builtin registry and bundled extensions
tiku_basic_vfs.c            the /data/basic node boot mounts
tiku_basic_vfs_file.inl     its bridge
tiku_basic_module.h/.c      the runtime-loadable native module ABI and loader
modules/                    mod_demo.c and its per-part linker scripts
```

`tiku_basic_config.h` holds the profile knobs (arena size, line count,
string heap).  The arena sits in the tier the part offers: PSRAM on the
ESP32-C61, SRAM on RP2350 (the refuse-NVM-arena guard keeps it off flash).

## Rules

- Parser code uses the cursor vocabulary in `tiku_basic_cursor.inl`; no
  raw `**p` walking in new code.  Its header lists the invariants.
- Every `.inl` is compiled through `tiku_basic.c` only; a sweep over
  `*.c`/`*.h` misses them, so search `*.inl` too.
- BASIC owns `/data/basic` as a static child of `/data`; boot mounts it
  through `tiku_basic_vfs_get()` (`kernel/vfs/AGENTS.md`).
- A builtin that blocks must kick the hang watchdog's pump; long crypto
  runs on the worker thread where the part has one.

## Native modules

A module is compiled separately at a fixed address, cannot link against
firmware symbols, and reaches every service through the jump table passed
to its entry point (`tiku_basic_module.h`, included by both builds).  The
physical window (address, size, whether it executes from RAM) is
`hal/tiku_module_layout.h`; the BASIC header keeps the ABI and loader
settings.  `make TIKU_BASIC_MODULE_ENABLE=1` builds `modules/mod_demo.c`
against `modules/mod_demo_<mcu>.ld`; the install takes the image from a
`/data` file, writing the embedded copy there first when it is absent.
Parts without a slot refuse the loader at compile time.
`TikuBench/tests/host/kernel/test_kernel_layout.py` compiles the window for
every part.

## Proof

Board: the `basic` suite (forces `TIKU_SHELL_BASIC_ENABLE=1`).  Host:
`TikuBench/tests/host/test_basic_smoke.c` and the `applications`
`tiku-basic` host build (`apps/basichost`), which compiles these sources
on the desktop.
