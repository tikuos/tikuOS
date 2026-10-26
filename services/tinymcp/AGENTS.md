# services/tinymcp/

The board as a Model Context Protocol server.  The protocol is the
tinymcp library (`http://tinymcp.org`, a sibling checkout `tinymcp/`
beside `tikukits/`, gitignored); this directory binds it to the namespace
and the shell and carries it on the console line.  Built with
`TIKU_MCP_ENABLE=1`, which needs the shell: requests run from its pump.

```
tiku_mcp.h/.c          the host table: resources, tools, watches, the process
tiku_mcp_console.c     channel 0xF3: the queue, the pump, the frame out
tiku_mcp_vfs.h/.c      /sys/mcp, a boot mount
```

The `mcp` shell command (`shell/commands/tiku_shell_cmd_mcp.c`) prints
what `/sys/mcp` holds.  The host side is `tinymcp/bridge/tinymcp.py`,
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
  `tiku_vfs_read_lines()`), `write` (`path`, `value`: `tiku_vfs_write()`,
  the status name on failure), `ls` (`path`), `shell` (`line`: the parser
  with a capture backend whose `putc` is `tinymcp_sink_putc()`; a mode
  command, `watch`, `basic`, `ping`, `ntp`, `dns`, `mqtt`, `slip` or
  `telnet`, is refused).  The capture backend carries `TIKU_VFS_CAP_ALL`:
  the console is the owner's cable.
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
- Watches: `tiku_vfs_watch()` under the `tinymcp` process, up to
  `TIKU_MCP_WATCH_MAX` (8) across streams, each a (stream, slot) the
  library named; `TIKU_EVENT_VFS` becomes `tinymcp_updated()`.  The
  process also watches `/data`, from its first run, after the boot has
  assembled the tree: a write or a delete there is hashed against the
  names and the prompt files it saw last, and a change in the names sends
  `tinymcp_listchanged()`, a change in the prompts
  `tinymcp_prompts_changed()`.  That watch holds one of the kernel's
  `TIKU_VFS_WATCH_MAX` slots for the boot.

## Execution

The decoder hands a frame over inside `tiku_console_getc()`, from the
line editor or a running command, so `on_frame()` copies it into one of
two queue slots and the pump handles it with `tinymcp_handle()` between
command lines, where rules and jobs run.  A third request while two
wait is answered `-32603` "busy" from the callback.  A reply is one
frame through `tiku_console_frame_begin/put/end()` and then
`tiku_console_flush()`, which the USB Serial/JTAG wire needs to send a
frame that ends without a newline.

## Memory

`TIKU_MCP_IN_CAP` and `TIKU_MCP_OUT_CAP` (1024 and 4608; 2048 and 8192
on the ESP32-C61 and P4; 256 and 512 on MSP430) size the channel buffer,
the two queue slots and the reply; `TIKU_MCP_SCRATCH` (4098) holds one
page of a node.  Measured on the nRF54L15 at `-Os`: the binding 5.4 KB
of code, the library 11.0 KB, 13.3 KB of SRAM in all.

## Proof

`tinymcp/tests/board_smoke.py <port>` drives a board through the bridge
with the official `mcp` SDK client in both eras: discover and the board's
identity, list, read, write the LED, subscribe and see the change, a shell
line, a refused mode command, a prompt written to `/data` got and heard
going, path completion, and the error codes.  TikuBench's `tinymcp` suite
runs it.
