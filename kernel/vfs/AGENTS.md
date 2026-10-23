# kernel/vfs/

A static virtual filesystem exposing system state as paths (`cat
/sys/uptime`, `write /dev/led0 1`).  `tiku_vfs.h/.c` is the resolver; the
production tree is one module per subtree under `tree/`:

```
tiku_vfs_tree.c               root assembly (/sys /dev /proc /data) and init
tree/tiku_vfs_tree_sys.c      /sys files, device/ mem/ cpu/ sched/, and
                              crypto/ radio/ bt/ flpr/ config/ where built
tree/tiku_vfs_tree_boot.c     /sys/boot (reason count stage rstiv hang clock/
                              mpu/) and the /sys files boot_count, last_reset,
                              cold_boots (lifetime uptime in seconds)
tree/tiku_vfs_tree_timer.c    /sys/timer, /sys/clock, /sys/htimer
tree/tiku_vfs_tree_watchdog.c /sys/watchdog
tree/tiku_vfs_tree_power.c    /sys/power
tree/tiku_vfs_tree_persist.c  /sys/persist (cells primed moved manifest)
tree/tiku_vfs_tree_watch.c    /sys/watch (slots), /sys/vfs (manifest, events)
tree/tiku_vfs_tree_dev.c      /dev leds, buttons, console, null, zero, buses
tree/tiku_vfs_tree_gpio.c     /dev/gpio, /dev/gpio_dir, /dev/gpio_owner
                              (macro-generated pins)
tree/tiku_vfs_tree_data.c     /data (the file store) and /sys/fs/data
```

The other modules under `tree/` serve /sys/mem/layout, /sys/net,
/sys/net/wifi, /sys/usb, /sys/store, /sys/psram, /sys/gpu, /sys/flash,
/sys/emmc, /sys/npu, /sys/coproc and /dev/sensors where the build has them.

`/proc` is `kernel/process/tiku_proc_vfs.c`.

## Boot assembly

`tiku_vfs_tree_init(config)` builds the core namespace; a NULL config
selects no optional providers.  `boot/tiku_boot_vfs.c` selects them: the
shell's `/sys/rules` and `/sys/jobs` (`shell/tiku_shell_vfs.c`), the init
table's `/sys/init` (`services/init/tiku_init_vfs.c`), tinymcp's
`/sys/mcp` (`services/tinymcp/tiku_mcp_vfs.c`), BASIC's `/data/basic`
(`basic/tiku_basic_vfs.c`) and the applications overlay's `/gui`.  Provider nodes and child tables must have static lifetime.  Rules,
jobs and init are mounts and list after the static `/sys` children; BASIC
is a static `/data` child ahead of the dynamic files; a runtime mount under
a dynamic directory is refused (EACCES).  An invalid configuration, a
failed mount or a failed NVM relock leaves no root published and halts
boot before the drivers start.  The mount table has `TIKU_VFS_MOUNT_MAX`
(22) entries: 17 for the driver registry and five for the providers.
Subtree hardware initialization runs once; a later call rebuilds the
namespace.  `tiku_boot_vfs_restore()` rebuilds it and remounts the drivers
without reinitializing hardware, shell state or the GUI channel.

## Adding a node to an existing subtree

1. Write the static read/write handler in the matching
   `tree/tiku_vfs_tree_<x>.c`.
2. Add an entry to that module's children table.
3. If the table is exported (`tiku_vfs_tree_<x>_children`), bump the
   `TIKU_VFS_TREE_<X>_NCHILD` macro in the module header; a `_Static_assert`
   beside the table catches a mismatch.

## Adding a subtree

1. Create `tree/tiku_vfs_tree_<new>.h/.c`.  Export a children array with
   an `NCHILD` count macro (static attach, like watchdog) or a `_get()`
   node getter (runtime attach, like /proc and /data).
2. Reference it from the parent table: `tree/tiku_vfs_tree_sys.c` for
   /sys, `tree/tiku_vfs_tree_dev.c` for /dev, or `tiku_vfs_tree.c` for a
   new top-level directory (bump `root_children[]`).
3. Add the `.c` to the VFS block of the root `Makefile`.
4. A module with boot-time setup gets an init function called from
   `tiku_vfs_tree_init()` (boot, dev and sys are the examples).

## Watch

`tiku_vfs_watch(path, &proc)` makes every successful `tiku_vfs_write()` to
that node post `TIKU_EVENT_VFS` (data = the `const tiku_vfs_node_t *`) to
the process; a driver whose value changes without a write calls
`tiku_vfs_notify(node)` (ISR-safe).  `unwatch` and `unwatch_all` release;
subscribing is idempotent; the table is `TIKU_VFS_WATCH_MAX` (8) SRAM slots,
per boot.  An event means "touched, re-read for the value": no coalescing,
and a write of an unchanged value still posts one.  The shell's rules and
`watch` command sit on this primitive (`shell/AGENTS.md`).

## Manifest and ids

`/sys/vfs/manifest` lists every node, boot mounts included and a dynamic
directory's files left out, as `path type perms meta cap id` (`manifest_rev`
4); the id is `tiku_vfs_node_id()`, the node's address, stable for the boot.
The manifest pages from any line (`read /sys/vfs/manifest <line> <bytes>`)
because a link reply is bounded.

## Streamed read

`tiku_vfs_read_at(path, offset, buf, max, &total)` copies a node's bytes
from `offset` on, with no NUL, and reports the full length; 0 bytes means
the end.  A node whose descriptor carries a `read_at` producer
(`TIKU_VFS_DESC_STREAM`) and a dynamic file whose directory has a `read_at`
op serve every offset, so a reader holding one small buffer walks content
of any length.  `/sys/vfs/manifest` (`tiku_vfs_manifest_at()`) and the
`/data` files (through `tiku_tfs_map()`) do.  Any other node is rendered
whole into the caller's buffer: offset 0 always reads, a later offset only
when all of it fit, else `TIKU_VFS_ENOTSUP`; such a node whose read drains or has
an effect refuses every offset past 0 without rendering.  The producer
serves the same bytes the text handler renders, and the read cache is not
consulted.

## Persistent nodes

Declare the value in durable memory, then its gate and descriptor with
`TIKU_PERSIST_CELL(cell, var, key, default, def_len)` from
`kernel/memory/tiku_mem.h`.  Call `tiku_persist_cell_init(&cell)` from the
module's init: it validates the magic gate and primes the default on virgin
NVM (data first, gate last).  Updates go through `tiku_persist_cell_write()`
/ `_write_u32()` (gate already valid) or `_commit()` (value then gate,
self-validating); the cell API owns every MPU unlock window.  Pick a fresh
magic key per cell and bump it when the cell's meaning changes.
`/sys/persist/cells` and `primed` report how many cells were validated and
how many primed this boot; primed != 0 on an established device means a
layout change or NVM corruption.  `moved` reports the cells this boot moved
to an updated image's places, and `manifest` where this image keeps each
cell.

## Proof

Host: `TikuBench/tests/host/kernel/test_vfs_assembly.c` (built per
profile), `test_vfs*.c`; `mutations/vfs.py` lists the source mutations the
streamed-read checks in `test_vfs_model.c` and `test_vfs_data.c` must catch
(`python3 mutate.py --port vfs`).  Board: the `vfs-c-core` (with
`test_vfs_stream.c`), `vfs-c-tree`, `vfs-c-watch`, `vfs-tree`,
`vfs-observe` and `vfs-events` suites.
