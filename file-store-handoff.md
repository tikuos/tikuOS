# tikuOS NVM `/data` File Store — Implementation Status & Handoff

**Audience:** a fresh Claude/engineer resuming this work (esp. on **apollo4l** at home).
**Scope:** the memory/VFS-backed file store added 2026-06 (commits `0411cac`…`7bf2dea` on `main`).
**Last HW test:** apollo510 EVB (J-Link VCOM `…22991`). apollo4l cross-builds clean but the
file store is **not yet HW-tested on apollo4l** — that's the first thing to do at home.

> Conventions (keep following these): commit author **WEISER Research Group @ National
> University of Singapore** `<105362093+weiserlab@users.noreply.github.com>`, **no AI/Claude
> attribution** (scan the message before committing). Port via `arch/` + HAL; keep `kernel/`
> changes additive and tightly gated; cross-build all platforms. TikuBench and tikukits are
> their own git repos.

---

## 1. TL;DR status

| Piece | apollo510 | apollo4l | msp430 | rp2350 |
|---|---|---|---|---|
| TFS core (file store logic) | ✅ host-test 21/21 | ✅ builds | ⚠️ unbuilt here | ✅ builds |
| `/data` dynamic dir + `write`/`cat`/`ls` | ✅ HW | ⬜ build-only | ⚠️ | ✅ build (`.bss`, volatile) |
| `rm` / `touch` / `rw` flags | ✅ HW | ⬜ build-only | ⚠️ | ✅ build |
| **Durable across power loss** | ✅ HW (reset-survival) | ⬜ **expected, verify** | ⚠️ FRAM (unbuilt) | ❌ no backend yet |
| BASIC stored-program / RUN | ✅ fixed (code) | ✅ **fixed + HW** (Bug #1) | ✅ fixed (code) | ✅ fixed (code) |

✅ = HW-verified · ⬜ = builds, not HW-tested · ⚠️ = can't build on this Mac (no msp430 toolchain) · ❌ = doesn't work

---

## 2. What was built (architecture)

A small, durable, power-cut-safe file store for arbitrary files (BASIC programs, configs,
blobs), surfaced as a normal VFS directory `/data`:

```
shell:  write /data/blink.bas "10 LED 0,1"   ls /data   cat /data/blink.bas   rm /data/blink.bas   touch /data/x
```

Three layers, each independently testable:

1. **Backend substrate** — `kernel/fs/tiku_nvm_backend.h`. A `tiku_nvm_backend_t` = `{base,
   size, write(), erase(), ctx}`. Reads are pointer derefs into `base`; writes go through
   `write()`. This is the seam where the *durable medium* plugs in (RAM, FRAM-in-place,
   MRAM-mirror, future direct-MRAM, future Flash).

2. **TFS (Tiku File Store)** — `kernel/fs/tiku_tfs.c` / `.h`. Flat namespace, whole-file I/O,
   fixed slots. Superblock magic + per-entry gate; **gate-last create/delete**, **shadow-slot
   atomic overwrite** (write fresh slot, then flip the dir entry's slot index in one aligned
   word — a torn write leaves the OLD file). Depends only on the backend header → **host
   unit-testable** (`-DTFS_TEST`, 21 assertions incl. fail-injection). 0 bss, ~1.5 KB text.

3. **Dynamic VFS directory** — `kernel/vfs/tree/tiku_vfs_tree_data.c` mounts the TFS under
   `/data`. The VFS node struct gained an optional `dyn` ops pointer (`list/read/write/unlink`);
   `tiku_vfs_read/write` fall back to it **only when a path fails to resolve statically**
   (the static tree pays nothing), and `tiku_vfs_list` enumerates static then dynamic children.
   A successful dynamic write rings the directory's watchers (so `on /data …` rules work).

**Durability** is per-family (`tiku_vfs_tree_data.c`, `DATA_TFS_SECTION`):
- **Ambiq (apollo510/apollo4l):** region in `.uninit`, which the kernel already mirrors to MRAM
  on every `tiku_mpu_lock_nvm()` (`arch/ambiq/tiku_mem_arch.c::tiku_mem_arch_nvm_flush`) and
  restores at boot (`tiku_mem_arch_init`). `data_be_write()` brackets `unlock/lock_nvm`, so each
  write commits to MRAM. **Constraint:** the mirror page is ~32 KB and already ~25 KB full →
  the store is kept small on Ambiq (`TIKU_TFS_MAX_FILES = 6`). Both Ambiq `.ld`s **ASSERT**
  `.uninit ≤ mirror-4`, so an oversize store fails the build, never silently truncates.
- **MSP430:** `.persistent` FRAM, in place (16 files).
- **rp2350 / host:** `.bss` (functional but volatile) until a backend lands.

---

## 3. Commits (all on `main`, WEISER/no-AI)

```
0411cac  basic: board-aware program limits + Apollo AUTO-tier → SSRAM
         (PROGRAM_LINES: apollo510=2048, apollo4l/rp2350=1024, FRAM=256, else=50;
          fixed the 128 KB AUTO-tier cap → apollo510 1 MB / apollo4l 512 KB of SSRAM)
de6e203  fs: Tiku File Store (TFS) core
a24ab4f  fs/vfs: wire the file store to a dynamic /data directory (M3)
489654e  fs: make /data durable on Apollo via the MRAM .uninit mirror (M2)
7bf2dea  shell: rm/touch + rw flags + basic load/save/run <path> (M4)
```

---

## 4. What works (HW-verified on apollo510)

- `write /data/foo hi` → `ls /data` lists it → `cat /data/foo` → `hi`. Multiple files coexist
  with the static `/data/basic` node.
- `touch /data/x` creates an empty file; a no-op (no truncate) if it already exists.
- `rm /data/x` deletes it.
- `ls /data` shows **`rw`** for dynamic files (synthesised list node carries flag-only sentinels;
  real I/O still takes the dyn path).
- **Durability:** wrote files, **reset the board (reflash), and the files + contents came back**
  (`cat /data/persist` → `M2DURABLE-7788`). A code reflash leaves the MRAM mirror page intact and
  cold-boots the kernel through the `.uninit`-restore path; MRAM is non-volatile, so true Vcc-
  removal survival follows. (Fully-rigorous unplug/replug HITL = human-only, still pending — use
  the TikuBench `mram-powercycle` pattern for that.)
- Quoted shell args work: `write /data/h.bas "10 PRINT 6*7"` stores the spaces correctly.

---

## 5. Known bugs / what doesn't work

### ✅ Bug #1 (RESOLVED 2026-06-24): BASIC stored-program store/RUN hung — `uint8_t` loop counter
**Root cause was a `uint8_t` loop counter, NOT a fault / cache / RUN-engine issue.** The `prog_*`
line-table helpers scanned `for (uint8_t i; i < TIKU_BASIC_PROGRAM_LINES; i++)`. A `uint8_t` caps
at 255, so once `0411cac` raised PROGRAM_LINES per tier (apollo4l/rp2350=1024, apollo510=2048,
**msp430-large=256**), `i < N` was permanently true → infinite loop. It fires when the FIRST
numbered line is stored (`prog_store`'s scan never returns); the `ok>` prompt never comes back,
so the *next*-typed `RUN` looked like the culprit. Affects apollo4l, apollo510 AND msp430-large —
only the host build (PROGRAM_LINES=50) was safe, which is why "works on msp430" held (immediate
mode only, never a stored line). Original (misleading) repro — now passes, printing `42`:
```
basic            → "Tiku BASIC ready. ok>"
10 PRINT 6*7     → ok>          (was: prompt never returned — store hung here)
RUN              → 42           (was: looked like the hang)
```
**Fixed:** widened the five scan counters to `uint16_t` + a `_Static_assert`
(`tiku_basic_program.inl` prog_clear/store/next_index/find_exact; `tiku_basic_stmt.inl`
prog_find_label). With the store/RUN engine working, `basic run <path>` (load into `prog[]` +
run) works too; `basic save/load` to the persist store is still blocked separately (see below).

**What it actually was** (the original hypotheses below were all wrong, kept for the record):
not `basic_pc`, not D-cache (apollo4l SSRAM is non-cacheable yet still hung), not a 16/32-bit
fault. JLink attach-halt on apollo4l showed `IPSR=NoException` with the PC pinned in `prog_store`
= a plain busy loop (the `uint8_t` scan). For the record, the wrong guesses were:
  (a) `basic_pc` not advancing → infinite loop;
  (b) BASIC arena in cached `.ssram` → D-cache coherency corrupting iteration;
  (c) a 16- vs 32-bit assumption on the M55/M4F.

**Still open (separate issue):** BASIC *persistence* can't fit Apollo — the save buffer is
`PROGRAM_LINES*(LINE_MAX+8)` (~88 KB at 1024 lines, ~180 KB at 2048) vs the 32 KB MRAM mirror →
`persist init failed` (`tiku_basic_persist.inl::basic_persist_ensure`). `basic save/load` to the
persist store still needs fewer lines on Apollo or the direct-MRAM backend. This is unrelated to
the store/RUN hang above (now fixed).

### Bug #2 (MEDIUM): rp2350 + net (`TIKU_SHELL_NET_TEST=1`) fails to link
`ERROR: .uninit region exceeds 4 KB flash backup sector`. **Pre-existing** — the net stack's
TCP/MQTT/DNS buffers (~5 KB: `tcp_tx_pool_buf` 2352, `tcp_rx_bufs` 2048, mqtt/dns) are
`.persistent` and blow rp2350's 4 KB persist sector (`arch/arm-rp2350/devices/rp2350.ld`,
`__tiku_nvm_flash_size = 0x1000`). **Confirmed it fails at `cea1af9`, before this file-store
work** (net is Ambiq-only per the net feature). The file store's region is correctly in `.bss`
on rp2350 (not the cause); the `dyn` node field adds only ~768 B. rp2350 *supported* configs
build: default native console ✅, shell-no-net ✅ (`.uninit` 3840/4096). Not ours to fix
(net is user-authored; would need net-buffer shrink or multi-sector backup in the rp2350 arch).

### Gap #3 (LOW): MSP430 not built on this Mac
No `msp430-elf-gcc` in PATH here. The changes are additive/generic and the `.persistent` path is
unchanged from M3, but **link-check on the HW machine** (`make MCU=msp430fr5994
TIKU_SHELL_ENABLE=1 MEMORY_MODEL=large HAS_TESTS=0 HAS_EXAMPLES=0`). `basic run/load/save <path>`
*should* work there (FRAM persist + BASIC RUN both work on msp430).

### Gap #4 (LOW): true power-cycle is HITL-pending
Reset-survival is proven; full Vcc-removal survival is implied (MRAM is non-volatile) but only a
human unplug/replug fully proves it. A `/data`-files variant of TikuBench's `mram-powercycle`
HITL suite would close this.

---

## 6. Key files

| File | Role |
|---|---|
| `kernel/fs/tiku_nvm_backend.h` | backend substrate (the medium seam) |
| `kernel/fs/tiku_tfs.{c,h}` | file store; `TIKU_TFS_MAX_FILES` board-aware (Ambiq=6); host test under `#ifdef TFS_TEST` |
| `kernel/vfs/tree/tiku_vfs_tree_data.c` | `/data` dynamic dir; `DATA_TFS_SECTION` (.uninit/.persistent/.bss); `data_be_write` |
| `kernel/vfs/tiku_vfs.{c,h}` | `dyn` node field; `vfs_dyn_read/write/unlink`; `tiku_vfs_unlink`; list-node `rw` sentinels |
| `kernel/vfs/tiku_vfs_tree.c` | attaches `/data` (gated `TIKU_SHELL_ENABLE`) |
| `kernel/shell/commands/tiku_shell_cmd_fs.{c,h}` | `rm` / `touch` (gated `TIKU_SHELL_CMD_FS`) |
| `kernel/shell/commands/tiku_shell_cmd_basic.c` | `basic load/save/run <path>` (load+autorun, NOT run_source) |
| `arch/ambiq/tiku_mem_arch.c` | the MRAM `.uninit` mirror (`tiku_mem_arch_nvm_flush`, restore in `_init`) |
| `arch/{ambiq/devices/apollo510.ld, ambiq/devices/apollo4l.ld}` | `.uninit ≤ mirror` ASSERT (the safety net) |

---

## 7. Build / flash / test (reproducible)

ARM toolchain on this Mac: `export PATH="/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin:$PATH"`.
The repo's `build/` is root-owned here → use a `/tmp` build dir.

```bash
# Build (apollo4l example — full feature set)
make BUILD_DIR=/tmp/fs MCU=apollo4l TIKU_SHELL_ENABLE=1 TIKU_KIT_NET_ENABLE=1 \
     TIKU_SHELL_NET_TEST=1 TIKU_SHELL_BASIC_ENABLE=1 HAS_TESTS=0 HAS_EXAMPLES=0 TARGET=/tmp/fs.elf

# Flash (resets + runs; the qc-detach for the secure SBL is baked into `make flash`)
make BUILD_DIR=/tmp/fs MCU=apollo4l ... TARGET=/tmp/fs.elf flash      # same vars as build

# Host TFS unit test
clang -O2 -Wall -DTFS_TEST -Ikernel/fs kernel/fs/tiku_tfs.c -o /tmp/tfs_test && /tmp/tfs_test

# Drive the shell over the VCOM (pyserial 3.5 is installed). Board: apollo4l = …12821, apollo510 = …22991
python3 - <<'PY'
import serial,time
s=serial.Serial("/dev/cu.usbmodemXXXX",115200,timeout=0.4); time.sleep(3)
def cmd(c,t=2.5):
    s.reset_input_buffer(); s.write((c+"\r").encode()); time.sleep(t)
    o=b"";  t0=time.time()
    while time.time()-t0<1.2:
        d=s.read(4096); o+=d; 
        if d:t0=time.time()
    return o.decode("latin-1","replace")
print(cmd("write /data/foo hi",3), cmd("ls /data"), cmd("cat /data/foo"))
PY
```
**Match `--board`/`MCU` to the physically-connected EVB** (auto-detected port tells you which —
both Ambiq EVBs share SEGGER VID `0x1366`). `make clean` when switching MCU (shared `build/`).
On Ambiq, `reboot`/SystemReset **halts** the SBL → recover with `make flash`.

---

## 8. Roadmap (suggested order)

1. **Fix Bug #1** — ✅ DONE (2026-06-24): `uint8_t` loop-counter overflow in the BASIC `prog_*`
   helpers (infinite loop for PROGRAM_LINES>255), widened to `uint16_t`; HW-verified on apollo4l.
2. **Verify `/data` on apollo4l** — flash + confirm `write`/`cat`/`ls`/`rm`/`touch` + reset-
   survival. This session only chased Bug #1; the `/data` HW pass on apollo4l is still TODO.
3. **Direct-MRAM backend** (the "megabytes" the file store was for) — a new
   `tiku_nvm_backend_t` whose `write()` calls the bootrom `nv_program_main2`
   (`arch/ambiq/tiku_mem_arch.c` already uses it; signature `(key, op, src, dst_word_off,
   num_words)`, word-granular, no erase) into a **dedicated MRAM region** carved in the `.ld`
   (top of MRAM, below the mirror), read in place (MRAM is memory-mapped). Drop it in behind the
   backend seam — no TFS/VFS change. Bump `TIKU_TFS_MAX_FILES`/`SLOT_DATA` once the region is big.
   This also gives BASIC somewhere to persist >32 KB programs.
4. **Host injection (M5)** — tikuConsole "send file" → `write /data/<name>` over the console.
5. **rp2350 Flash backend (M6)** — erase+program a flash sector behind the backend seam.
6. **TikuBench `files` suite (M7)** — functional + a `/data` power-cycle HITL variant.

---

## 9. Gotchas
- The Ambiq `.uninit` mirror is ~25/32 KB full *before* the store — keep the Ambiq store small,
  or the `.ld` ASSERT fails the build (that's by design; it's better than silent MRAM truncation).
- `basic run <path>` uses **load+autorun, not `run_source`**. Its original rationale (`run_source`
  hangs on Ambiq) was Bug #1 — now FIXED (the `prog_*` `uint8_t` loop), so re-test before assuming
  the detour is still required.
- Don't `git add -A`; stage only intended files. The tracked `tests/tiku_test_config.h` drifts if
  you run TikuBench kernel categories — `git checkout` it before committing.
- A code reflash does NOT erase the MRAM mirror page (`0x7F8000+` on apollo510) — that's *why*
  `/data` files survive a reflash. To wipe the store, format it (or change the superblock magic).
```
