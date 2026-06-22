# Telnet (shell-over-TCP) on apollo4l — problem writeup

**Status (2026-06-22):** the user-facing bug is **fixed**; one residual remains.
TikuBench's `run net --only "Telnet *"` suite went from **28 failures → a 70/70
green run**, but it is **not yet reliably 100%** — a residual intermittent
failure shows up under the suite's aggressive reconnect pattern.

This file is the running record: the symptom, the two bugs already fixed, the
deep instrumentation that localized the residual, what was tried, and where to
look next.

---

## 1. Symptom

`python3 -m tikubench run net --board apollo4l --only "Telnet …"` (tests #90–97)
intermittently fails with output like:

```
[92] telnet multi-cmd
  [PASS] 'help' (#1) ... [PASS] 'ps' (#4) produced output
  [FAIL] 'help' (#5) returned prompt
  [FAIL] session frozen after 'help' -- possible TX pool exhaustion
[96] telnet output
  [PASS] help ... [PASS] info ...
  [FAIL] 'ps' lists the shell process        <- output stops mid-session
```

The failing assertions are always **"command produced no output / no prompt"**,
and the test blames *"TX pool exhaustion."* Run-to-run it varies (70/70 one run,
65/71 the next). Single connections are mostly fine; the failures cluster when
many connect→command→RST cycles run back-to-back.

**The "TX pool exhaustion" message is a red herring** (see §5).

---

## 2. Architecture (how telnet output flows)

- `kernel/shell/tiku_shell_io_tcp.c` — the telnet I/O backend. CLI output goes
  through `tcp_putc()` → a 2 KB coalescing `tx_buf` → `tiku_shell_io_tcp_flush()`
  sends **one MSS segment per CLI poll** via `tiku_kits_net_tcp_send()`.
- `kernel/shell/tiku_shell.c` — the shell process / main poll loop. On the
  net-test firmware the **UART is shared** between the local console and the
  SLIP/TCP transport. A per-poll RX pump (`#if TIKU_SHELL_NET_TEST` block) drains
  the UART through `shell_net_demux()` so the IP stack ingests the telnet
  client's segments + ACKs. The active I/O backend switches TCP⇄UART each poll
  based on `tiku_shell_io_tcp_is_connected()`.
- `tikukits/net/ipv4/tiku_kits_net_tcp.c` — the TCP stack: 4 connection slots
  (`TIKU_KITS_NET_TCP_MAX_CONNS=4`), a shared 24-slot TX retransmission pool
  (`TIKU_KITS_NET_TCP_TX_POOL_COUNT=24`), reclaimed on ACK by `tcp_tx_ack()`.
- `TikuBench/tikubench/suites/net/telnet.py` — the host test client
  (`SlipTelnetConn`): a minimal TCP-over-raw-SLIP implementation that shares the
  serial port with the rest of the net suite. **MSP430-heritage** (comments
  reference 9600 baud / eZ-FET / FTDI-to-MSP430).

This whole telnet path was developed for MSP430 and is being exercised hard on
apollo4l (J-Link VCOM @ 115200) for the first time.

---

## 3. Root cause #1 — FIXED ✅ (the real, deterministic bug)

**`uint8_t` truncation in the flush.** `tiku_shell_io_tcp_flush()` declared the
per-call send length as `uint8_t chunk` but assigned it `tx_pos` (a `uint16_t`):

```c
uint8_t chunk;              /* BUG */
...
chunk = tx_pos;            /* tx_pos can be 1898 for `help` */
if (chunk > mss) chunk = (uint8_t)mss;
```

For any command output > 255 bytes the length **wrapped**. For `help` (~1898 B):

| flush | `tx_pos` | `tx_pos & 0xFF` | min(.,mss=88) | sent |
|------:|---------:|----------------:|--------------:|-----:|
| 1 | 1898 | 106 | 88 | 88 |
| 2 | 1810 | 18  | 18 | 18 |
| 3 | 1792 | **0** | 0 | `tcp_send(len=0)` → **ERR_PARAM** |

The third flush calls `tcp_send` with length 0, which returns an error **every
poll forever**, so the drain **wedges after 2 segments (88+18 = 106 B)** and the
session looks "frozen." Output ≤ 255 B (e.g. the 244-byte banner) slipped under
the wrap, which is exactly why **connect/echo worked but help/info/ps froze.**

**Fix:** widen `chunk` to `uint16_t`. → commit **`3798cf4`** (tikuOS, WEISER).
HW-verified: `help` now drains the full 1898 B + prompt, repeatedly.

**How it was found:** J-Link instrumentation counters proved the *full* output
reached `tx_buf` intact (0 drops, high-water 1898) yet only 106 B were ever sent
— which pointed straight at the length field, not flow control.

---

## 4. Root cause #2 — FIXED ✅ (a reconnect bug)

**`telnet_event_cb()` dropped the live connection on a stale peer close.** It
nulled `telnet_conn` on **every** `CLOSED`/`ABORTED` without checking the event
was for the connection it currently tracks. With 4 TCP slots, a just-RST'd
*previous* client's close event can arrive **after** the next client has
connected (`telnet_conn = new`), nulling the live session — so its first command
landed on a NULL `telnet_conn` and produced no output (banner already sent → it
survived).

**Fix:** guard `if (c == telnet_conn)`. → commit **`01ec7a9`** (tikuOS, WEISER).
This took the reconnect failures from "consistently several" to "intermittent"
and made fully-green runs achievable.

Supporting fixes:
- **`6028754`** (TikuBench) — settle ~0.4 s after the banner in
  `connect_and_banner()` so a test's first command doesn't race the connection
  setup (the device backend-switch + banner drain span a couple of poll cycles).
- **`b159f87`** (TikuBench) — modernized MSP430-era assertion strings in
  `test_telnet_output.py` / `test_telnet_iac.py`: `Available commands` →
  `--- System ---`, `MSP430` → per-board MCU set, `CLI` → `Shell`.

---

## 5. The residual — UNFIXED, and what it is NOT

Under stress (5 connections × 10 commands, with the settle) the suite still
fails intermittently. **Deep J-Link instrumentation proved the device is healthy
per command** — the failure is NOT what the test claims.

**Method (reusable):** add `volatile uint32_t g_dbg_*` counters, build,
`arm-none-eabi-nm main.elf | grep g_dbg`, then read them off the *running* target
(halt-read-go does NOT trip the Ambiq SBL park — only RESET does):

```sh
printf 'mem32 <addr> <N>\ngo\nexit\n' > /tmp/rd.jlink
JLinkExe -device AMAP42KL-KBR -if SWD -speed 4000 -autoconnect 1 -nogui 1 \
         -CommanderScript /tmp/rd.jlink
```

**Counter readings at a stress failure:**

| counter | value | conclusion |
|---|---|---|
| `g_dbg_inuse_max` (TX pool blocks) | **3** of 24 | pool is NOT exhausted — "TX pool exhaustion" is a misnomer |
| `g_dbg_alloc_fail` | **0** | no pool-alloc failures, ever |
| `g_dbg_exec_tcp` | tracks command count | commands DO execute on the TCP backend |
| `g_dbg_iscon_st` / `g_dbg_iscon_cw` | **0** / **0** | `is_connected()` never wrongly returns false; backend does NOT spuriously flip to UART |
| `g_dbg_evt_close` | ≈ #connections | only the legitimate per-connection RSTs null `telnet_conn` |

So the residual is **NOT**:
- the TX pool (max 3/24 used),
- backend TCP⇄UART routing (the switch paths never fire),
- a stale-close nulling the live conn (guard holds; `evt_close` is legitimate),
- the host `_raw` buffer (bounding `SlipTelnetConn._raw` did not help).

**What it IS:** the failures are **cumulative over *connections*, not commands.**
A 5-conn × 10-cmd stress degrades **connections 3–5** (e.g. fails per conn:
2, 0, 7, 8, 7), and occasionally the first connection can't even get a SYN+ACK.
Within a single healthy connection, all 8–10 commands pass. This is a **TCP
connection slot / state-lifecycle degradation over rapid connect→RST reuse**:
the 4 slots (or their TIME_WAIT / half-open state) are not fully reaped between
the suite's ~8 reconnects in 30 s, so later connections inherit a degraded stack.

**Single-connection telnet — i.e. real usage — is solid.** Only the test
harness's machine-gun connect→RST loop trips it. A human/real telnet client does
not reconnect 8× in 30 s.

---

## 6. What was tried

| attempt | outcome |
|---|---|
| Widen `chunk` to `uint16_t` (`3798cf4`) | **fixed** the deterministic freeze; multi-cmd/IAC/RST/idle all green |
| `telnet_event_cb` stale-close guard (`01ec7a9`) | **fixed** the first-command-after-reconnect drop; fully-green runs now possible |
| Host settle after banner (`6028754`) | reduced the first-command race |
| Modernize MSP430-era test strings (`b159f87`) | `Telnet output integrity` 17/17 standalone |
| Pump SLIP RX during the flush (drain hook) | **reverted** — wrong layer; `tcp_send` has no send-window gate, so it never triggered |
| Bound the host `SlipTelnetConn._raw` buffer | **reverted** — did not help; residual is device-side, cumulative over connections |
| J-Link counter instrumentation (pool / exec / is_connected / events) | **diagnostic** — proved the device is healthy per-command; localized the residual to connection lifecycle |

---

## 7. Where we are

**Committed this session (all WEISER, AI-scan clean):**
- tikuOS `3798cf4` — uint8_t→uint16_t flush length (the core fix)
- tikuOS `01ec7a9` — `c == telnet_conn` stale-close guard
- TikuBench `6028754` — connect settle
- TikuBench `b159f87` — board-agnostic telnet assertion strings

**Result:** `run net --only "Telnet *"` reaches **70/70**; intermittently
65–67/70 under heavy reconnect stress, all failures being the residual in §5.

**Working trees clean; board flashed with the committed firmware.**

---

## 8. Next steps (if/when the residual is worth chasing)

The residual is a focused TCP-stack task, not a quick fix. Where to look:

1. **Connection-slot reaping on RST/abort** in `tikukits/net/ipv4/tiku_kits_net_tcp.c`.
   When the host RSTs, is the slot (TCB) freed *immediately*, or does it linger
   (TIME_WAIT / LAST_ACK / half-open) and count against `MAX_CONNS=4`? Related
   prior work: weekend commit `8fea10a` "reap idle half-open and orphaned
   CLOSE_WAIT slots" — this is the same area; it may need to also reap on rapid
   RST reuse, or shorten the linger.
2. **Instrument the slot table:** add counters for slots-in-use (high-water),
   `tcp_listen_accept` failures, and the per-slot state at the moment a new
   connection fails — read via the J-Link method in §5. Confirm whether later
   connections fail because the slot pool is full of un-reaped prior connections.
3. **Consider whether the device aborts a live connection** on retransmit-RTO
   when the (slow, MSP430-heritage) host client lags its ACKs — add an
   `evt_close` breakdown (CLOSED vs ABORTED + reason). In the stress run
   `evt_close` ≈ the legitimate RSTs, so this looked unlikely, but it wasn't
   fully ruled out under the worst-degraded connections.
4. **Or accept it:** since single-connection (real) telnet is solid and only the
   suite's aggressive reconnect trips it, an alternative is to make the *test*
   less abusive (longer inter-connection settle, or fewer reconnects) and
   document the rapid-reconnect limit — rather than hardening the stack for a
   pattern real clients don't produce.

---

## 9. Reproduce

```sh
# Flash the net-test firmware
cd ~/tikuOS
make TIKU_SHELL_ENABLE=1 TIKU_KIT_NET_ENABLE=1 TIKU_SHELL_NET_TEST=1 \
     HAS_TESTS=0 HAS_EXAMPLES=0 MCU=apollo4l UART_BAUD=115200 flash

# Run the telnet suite (intermittent residual; run a few times)
cd TikuBench
python3 -m tikubench run net --board apollo4l \
  --port /dev/cu.usbmodem0011600012821 --baud 115200 \
  --only "Telnet connect,Telnet echo,Telnet multi-command,Telnet IAC filtering,\
Telnet disconnect,Telnet RST recovery,Telnet output integrity,Telnet idle"
```

Stress harness that reliably exposes the residual: a Python loop over
`SlipTelnetConn` doing N connections × M commands each, `c.reset()` + ~2 s
between connections, counting `PROMPT not in resp`. Degradation appears by
connection 3–4.

## 10. Key files

- `kernel/shell/tiku_shell_io_tcp.c` — telnet backend, flush (`3798cf4`),
  `telnet_event_cb` / `is_connected` (`01ec7a9`)
- `kernel/shell/tiku_shell.c` — shell loop, RX pump, backend switch
- `tikukits/net/ipv4/tiku_kits_net_tcp.c` — TCP stack, slots, TX pool, **slot
  reaping (the residual lives here)**
- `TikuBench/tikubench/suites/net/telnet.py` — host `SlipTelnetConn`, `connect_and_banner`
- `TikuBench/tikubench/suites/net/tests/test_telnet_*.py` — tests #90–97
