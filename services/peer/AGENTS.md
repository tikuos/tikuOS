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
`TIKU_PEER_F_ONESHOT`, and `stop` is refused.  A peer the board is one
computer with carries `TIKU_PEER_F_AUTOSTART`: its supervisor starts it at
boot.

A supervisor switches its peer at run time:

```text
peer start nrf
peer stop nrf
peer reset nrf
```

`tiku_peer_start()`, `tiku_peer_stop()` and `tiku_peer_reset()` are the
calls under them.  A stop ends the sessions, parks the medium and takes the
peer's power; a start gives it back and the sessions say hello again.

A board lists its peers in `tiku_peer_table`, which boot assembly defines.
A peer's services take consecutive rows of the endpoint table, in the order
of its service list.  `tiku_peer_prepare_all()` runs before kernel IPC
starts and `tiku_peer_bind_all()` after it; the second finds the row named
`<peer>/<first service>` and hands it to the driver.  `tiku_peer_start_all()`
then starts the peers flagged for it; one that does not start shows as off
or lost, and boot goes on.

## Files

```
tiku_peer.c              lookup, the lifecycle calls, boot binding; no state
tiku_peer_wire.c         a driver for a carrier of whole messages
tiku_peer_uart.c         the board's link UART as that driver's carrier
tiku_peer_vfs.c          /sys/peer/<name>/state, role, carrier, restarts, speed
tiku_peer_ctl.c          the process that owns <peer>/ctl: the link's speed
tiku_peer_mount.c        /peer/<name>: a VFS call turned into requests
tiku_peer_mount_proc.c   the process that owns <peer>/vfs
tiku_peer_mount_serve.c  one request answered from this side's tree
```

`boot/tiku_boot_peer.c` is the table of a board with one peer on a UART; it
serves both sides of the TikuComputer-nRF.  The `peer` shell command lists
the peers, calls the lifecycle, sets the link's speed, and sends, listens
and pings on an endpoint.

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

Each session is told the carrier's speed in message bytes a second
(`byte_rate` of `tiku_peer_wire_t`; on a UART `TIKU_BOARD_LINK_BAUD` × 3/40,
a quarter of the line going to framing and escapes).  It sends an unanswered
frame again only after the time the frame, its answer and one frame of the
peer's take at that speed, so a slow link carries each frame once.

The board may name a call the driver makes at bind with the peer's first
endpoint (`attend`).  With the link UART it registers a hook
(`tiku_uart_link_on_event()`): on the nRF54L the UART's interrupt requests
that endpoint's service when a frame's last byte is in and when a write has
been sent, and the session's pass then reads and writes the link.  The
MSP430's link UART has no such interrupt and is read on the tick; its
console line wakes its own reader when a burst has landed.

## The link's speed

The link starts at the board's speed, `TIKU_BOARD_LINK_BAUD`, and changes at
run time, by either side:

```text
peer speed msp 460800
write /sys/peer/msp/speed 460800
```

`tiku_peer_set_speed()` is the call under both; `/sys/peer/<name>/speed`
reads the link's speed, then the one asked for when the two differ.  A
speed is one of the link UART's rows: 9600, 19200, 38400, 115200, 230400,
250000, 460800, 500000, 921600 and 1000000 bits a second on both chips of
the TikuComputer-nRF.  A peer on the console line keeps the console's speed,
and the call returns `TIKU_PEER_ERR_DRIVER` there.

The change is made on `<peer>/ctl` by the "Peer control" process
(`tiku_peer_ctl.c`, messages in `tiku_peer_ctl.h`):

| Step | Asker | Answerer |
|------|-------|----------|
| 1 | sends ASK with the speed | answers yes, no (no setting) or busy |
| 2 | on yes, switches once its link has nothing left to send | switches once its answer and the asker's receipt of it are through |
| 3 | sends CHECK at the new speed | answers CHECKED, and keeps the speed |
| 4 | on CHECKED, keeps the speed | |

What holds:

- One change runs at a time.  Asked while asking, the worker drops its own
  change and the supervisor answers busy.
- No answer in 3 s, no CHECKED 3 s after the switch, or no CHECK 4 s after
  it: the change fails, and a side that switched goes back to the starting
  speed.
- Both sides keep the speed and ask for it again 5 s after the link has
  another, so a restart of either side brings the link back to it.  Three
  failed tries in a row give it up.
- The asked speed is RAM: a board that resets both chips starts again at
  `TIKU_BOARD_LINK_BAUD`, and its setup code calls `tiku_peer_set_speed()`
  again.

The wire driver watches what the medium hears away from the starting speed
(`TIKU_PEER_WIRE_GUARD_S`): 16 bytes or more outside whole frames for 3 s
mean the peer talks at another speed, as a restarted peer does.  The driver
then goes back to the starting speed and renews the sessions, so the peer
hears its hellos garbled and comes back too; the peer counts it as a
restart.  A receiver faster than its sender sees only breaks, so both link
UART drivers put a break in their ring as a zero byte.

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
| 9600 | 0.3 | 0.3 | 1.8 | 1.5 | 1.5 |
| 115200 | 0.09 | 0.08 | 0.34 | 0.28 | 0.27 |

No frame was sent twice in these runs.  With the MSP430's peer on its link
UART and the speed set at run time:

| Speed | `read` of a counter | `write` of an LED | 220-byte file written | read | `ls` of 26 entries |
|------|------|------|------|------|------|
| 115200 | 0.10 | 0.08 | 0.34 | 0.35 | 0.35 |
| 460800 | 0.09 | 0.08 | 0.33 | 0.29 | 0.32 |
| 1000000 | 0.09 | 0.08 | 0.28 | 0.27 | 0.31 |

Above 115200 the time goes to the MSP430, which reads its link UART on the
tick.

The nRF54L's `uart relay` reads the link itself and does not run the
scheduler: while it runs, that side serves no request.

## The TikuComputer-nRF

| Side | Peer | Role | Carrier |
|------|------|------|---------|
| MSP430 | `nrf` | supervisor, through the board controls | the link UART; the console line with `TIKU_BOARD_CONSOLE_SEL=1` |
| nRF54L | `msp` | worker | the link UART |

The MSP430 starts the nRF54L at boot (`TIKU_BOARD_PEER_AUTOSTART` in its
board header), with its console on J11 as with its console on the link.

Both have the services `ctl`, for the pair's own control messages such as
the link's speed, `msg`, for applications, and `vfs`, for the mount.  Where the peer rides the console line the
Makefile sets `TIKU_PEER_ON_CONSOLE`, and the console's own pump serves its
frames while the shell is inside a command.

The link starts at `TIKU_BOARD_LINK_BAUD`, 9600 unless `make LINK_BAUD=`
says otherwise, the same on both sides.  Measured on the board with the peer
on the MSP430's console line, 100 messages at each rate:

| Baud | Frames the MSP430 received damaged |
|------|------------------------------------|
| 9600 | 0 of 473 |
| 38400 | 0 of 324 |
| 115200 | 0 of 406 |

At 9600, 40 round trips of a 32-byte message took 0.36 s each, with no
frame sent twice.

The MSP430's eUSCI holds one received byte.  A DMA channel moves each into
the driver's ring (`arch/msp430/tiku_uart_dma_arch.h`), so none is lost
while kernel IPC holds interrupts off.  With the MSP430's console on J11
the peer rides the link UART, which the same kind of ring feeds and kernel
IPC drains every tick: 40 of 40 messages came back at 115200 with no byte
lost, 0.17 s each.

The epoch needs no random source on the MSP430: `hal/tiku_ipc_epoch_hal.c`
builds it from the die's ID and a durable count.

## Proof

Host tests `test_peer`, `test_peer_ctl` and `test_peer_mount` with the
`peer`, `peer-ctl` and `peer-mount` mutations
(`TikuBench/tests/host/kernel/mutations/interfaces.py`); the second answers
the control process as a scripted peer, and the third mounts this side's
tree on itself through the VFS core and the mount's three files.  On the board, the
firmware test `TEST_UART_PEER` starts the peer and waits for ready, and
`peer ping <endpoint> <count>` against the other side's `ipc echo` checks
messages both ways.  For the mount, the paths above from the nRF54L's shell,
and from the MSP430 a `once` job that writes and reads under `/peer/nrf`
after the relay has ended.  For the speed, `peer speed msp` through every
row from the nRF54L's shell with the MSP430's peer on its link UART; a
reset of the nRF54L at 460800, after which the link came back at 460800;
and the nRF54L's UART set to another speed behind its driver with the
J-Link, after which both sides fell back and the link came back.
