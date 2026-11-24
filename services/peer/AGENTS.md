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
tiku_peer.c        lookup, the lifecycle calls, boot binding; no state
tiku_peer_wire.c   a driver for a carrier of whole messages
tiku_peer_uart.c   the board's link UART as that driver's carrier
tiku_peer_vfs.c    /sys/peer/<name>/state, role, carrier, restarts
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

## The TikuComputer-nRF

| Side | Peer | Role | Carrier |
|------|------|------|---------|
| MSP430 | `nrf` | supervisor, through the board controls | the link UART; the console line with `TIKU_BOARD_CONSOLE_SEL=1` |
| nRF54L | `msp` | worker | the link UART |

Both have the services `ctl`, for the pair's own control messages, and
`msg`, for applications.  Where the peer rides the console line the
Makefile sets `TIKU_PEER_ON_CONSOLE`, and the console's own pump serves its
frames while the shell is inside a command.

The link runs at `TIKU_BOARD_LINK_BAUD`, 9600 unless `make LINK_BAUD=` says
otherwise, the same on both sides.  Measured on the board with the peer on
the MSP430's console line:

| Baud | Frames the MSP430 received damaged | Round trip of a 32-byte message |
|------|------------------------------------|---------------------------------|
| 9600 | 0 of 504 | 0.48 s |
| 19200 | 3 of 89 | 0.22 s |
| 38400 | 8 of 87 | 0.14 s |

A damaged frame is sent again.  What damages one is a hardware overrun: the
eUSCI holds one received byte, and with kernel IPC running the receive
interrupt is at times held off for longer than a byte takes, 260 us at
38400.  At 115200 a byte takes 87 us and most frames are damaged.  With the
MSP430's console on J11 the peer rides the link UART, which kernel IPC
drains every tick: 40 of 40 messages came back at 38400, 0.19 s each.

The epoch needs no random source on the MSP430: `hal/tiku_ipc_epoch_hal.c`
builds it from the die's ID and a durable count.

## Proof

Host test `test_peer` with the `peer` mutations
(`TikuBench/tests/host/kernel/mutations/interfaces.py`).  On the board, the
firmware test `TEST_UART_PEER` starts the peer and waits for ready, and
`peer ping <endpoint> <count>` against the other side's `ipc echo` checks
messages both ways.
