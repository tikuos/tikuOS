# AGENTS.md

Rules for anyone, person or agent, changing this tree.  `README.md` says
what TikuOS is and how it is built; this file says how work lands.
A directory's `AGENTS.md` (listed at the end) says how that subsystem
works; read it before changing files there.

## Source ownership

- `kernel/`: CPU policy, memory, processes, scheduler, timers, VFS, file
  store, threads, driver registry.  Core files must not include or call
  `shell/`, `basic/`, `services/` or the applications overlay; `make lint`
  (`tools/check_kernel_dependencies.py`) refuses an include or a symbol
  reference that does.
- `services/`: console framing, links, USB device classes, the init table.
- `shell/` and `basic/`: the shell with its commands; the interpreter with
  its native modules.
- `boot/` and `main.c` choose the components and assemble the VFS
  (`tiku_boot_vfs_init()`); providers hand their nodes to the core tree.
- `hal/`, `interfaces/`, `arch/<family>/`: hardware contracts and ports.
  Eight arch files still include shell I/O headers (listed in
  `shell/AGENTS.md`); do not add a ninth.
- Feature work the mainline must not carry lives in an overlay repository
  cloned into the tree (`experiment/`, gitignored) and hooks in through
  `-include experiment/experiment.mk` and `TIKU_EXP_*` macros only.  With
  the overlay absent the build must be byte-identical to a tree that never
  had it.

## Build, test, lint

```bash
make MCU=<mcu> [TIKU_SHELL_ENABLE=1 TIKU_SHELL_BASIC_ENABLE=1 ...]
make flash MCU=<mcu> ...
make lint                      # comment style, durable placement, dependencies
make -C TikuBench/tests/host run # host suites: kernel, sanitizers, port-check
cd TikuBench && python -m tikubench run <suite> --board <board> --flash
cd TikuBench && python -m tikubench all --board <board> --flash
```

- `main.elf` is one shared file across MCUs: `rm -rf build/<mcu> main.elf`
  before comparing sizes or switching targets.  The flag-change guard wipes
  a build directory when make variables change; flags set in the
  environment are not seen.
- A new file is untracked until staged, and the linters only open tracked
  files: lint after staging.
- With more than one J-Link attached, pass `TIKUBENCH_JLINK_SN=<serial>`
  and let the runner pick the console port; a SEGGER tool run without
  `-USB <serial>` opens a chooser dialog and blocks.  Flash or open a board
  port only when asked to, and never while another process (another agent,
  the desktop test suite) holds the ports.
- A passing new test proves nothing until it has been seen to fail on the
  input it guards.

## Comments

`comment-style.md` is the contract.  Comments are written for a reader who
has only the code: what it does, returns and requires; no comparison with
a design the code does not use, no narration of the debugging path, no
"for now", no metaphor.  Check every "every", "all", "only" and "never"
against the code.  `make lint` enforces the mechanical rules only.

## Durable data

Never write a raw `__attribute__((section(".persistent")))` outside
`kernel/memory/`.  Use `TIKU_DURABLE` (survives a power cycle, budgeted),
`TIKU_RETAINED` (survives a warm reset), `TIKU_FRAM_SPILL` (MSP430
capacity only); small durable scalars use `TIKU_PERSIST_CELL` and the cell
API.  Writes to `TIKU_DURABLE` data sit inside
`tiku_mpu_unlock_nvm()`/`lock_nvm()`; an unbracketed write is dropped on
MSP430 and faults on the Cortex-M ports.  `kernel/memory/AGENTS.md` has the
per-platform semantics and the NVM region layout.

## Commits

- One commit per capability, not per session.  Subject `Area: what changed`
  (`Shell`, `VFS`, `BASIC`, `Nordic Port`, `TikuBench`, `Docs` ...), at most
  72 characters; body at most three `- ` bullets in plain English: what
  changed, why, how it was checked.  No milestone markers, no metaphor, no
  tool or assistant trailers.  A local `commit-msg` hook may refuse a
  message or an author that does not fit; do not use `--no-verify`.
- Commit as the repository's configured identity with plain `git commit`;
  no `-c user.email=` override.
- Before `git commit`, present the subject, author and body and wait for
  approval.  Push only when asked.  Check `git branch -r --contains`
  before amending.
- Each nested checkout (`TikuBench/`, `tikukits/`, `applications/`,
  `drivers/`, `experiment/`) is its own repository: commit inside it.
  Changes that span repositories are pushed together.
- Never committed: the gitignored local directories (`kintsugi/`,
  `experiment/`, `temp/`, `setup-guides/`), board captures and ledgers.

## Where to read next

| Working on | Read |
|------------|------|
| the VFS, nodes, watch, rules, persist cells | `kernel/vfs/AGENTS.md` |
| memory tiers, NVM region, `/data`, layout knobs | `kernel/memory/AGENTS.md` |
| the console line, frames and channels | `services/console/AGENTS.md` |
| message links over the console, IP or BLE | `services/link/AGENTS.md` |
| the shell, a new command, an I/O backend | `shell/AGENTS.md` |
| BASIC, modules, the module layout | `basic/AGENTS.md` |
| drivers and the driver registry | `drivers.md`, `drivers/README.md` |
| the ESP32-C61 radios | `drivers/wifi/esp/README.md` |
| tests, suites, the board runner | `TikuBench/docs/RUNNER.md` |
| the desktop and board GUI | `applications/README.md` |
