# shell/

The interactive shell is a kernel service, not an app: it coexists with
tests, examples and apps.  `make TIKU_SHELL_ENABLE=1` builds it (the legacy
`make APP=cli` does the same).  All I/O goes through a pluggable backend
(`tiku_shell_io_t`); the default is the UART, Telnet hands the same shell
over TCP, and the desktop's window session carries it over a link.

## Layout

```
tiku_shell_io.h/.c      I/O backend struct, SHELL_PRINTF, the UART backend
tiku_shell_parser.h/.c  tokenizer and command dispatcher
tiku_shell.h/.c         shell process: line editing, command table, help
tiku_shell_config.h     command enable/disable flags and hardware gates
tiku_shell_vfs.h/.c     the /sys/rules and /sys/jobs nodes boot mounts
commands/               one .h/.c pair per command
```

## Adding a command

1. Add a `TIKU_SHELL_CMD_XXX` flag to `tiku_shell_config.h`.
2. Create `commands/tiku_shell_cmd_xxx.h` and `.c`.
3. Include the header and add a table entry in `tiku_shell.c`.
4. Add the `.c` to the `ifeq ($(TIKU_SHELL_ENABLE),1)` block of the root
   `Makefile`.
5. A command that drives hardware not every target has gets a rule in the
   HARDWARE REQUIREMENTS section at the bottom of `tiku_shell_config.h`
   (forces the flag to 0 when the capability macro is absent), the whole
   `.c` body gated on the resolved flag (no runtime "not available" stubs),
   and the same rule in the Makefile with a `$(warning ...)` when the
   command is requested on a target without the capability.  Capability
   macros are `-D` globals from the Makefile, never board or device header
   macros: those depend on include order.

## Adding an I/O backend

1. Fill a `tiku_shell_io_t` with putc, rx_ready and getc and its flags.
2. Call `tiku_shell_io_set_backend()` before or during shell init.
3. `TIKU_SHELL_IO_ECHO` echoes typed characters; `TIKU_SHELL_IO_CRLF`
   turns `\n` into `\r\n`.

Nothing pumps a transport from inside `putc`: the IP stack is on that call
chain.  Output for a link goes through a RAM `tiku_shell_io_t` in front of
the cable's and is flushed by a shell pump (`applications/board/gui/
tiku_draw_shell.c` is the model).

## Rules, jobs, watch

The namespace is an event bus (`kernel/vfs/AGENTS.md`).  Rules on writable
nodes are event-armed and evaluated in `tiku_shell_rules_on_vfs()`; rules
on sensor or read-only nodes stay on the poll tick.  Any rule mutation
re-arms wholesale through `rules_rearm()`, which calls `unwatch_all()` for
the shell process, so every subsystem that subscribes as the shell process
must tolerate that drop: the `watch` command re-subscribes idempotently on
every tick for that reason.  `watch` is a non-blocking shell-loop mode
(event mode for writable nodes, tick-counted intervals for sensors, Ctrl+C
cancels; hooks `tiku_shell_cmd_watch_{active,tick,on_vfs,cancel}()`).

## Large replies

A reply over a link is bounded by the link's outbox (200-byte messages),
so a large node is read in pages: `read <path> <line> [bytes]` prints
whole lines from the given line within the byte budget, a wider line prints
whole, and a reader stops at an empty page.

## Shell I/O in arch files

Eight arch files include the shell I/O headers for diagnostics and are the
only ones allowed to: Ambiq `tiku_emmc_arch.c`, `tiku_usb_arch.h`,
`tiku_usb_arch.c`, `tiku_psram_arch.c`, `tiku_nor_arch.c`; RA8P1
`tiku_sdram_arch.c`; RP2350 `tiku_usb_cdc_arch.h`; Nordic
`tiku_usb_cdc_arch.c`.  A HAL print interface is the way to remove them.

## BASIC

`basic/` is the interpreter behind the `basic` command; `basic/AGENTS.md`.
