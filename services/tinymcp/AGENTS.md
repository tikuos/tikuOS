# services/tinymcp/

The board as a Model Context Protocol server.  The protocol is the
tinymcp library (`http://tinymcp.org`, a sibling checkout `tinymcp/`
beside `tikukits/`, gitignored); this directory binds it to the namespace
and the shell and carries it on the console line.  Built with
`TIKU_MCP_ENABLE=1`, which needs the shell: requests run from its pump.

```
tiku_mcp.h/.c          the host table: resources, tools, watches, the process
tiku_mcp_console.c     channel 0xF3: the queue, the pump, the frame out
tiku_mcp_ask.h/.c      /sys/mcp/ask, the journal, BASIC's ASK
tiku_mcp_consent.h/.c  which actions ask the owner, and the open question
tiku_mcp_job.h/.c      the basic tool: one BASIC job, its output, /sys/mcp/job
tiku_mcp_tcp.h/.c      mcp listen: one TCP connection, /sys/mcp/key and tcp
tiku_mcp_vfs.h/.c      /sys/mcp, a boot mount
```

The `mcp` shell command (`shell/commands/tiku_shell_cmd_mcp.c`) prints
what `/sys/mcp` holds; `ask` (`tiku_shell_cmd_ask.c`) asks a question.  The host side is `tinymcp/bridge/tinymcp.py`,
which turns an MCP client's stdio into frames on the port; the text
around the frames, the banner and a person's shell session, stays on
its stderr.

## What the library sees

- Identity: `serverInfo.name` is `/sys/device/name`, read again before
  each request, so a renamed board is renamed at once; `title` is
  "tikuOS on <device>"; `version` is `TIKU_VERSION`.
- Resources: `tiku://shell/commands` (the command table, one `name  help`
  line each), every readable file of `/sys/vfs/manifest` as
  `tiku:///<path>` with the manifest's meta column as its description,
  then the `/data` files.  A cursor below `0x40000000` is a manifest
  line; from there on it indexes `/data`.  A directory reads as its
  listing.  A node whose read acts (`DF_READ_EFFECT`, `DF_READ_CONSUMES`)
  is refused as a resource and offered through the `read` tool; a secret
  node is not listed and reads as missing.
- Tools: `read` (`path`, `line`, `bytes`: pages through
  `tiku_vfs_read_lines()`; a whole read of a typed node also returns
  `structuredContent` `{value, unit}`, decoded from the text read by
  `tiku_vfs_val_from_text()`, a FIXED value with its scale applied), `write` (`path`, `value`: `tiku_vfs_write()`,
  the status name on failure), `ls` (`path`), `shell` (`line`: the parser
  with a capture backend whose `putc` is `tinymcp_sink_putc()`; a mode
  command, `watch`, `basic`, `ping`, `ntp`, `dns`, `mqtt`, `slip` or
  `telnet`, is refused), `basic` (`program`, `background`, `seconds`;
  below).  The capture backend carries `TIKU_VFS_CAP_ALL`: the console is
  the owner's cable.
- Prompts: each `/data/prompt.<name>` file is the prompt `<name>`.  Its
  first line is the description, its `{{argument}}` places are its
  required arguments (`tinymcp_placeholders()`), and `prompts/get`
  returns the whole file with the arguments filled in
  (`tinymcp_render()`).  A file is read through `tiku_vfs_read_at()` up to
  `TIKU_MCP_SCRATCH` less one byte; one page of `prompts/list` holds up to
  `TIKU_MCP_PROMPTS_MAX` (16) prompts.
- Completion: the argument `path` of either node template, or of any
  prompt, completes against the namespace: the value up to its last slash
  names a directory, the rest is a prefix of a child's name, and a
  directory's completion ends in a slash.  A template's path has no
  leading slash; a prompt's may.
- Asking: `/sys/mcp/ask/` holds one question at a time.  `question` is
  written by `ask <words...>`, by a rule whose action is `ask ...`, by
  BASIC's `ASK q$, A$[, seconds]` or by any writer; each write grows `seq`,
  clears the answer and sets `state` to `asked`, and subscribers of
  `tiku:///sys/mcp/ask/question` hear it.  `answer` takes one answer to
  the question waiting; a leading `#<seq> ` names that question, and an
  answer to an older one, or a second answer, is `EINVAL`.  `asked_by`
  and `answered_by` hold the writer: the client's name
  (`tinymcp_client()`) while one of its tools runs, `board` otherwise
  (`tiku_mcp_actor()`).  BASIC's `ASK` parks the program through
  `tiku_basic_ext_wait()` until the answer comes, a later question
  replaces it, or the seconds pass; the variable is empty in the last two
  cases.  `tinymcp/bridge/attend.py` answers for an owner who leaves the
  board with no agent attached.
- Consent: before an agent reboots the board, changes the sleep mode,
  powers a part down (`power ... off|down`), changes the CPU clock
  (`freq <mhz>`, a write under `/sys/cpu/freq`) or the memory layout
  (`layout stage|recover`, a write to `/sys/mem/layout/stage` or
  `recover`), the `write` and `shell` tools ask the owner; a command that
  `once`, `every` or `on` would run later is judged as itself.  A client
  that declared elicitation gets an `input_required` form whose state
  names the question; its retry with a yes acts, with a no does nothing.
  Any other client gets a six-digit token in the tool error; writing it to
  `/sys/mcp/consent` grants the same call from the same client within
  `TIKU_MCP_CONSENT_TTL_S` (60).  One question is open at a time, and a new
  one replaces it.  The console's own commands never ask: the person at it
  is the owner.
- Jobs: the `basic` tool hands its `program`, numbered lines one per line
  of text, to `tiku_basic_job_start()` with a backend whose `putc` fills a
  `TIKU_MCP_JOB_OUT` (1024) byte ring, ANSI escapes dropped; one job runs
  at a time, and not while a person is in BASIC at the console.  The call
  is kept with `tinymcp_defer()` and answered from the pump when the job
  ends, with the ring and, past its `seconds` (`TIKU_MCP_JOB_BUDGET_S`,
  60), a line saying it was stopped; each whole line printed goes out
  through `tinymcp_progress()`, at most one per `TIKU_MCP_JOB_NOTE_MS`
  (250).
  With `background` true the call answers at once.  `/sys/mcp/job/` holds
  `seq`, `state` (`idle`, `running`, `done`, `error`, `stopped`), `by`,
  `out` (the ring, streamed, notified with the notes) and `stop` (any
  write ends the job; `EINVAL` with none).  A `notifications/cancelled`
  for the kept call stops its job through the host's `cancel`.  The
  backend's capability is `TIKU_VFS_CAP_ALL` without `TIKU_VFS_CAP_SYS`,
  so a job's writes to the clock target, power, the layout and the
  watchdog are `EPERM`; BASIC refuses `INPUT`, `REBOOT` and `POKE` in a
  job.  The journal records each start, stop, cancel and end.
- Journal: `/sys/mcp/journal`, a RAM ring of `TIKU_MCP_JOURNAL_MAX` (16)
  entries, one line each, `<seconds> <who> <what>`: questions, answers,
  the `write` and `shell` tools (not those under `/sys/mcp/`, which write
  their own), and any text a writer adds.  It streams through
  `tiku_vfs_read_at()` and a change notifies its subscribers.
- Watches: `tiku_vfs_watch()` under the `tinymcp` process, up to
  `TIKU_MCP_WATCH_MAX` (8) across streams, each a (stream, slot) the
  library named; `TIKU_EVENT_VFS` becomes `tinymcp_updated()`.  The
  process also watches `/data`, from its first run, after the boot has
  assembled the tree: a write or a delete there is hashed against the
  names and the prompt files it saw last, and a change in the names sends
  `tinymcp_listchanged()`, a change in the prompts
  `tinymcp_prompts_changed()`.  That watch holds one of the kernel's
  `TIKU_VFS_WATCH_MAX` slots for the boot.

## TCP and the key

With the full net kit (`TIKU_KIT_NET_ENABLE=1`, not `TIKU_KIT_NET_MIN`)
`mcp listen <port>` serves a second stream on that TCP port, one
connection at a time, newline-delimited; `mcp listen off` stops it.  On a
board without Wi-Fi the port is reached over the console line once `slip`
is on.  A connection's in and out buffers are an arena leased from the
SRAM tier when it connects; a closed or failed connection is released from
the pump, where no request is being served, its arena zeroed and given
back.  A reply goes out in segments of the connection's MSS; while the TX
pool is full the console is pumped so acknowledgements come in, and after
`TIKU_MCP_TCP_SEND_MS` (3000) the connection is aborted.  The pump also
runs the TCP stack's timers about 20 times a second while it listens,
except in the `TIKU_SHELL_NET_TEST` build, whose shell runs them.  A
request has the owner's authority (`tiku_mcp_owner()`) on the console, or
over TCP when its `_meta` member `org.tinymcp/key` equals the key in
`/sys/mcp/key` (a persist cell of 8 to 32 characters, written with the
system capability, read back as `set` or `none`, compared byte by byte to
the end).  Without it `write`, `shell` and `basic` are refused, `read`
refuses a secret node or one whose read acts, and the request's VFS
writes carry no capability.  `/sys/mcp/tcp` says `off`, `listening <port>`
or `connected <port> <peer>`, with the connections accepted and refused.

## Execution

The decoder hands a frame over inside `tiku_console_getc()`, from the
line editor or a running command, so `on_frame()` copies it into one of
two queue slots and the pump handles it with `tinymcp_handle()` between
command lines, where rules and jobs run.  A third request while two
wait is answered `-32603` "busy" from the callback, unless a reply's
frame is open, when the refusal is dropped.  A reply that fits the out
buffer is one frame through `tiku_console_frame_begin/put/end()` and then
`tiku_console_flush()`, which the USB Serial/JTAG wire needs to send a
frame that ends without a newline.  A longer one, a node's text or a
tool's output, streams through `send_part`: the frame opens at the first
piece and ends at the last, each piece kicking the watchdog, so a read
returns the whole node.  Nothing else may write to the wire while that
frame is open; text written there lands inside it.

## Memory

`TIKU_MCP_IN_CAP` and `TIKU_MCP_OUT_CAP` (1024 and 4608; 2048 and 8192
on the ESP32-C61 and P4; 256 and 512 on MSP430) size the channel buffer,
the two queue slots and the reply; `TIKU_MCP_SCRATCH` (4098) holds one
page of a node.  Measured on the nRF54L15 at `-Os`, from the object
files on 2026-10-28, with the net kit: the binding 16.9 KB of code (the
job module 2.1 KB, the TCP stream 1.4 KB), the library 17.3 KB, 20.5 KB of
SRAM in all (the ask module 2.2 KB of it, the journal most; the job's ring
and state 1.3 KB), and while a TCP connection is open its 5.6 KB arena.

## Proof

`tinymcp/tests/board_smoke.py <port>` drives a board through the bridge
with the official `mcp` SDK client in both eras: discover and the board's
identity, list, read, write the LED, subscribe and see the change, a shell
line, a refused mode command, a prompt written to `/data` got and heard
going, path completion, the board asking and an answer refused when stale,
a reboot refused and a clock target allowed by the owner, BASIC jobs
(waited for with progress, read meanwhile, in the background, stopped,
cancelled, past their seconds, confined), the manifest past the reply
buffer in one read, `/sys/cpu/freq` as a value in Hz, and the error
codes.  `tinymcp/tests/board_tcp.py <port>` turns `slip` on, sets a key,
runs `mcp listen 7000`, and drives the same SDK through the bridge's
`--tcp` to `tests/slip_tcp.py`, a TCP client over the board's console line:
one connection at a time, reads and a 16 KB read, the refusals without the
key, a subscription's update, writes and consent with it, a wrong key, the
handshake era, and the arena given back.  `tinymcp/tests/board_ask.py <port>` runs a BASIC
program whose `ASK` it answers over the same port, lets one run out of
time, has a rule ask, and runs `attend.py` against a stub of the Messages
API.  TikuBench's `tinymcp` suite runs both, with BASIC built in.
