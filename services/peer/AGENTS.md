# services/peer/

A peer is another processor this TikuOS instance works with: the nRF54L
beside the MSP430 on a TikuComputer, a radio chip beside its host, a second
core.  `interfaces/peer/tiku_peer.h` is the contract; `TIKU_PEER_ENABLE=1`
builds it on a board that declares one (`BOARD_CAPS` `PEER`), and switches
kernel IPC on.

## The three parts of a peer

| Part | What it is | Where |
|------|------------|-------|
| Lifecycle | `start`, `stop`, `reset`, and a state: off, starting, ready, lost with a reason | the peer's driver (`tiku_peer_ops_t`) |
| Carrier | how whole messages travel | a `tiku_link`, or a kernel IPC backend |
| Services | what travels | kernel IPC endpoints named `<peer>/<service>` (`kernel/ipc/README.md`) |

The side that switches the other's power or reset is its supervisor
(`TIKU_PEER_F_SUPERVISOR`); the other side is the worker, and `start`, `stop`
and `reset` are refused there.  A peer whose start cannot be undone carries
`TIKU_PEER_F_ONESHOT`, and `stop` is refused.

A board lists its peers in `tiku_peer_table`, which boot assembly defines.
A peer's services take consecutive rows of the endpoint table, in the order
of its service list.  `tiku_peer_prepare_all()` runs before kernel IPC
starts and `tiku_peer_bind_all()` after it; the second finds the row named
`<peer>/<first service>` and hands it to the driver.

## Files

```
tiku_peer.c              lookup, the lifecycle calls, boot binding; no state
tiku_peer_wire.c         a driver for a carrier of whole messages
tiku_peer_uart.c         the board's link UART as that driver's carrier
tiku_peer_vfs.c          /sys/peer/<name>/state, role, carrier, restarts
tiku_peer_mount.c        /peer/<name>: a VFS call turned into requests
tiku_peer_mount_proc.c   the process that owns <peer>/vfs
tiku_peer_mount_serve.c  one request answered from this side's tree
```

`boot/tiku_boot_peer.c` is the table of a board with one peer on a UART; it
serves both sides of the TikuComputer-nRF.  The `peer` shell command lists
the peers, calls the lifecycle, and sends, listens and pings on an endpoint.

## The wire driver

One physical link, a channel of `tiku_ipc_mux` per service, and on each
channel a `tiku_ipc_wire` session (`services/ipc/README.md`).  The session's
hello carries an epoch, a number that differs on every boot:

- The peer is ready while its first service's session is.
- A new epoch from a ready peer counts one restart.
- Started and silent for the board's start time, the peer is lost with the
  reason timeout; with its medium refused, carrier.
- The session has no keep-alive.  A peer that stops after ready is noticed
  when it says hello again, or when this side's own sends go unanswered.

The board supplies the link, a call that opens or parks the medium under
it, and on the supervisor side the power and reset controls.  The link is
opened once, at bind, and stays open: a parked UART reads nothing and takes
what is written, so a stop and the next start need no new link.

A session holds three frames of `TIKU_IPC_WIRE_FRAME` bytes and two blocks
of `TIKU_IPC_WIRE_BLOCK` (`services/ipc/tiku_ipc_wire.h`).  A peer's sessions
carry copied messages only, so a build with peers and without `console/ipc`
sets them to 128 and 16 (the Makefile's peer block): 696 bytes a session
on the nRF54L, where the defaults take 2,072.  Both sides are built with
the same two values; a session refuses a hello that names larger ones.

## The mount: /peer/<name>

A peer that lists a service named `vfs` is mounted.  Its file tree is the
directory `/peer/<name>` here, and this side's tree is under `/peer` there:

```text
read /peer/msp/sys/uptime
write /peer/msp/dev/led0 1
ls /peer/nrf/data
```

`ls`, `read`, `write`, `rm` and every other caller of the VFS take such a
path as they take a local one.  `/peer/<name>` is a dynamic directory
(`kernel/vfs/AGENTS.md`); its operations send requests on `<peer>/vfs` and
return what the same call returns on the peer.  A request and its answer are
one copied message each, 64 bytes at most; `tiku_peer_mount.h` has the
layout.

| Operation | Carries |
|-----------|---------|
| LIST | a directory's names, as many as fit an answer, from a given entry on |
| STAT | whether a path is a directory or a file |
| READ | 56 bytes of a file from a byte offset, and the file's length |
| WRITE, MORE | a value's length and its bytes, 56 at most in each request |
| UNLINK | the deletion of a file |

What holds on both sides:

- One request is out at a time.  Its caller runs the scheduler until the
  answer is in, so the processor stays awake for that time; a second caller
  meanwhile gets `EBUSY`, and so does the mount's own process.
- A peer that is not ready answers nothing: every call returns `ENOTSUP`
  and sends no request.  The mount does not start a peer.  No answer within
  `TIKU_PEER_MOUNT_WAIT_S` (3 s), or a peer that restarts or is lost
  meanwhile, returns `EIO`.
- The serving side leaves `/peer` out of what it serves, so two sides that
  mount each other form no loop, and it does not read a node marked secret.
- A peer's writes and deletes run under `TIKU_PEER_MOUNT_CAP`: hardware and
  the file store by default, not system control or network settings.
- A read from offset 0 renders the file once into a buffer of
  `TIKU_PEER_MOUNT_READ_BUF` (512) bytes, and the parts after it are served
  from that buffer for two seconds.  A file with a streamed producer reads
  to any length; another file is cut as a local read into a buffer of that
  size is.
- The longest value a peer can write is `TIKU_PEER_MOUNT_WRITE_MAX` (256)
  bytes.
- `TIKU_PEER_MOUNT_PEERS` (1) of the table's peers are mounted; each costs
  the serving side's buffers, about 1 KB of RAM.
- A watch on a mounted path is not carried to the peer.

Measured on the TikuComputer-nRF from the nRF54L's shell, the MSP430
serving, in seconds from the Enter key to the last byte of output:

| Baud | `read` of a counter | `write` of an LED | 220-byte file written | read | `ls` of 26 entries |
|------|------|------|------|------|------|
| 9600 | 0.3 | 0.5 | 2.1 | 2.0 | 1.9 |
| 115200 | 0.12 | 0.13 | 0.63 | 0.44 | 0.42 |

The nRF54L's `uart relay` reads the link itself and does not run the
scheduler: while it runs, that side serves no request.

## The TikuComputer-nRF

| Side | Peer | Role | Carrier |
|------|------|------|---------|
| MSP430 | `nrf` | supervisor, through the board controls | the link UART; the console line with `TIKU_BOARD_CONSOLE_SEL=1` |
| nRF54L | `msp` | worker | the link UART |

Both have the services `ctl`, for the pair's own control messages, `msg`,
for applications, and `vfs`, for the mount.  Where the peer rides the console line the
Makefile sets `TIKU_PEER_ON_CONSOLE`, and the console's own pump serves its
frames while the shell is inside a command.

The link runs at `TIKU_BOARD_LINK_BAUD`, 9600 unless `make LINK_BAUD=` says
otherwise, the same on both sides.  Measured on the board with the peer on
the MSP430's console line, 100 messages at each rate:

| Baud | Frames the MSP430 received damaged | Round trip of a 32-byte message |
|------|------------------------------------|---------------------------------|
| 9600 | 0 of 473 | 0.44 s |
| 38400 | 0 of 324 | 0.13 s |
| 115200 | 0 of 406 | 0.11 s |

The MSP430's eUSCI holds one received byte.  A DMA channel moves each into
the driver's ring (`arch/msp430/tiku_uart_dma_arch.h`), so none is lost
while kernel IPC holds interrupts off.  With the MSP430's console on J11
the peer rides the link UART, which the same kind of ring feeds and kernel
IPC drains every tick: 40 of 40 messages came back at 115200 with no byte
lost, 0.17 s each.

The epoch needs no random source on the MSP430: `hal/tiku_ipc_epoch_hal.c`
builds it from the die's ID and a durable count.

## Proof

Host tests `test_peer` and `test_peer_mount` with the `peer` and
`peer-mount` mutations
(`TikuBench/tests/host/kernel/mutations/interfaces.py`); the second mounts
this side's tree on itself through the VFS core and the mount's three
files.  On the board, the
firmware test `TEST_UART_PEER` starts the peer and waits for ready, and
`peer ping <endpoint> <count>` against the other side's `ipc echo` checks
messages both ways.  For the mount, the paths above from the nRF54L's shell,
and from the MSP430 a `once` job that writes and reads under `/peer/nrf`
after the relay has ended.
