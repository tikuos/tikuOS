# Hybrid Application Hosting

> **Built 2026-08-21.** All three phases are done and pushed: desktop
> `39c92d2` (A), `62cf68f` (B), `a69be15` (C); tracker `dec82ae`,
> `6deb6c0`, `f4b5492`.  Where the plan and the code differ, the code is
> right and the differences are listed at the end.

A plan for making toolkit applications placeable — in-process, out-of-process,
or loaded from a shared object — with placement as per-app policy rather than
a build decision. Written for an agent with no prior context; every claim
below was verified against the tree on 2026-08-21 (desktop `7a2b170`,
tracker `7a1b758`).

---

## 0. Context the executing agent must have

### Repos, branches, commit discipline

- Toolkit: `applications/kits/` inside the **tikuOS repo**
  (`/home/ambujv/Tiku/tikuOS`, branch `vfs-watch-wip`). The toolkit moved
  out into the six kits; what is left in `applications/TikuDesktop/` is the
  samples, apps and tools that exercise them. The repo `.gitignore` covers
  `applications/`, so **new files need `git add -f`**, and a commit should
  be verified with `git show HEAD:./applications/kits/<kit>/<file>`.
  Do not touch the `drivers` WIP change.
- Tracker: `applications/TikuTracker/` is **its own repo**
  (github.com/tikuos/tracker, branch `main`).
- Commits: author `Ambuj Varshney <ambuj.varshney@proton.me>`, subject
  `Desktop:`/`Tracker:` + what now works, ≤5 one-line `- ` bullets, **no
  assistant co-author trailers**. `tools/check_commit_msg.py` (recoverable
  from tikuOS history at `a2ef818^`) checks the shape.
- Style: new files must pass the comment-style rules — 15-line file headers,
  ≤3 prose lines per doc comment, no first person, no design history.
  Checker: `git -C /home/ambujv/Tiku/tikuOS show a2ef818^:tools/check_comment_style.py`.

### Method (non-negotiable)

Per feature: failing/proving test → implement → **mutation test** (break the
new code, confirm the specific test fails, restore) → full `make test` in
`applications/TikuTracker` ending `shell scripts hold in both front-ends` →
commit → push. `TRK_SHELL_ONLY=<name>.script bash tests/shell/run.sh` runs
one lane. In the harness use `pkill -x`, never `pkill -f` (a `-f` pattern
matches the wrapper shell's own command line and kills the session).

### Architecture as it stands

- **Descriptor**: `tiku_app_descriptor_t`
  (`applications/kits/application/tiku_app.h`) — `{id, name, start, stop,
  event, tick, pick, run}` against `tiku_app_services_t`
  `{ctx, open, frame, menus, close}`. `TIKU_APP_MAX` is 16;
  `tiku_app_registry_t` exists and is currently unused by the shells.
- **Out-of-process (the default today)**: `tiku_client_run(app)` in
  `applications/kits/application/tiku_client.c` connects to
  `$HOME/.config/tracker/desk.sock`,
  pumps messages (drains **all** pending per turn — one-per-nap starved
  drags once), ticks, then `usleep(30000)` at line ~114.
- **Wire** (`applications/kits/application/tiku_remote.h/.c`):
  `[u32 type][u32 len][payload]`,
  fd-agnostic (`read`/`write`, header accumulation `hbuf[8]/hgot` — a pty
  has no peek). Client→desktop HELLO(1)/OPEN(2)/FRAME(3)/MENUS(4)/CLOSE(5);
  desktop→client EVENT(16)/PICK(17)/CLOSED(18). Version 1 checked in HELLO.
  `REMOTE_MAX_PAYLOAD` guards length; dims capped at 1024.
  `tiku_remote_adopt` / `connect_fd` carry sessions over any fd.
- **Embedded host** (tracker repo, `src/shell/tiku_trk_desktop.c`):
  a **single-slot** `app.embed` struct hosts one descriptor when
  `TIKU_EMBED_DEMO` is set. Functions at (approx.) lines 1033–1160:
  `embed_window_draw`, `embed_svc_open/frame/menus/close`, `embed_menu_pick`,
  `embed_stop`, `embed_start`. Input routing: `embed_press` /
  `remote_press` fields forward POINTER_MOVE/UP to whoever took the DOWN.
- **Leaf-menu launching**: `find_companion` (~line 3884) climbs from
  `argv[0]` by **appending `/..`** (trimming breaks a relative argv[0]) and
  resolves **absolute** paths via `realpath` (launch chdirs to `$HOME`
  before exec — a relative path names nothing there). `offer_companions`
  (~3947) registers Terminal and Text editor via
  `tiku_trk_deskbar_add_app()`; a pick arrives as `TIKU_TRK_DB_LAUNCH_APP`
  and runs through `tiku_trk_launch_run`. `TIKU_DESK_APPS` env overrides
  the search.
- **Apps**: `applications/TikuDesktop/apps/tiku_edit.c`, `tiku_term.c` — single
  files, descriptor + `main` guarded by `#ifndef TIKU_APP_EMBED`. Examples
  in `examples/` use `TIKU_EXAMPLE_EMBED` the same way.
- **Test harness**: `tests/shell/run.sh` drives `build/dev/*` (in-process
  script engine) and the shipping `build/TikuDesktop` via `trk-conduct`
  (conductor channel, `-conduct <socket>` / `-conduct-tty <tty>`). The
  shipping split is guarded by `tools/check_shipping.py` — the runner and
  loader added below are **product**, not test infrastructure, and must not
  trip it (it greps for script-op vocabulary only).

### Known traps, learned the hard way

- Adding rows to the Deskbar leaf menu shifts every later row:
  `dbmenu.script` (menu string, 2 places) and `dbmenuicons.script` (art
  pixel) must be updated when the app list changes.
- Fixture homes are rebuilt per lane; anything writing to real `$HOME` in a
  unit test must `setenv("HOME", scratch, 1)` first (see `test_model`).
- Socket dirs: create **every** parent (`.config` may not exist on a fresh
  home) — both listeners already do this; keep the property.
- `make test` recipe lists tests by hand; a binary in `TESTBINS` is not
  necessarily run. Add new suites to **both** lists.
- Alphabetical script order matters only via fixtures; each lane starts
  clean.

---

## Phase A — event-driven client wakeup (smallest, do first)

**Goal.** Cut keystroke→repaint latency for out-of-process apps from a
30 ms nap cycle to near-immediate, with no behavior change.

**Change.** In `tiku_client_run_fd` (`../kits/application/tiku_client.c`), replace
the unconditional `usleep(30000)` with `poll()` on the session fd, timeout
**10 ms**. The timeout must stay short and unconditional: `tick()` is how
`tiku_term` pumps its pty and how the clock example repaints — the runtime
does not know the app's other fds, so ticks must keep their cadence even
when the session is quiet. The drain loop above the nap is unchanged.

**Proving test.** Extend `applications/TikuTracker/tests/test_remote.c` (or a
new `test_client_latency` in the same style): over a socketpair, send an
EVENT to a client running the demo descriptor and measure
send→FRAME-arrival under 20 ms (generous; the nap made it 30–60 ms). Use a
socketpair, not a pty, to keep it deterministic. Run it in the `test:`
recipe.

**Mutation.** Revert poll to `usleep(30000)` → latency test fails; restore.

**Acceptance.** Full suite green both front-ends; `remoteapp.script`,
`embedapp.script`, `terminal.script` unchanged and green.

**Commit** (tikuOS repo): `Desktop: a client wakes when spoken to`.

---

## Phase B — shared objects, a runner, and placement as policy

**Goal.** Every app ships as one `.so`; the desktop either `dlopen`s it
(trusted tiles) or spawns it under a generic runner (default). Placement is
decided by which directory the file sits in — no prefs plumbing.

### B1. The export convention (toolkit)

In `../kits/application/tiku_app.h` define:

```c
#define TIKU_APP_ABI 1u
/* The one symbol a loadable application exports. */
#define TIKU_APP_EXPORT "tiku_app_v1"
typedef struct {
    uint32_t abi;               /* TIKU_APP_ABI of the builder   */
    uint32_t size;              /* sizeof(tiku_app_descriptor_t) */
    const tiku_app_descriptor_t *app;
} tiku_app_export_t;
```

Version in the **symbol name** (`_v1`) so an incompatible future export is
refused by absence, and `abi`+`size` checked so a stale `.so` is refused
loudly rather than crashed into. Each app adds, under a new
`#ifdef TIKU_APP_SO`, a `const tiku_app_export_t tiku_app_v1`
naming its descriptor. `main` stays under `#ifndef TIKU_APP_EMBED` —
independent guards, one file, three build products.

### B2. The loader (toolkit, `../kits/application/tiku_app_load.c` + decl in app.h)

```c
const tiku_app_descriptor_t *tiku_app_load(const char *path,
                                                     char *err, size_t max);
```

`dlopen(path, RTLD_NOW | RTLD_LOCAL)`, `dlsym(TIKU_APP_EXPORT)`, check
`abi` and `size`, return the descriptor or write **why not** into `err`
(dlerror text included — a refused load must say so, in the status line,
not silently show no row). Add `-ldl` where needed (glibc ≥2.34 folds it
into libc; keep `-ldl` on the link lines anyway, it is harmless). New file
⇒ `git add -f`.

### B3. Build products (toolkit Makefile)

- `build/apps.d/tiku_edit.so`, `tiku_term.so` (+ the four examples):
  `$(CC) $(CFLAGS) -fPIC -shared -DTIKU_APP_EMBED -DTIKU_APP_SO -o $@ $< $(LIBS)`.
  Note the libs are static archives compiled without `-fPIC`; on x86-64
  and aarch64 this links into a `.so` with text relocations at worst —
  cleaner: add a `-fPIC` object flavour of the three libs
  (`build/pic/*.a`) and link `.so`s against those. Budget the Makefile
  work for it; it is mechanical.
- `build/tiku-run` from a new `apps/tiku_run.c` (~40 lines):
  `main(argv)` → `tiku_app_load(argv[1])` → `tiku_client_run()`.
  Errors print the loader's `err` and exit 2.
- Keep the existing standalone binaries building; they become a convenience,
  not the mechanism.

### B4. Placement policy (tracker repo, desktop shell)

Replace the hardcoded `known[]` table in `offer_companions` with a scan:

- Resolve the app directory: `$TIKU_DESK_APPS` if set, else climb from
  `argv[0]` (reuse `find_companion`'s walk) looking for `apps.d/`.
- `apps.d/*.so` → leaf-menu row, **spawned via `tiku-run`** (found beside
  the `.so` or by the same climb). Label = descriptor `name`? No — that
  needs loading; label = filename stem, de-`tiku_`-prefixed, underscores to
  spaces, capitalised (`tiku_edit.so` → "Edit"). Keep it dumb and
  predictable.
- `apps.d/trusted/*.so` → **loaded in-process** at startup via
  `tiku_app_load` and started through the embed host. Moving a file
  between `apps.d/` and `apps.d/trusted/` is the whole policy mechanism.
- Executables in `apps.d/` keep working as plain launches (today's route).
- A `.so` that fails to load: leaf row still appears, disabled, with the
  loader's reason in the status line on pick-attempt — never a silent gap.

### B5. Generalize the embed host (tracker repo — the main lift)

`app.embed` is single-slot. Make it `app.embeds[8]` (8 is plenty;
`TIKU_APP_MAX` is 16 but windows are the real bound):

- Each slot: `{descriptor, state, window, frame, fw, fh, running}` — the
  existing struct, arrayed.
- `embed_svc_*` callbacks already take a `ctx`; make `ctx` the **slot**,
  not the app. `embed_menu_pick`, close-box handling, `embed_press`
  routing, and the tick loop iterate slots. `embed_press` becomes a slot
  pointer, mirroring `remote_press`.
- `TIKU_EMBED_DEMO` keeps working (the demo becomes just another slot) —
  `embedapp.script` must stay green unmodified; it is the regression net
  for this refactor.
- In-process apps returning done from `event`/`pick` → `embed_stop(slot)`.

**Done 2026-08-25, later the same day:** both gaps below are closed and
the About IS a descriptor now (`tiku_trk_about_app`, id
`org.tikuos.about`, hosted through `embed_open_or_raise`).  The contract
grew `place()` -- a window ROLE, not a coordinate, because an
application does not know the screen -- and `resize()` for the About's
refit when the face changes; both appended in the `present()` manner.
Cmd-W became the runtime's key, decided before any application sees it.
about.script pins real closure with a pixel (the title noun reads only
the FOCUSED window, so alone it cannot tell closed from buried), and
embedapp.script opens the About over the running demo, where slot one's
cascade and the ANNOUNCE centring differ -- the one place an honoured
role is distinguishable from a lucky default.

**Measured 2026-08-25, against the tree as it stands.** The slot array
above EXISTS (`embeds[TIKU_TRK_EMBED_MAX]`, per-slot services, ticks
pumped for every running slot, pointer and key events routed to the
focused slot, the close gesture and a done-returning `event`/`pick`
reaching `embed_stop`) — the demo descriptor rides all of it.  What
stands between here and "the shell's About is a descriptor" is exactly
two things, and neither is the services contract:

1. **Start-on-demand.** `embed_start` is called once, at boot, behind
   `TIKU_EMBED_DEMO`.  A menu row needs `embed_open_or_raise(app,
   descriptor)`: a running slot found by `descriptor->id` gets its
   window activated; otherwise the descriptor starts.  Small, and the
   registry idiom for it already exists on the blocking side.
2. **Placement is the real design decision.** `embed_svc_open` cascades
   windows by slot index; the About centres itself.  Converted naively,
   the flagship window MOVES, and about.script's clock-sensitive fade
   pixels re-anchor.  Either the contract grows a placement hint, or
   the shell owns placement per descriptor id — decide this before
   converting anything with a scripted pixel surface.

The About conversion itself is then a rewrite of
`tiku_trk_about_window.c` (388 lines: the R5 layout, the controller
facts, the logo fade driven today by the shell's 75 ms periodic) into a
descriptor whose `tick` owns the fade — with about.script and
sysmenu.script as the regression net, and pixel-identity as the bar.

### B6. Tests

- **Unit** `tests/test_appload.c` (tracker repo, links desktop libs like
  `test_remote`): build a fixture `.so` at test time with `cc -shared`
  into `/tmp` (the suite already shells out; keep `$HOME` scratch): (1) a
  good export loads and `start` runs against fake services; (2) `abi+1` is
  refused with a message; (3) a missing symbol is refused. Add to
  `TESTBINS` **and** the `test:` recipe.
- **Shell** `tests/shell/appsd.script` + a `run.sh` case block that stages
  `build/apps.d/` into the fixture: copy `tiku-example-clock`'s `.so` into
  `apps.d/trusted/`, `sketch.so` into `apps.d/`. Script: open leaf →
  `expect-menu Clock|Sketch` → pick Clock → `expect-title Clock` (opened
  with **no new process** — assert via `expect-windows` count and, in the
  case block after the run, `pgrep -c tiku-run` = 0 for the trusted one) →
  pick Sketch → `expect-title Sketch` (runner path). Escape both.
- **Leaf-menu shift**: update `dbmenu.script` / `dbmenuicons.script`
  expectations if fixture rows change (see traps).

**Mutations** (each: break → named test fails → restore):
1. Skip the abi check in the loader → `test_appload` refusal case fails.
2. Point trusted loading at the runner instead → the no-new-process
   assertion in `appsd.script` fails.
3. Drop the `apps.d` scan → `expect-menu Clock|Sketch` fails.

**Commits.** Toolkit: `Desktop: one artifact, three homes` (export, loader,
runner, .so builds). Tracker: `Tracker: placement is a directory, not a
build` (scan, trusted loading, multi-slot embed host, tests).

---

## Phase C — shared-memory frames (do last; independent of B)

**Goal.** Remove the per-repaint pixel copy (~0.9–1.1 MB per frame over the
socket today) for out-of-process apps, keeping the process boundary.

### Protocol

- HELLO grows a **feature word**: bump payload to
  `u32 version, char name[32], u32 features` with bit 0 =
  "I can pass and map fds". The listener must keep accepting the 36-byte
  v1 HELLO (length-discriminate) — old clients and the **serial transport
  keep the copying FRAME path forever**; a pty cannot pass an fd, and
  `test_remote`/`test_conduct` prove that path stays alive.
- New client→desktop `SURFACE(6)`: `u32 id, i32 w, i32 h, u32 nbuf(=2)`
  with the `memfd` in `SCM_RIGHTS` ancillary data (this is the one message
  that needs `sendmsg`/`recvmsg` instead of `write`/`read` — isolate it;
  the rest of the wire stays fd-agnostic).
- New `FRAME_READY(7)`: `u32 id, u32 index`. Double-buffer: the client
  paints buffer `index^1` while the desktop blits `index` — no tearing, no
  locks; the desktop copies nothing, it blits from the mapping in
  `remote_window_draw`.
- Client side: `tiku_remote_frame` transparently upgrades — if the
  connection negotiated shm, allocate `memfd_create` +`ftruncate` on first
  frame, `memcpy` into the current back buffer (the app API is unchanged),
  send READY. Sessions without the feature keep FRAME(3).
- Desktop: mmap on SURFACE, `munmap`+`close` in `session_free` (the sweep
  already reclaims dead sessions — extend it; leaking a mapping per crashed
  app is the failure mode to test for).

### Tests

- Extend `test_remote.c`: a socketpair pair negotiates shm, sends SURFACE +
  READY, asserts the desktop-side session sees the right pixel **without a
  FRAME message**; then a pty pair asserts negotiation correctly falls back
  to FRAME (feature bit refused).
- `remoteapp.script` unchanged — it must pass identically through the shm
  path (the demo app runs over a socket, so it will negotiate shm; the
  drag-ink pixels are the proof the blit is live).
- Valgrind or a session-count assert for the unmap-on-sweep path if cheap;
  otherwise a unit check that `session_free` leaves no mapping (track count
  in the listener, assert 0 after frees).

**Mutations**: (1) blit from the wrong buffer index → drag-ink pixel in
`remoteapp.script` reads stale; (2) skip munmap in `session_free` → the
mapping-count unit assert fails; (3) claim the feature over a pty →
`test_conduct`/`test_remote` pty leg fails.

**Commit** (toolkit): `Desktop: a frame is referenced, not copied` +
tracker test updates if any.

---

## Order, budget, and what is out of scope

| Phase | Size | Risk | Depends on |
|---|---|---|---|
| A — poll wakeup | ~½ day | low | nothing |
| B — .so / runner / trust dir | ~2 days | medium (embed refactor) | nothing |
| C — shm frames | ~2 days | medium (fd passing, sweep) | nothing (nicer after A) |

Out of scope, deliberately: crash-demotion of trusted apps (a crashed
in-process app takes the desktop; the trust directory is the user choosing
that), window resize for apps, `.so` unloading/reloading (load-once; quit
and restart the desktop to pick up a new build), and any device-side
loader.

Definition of done for the whole scheme: `make` in both trees builds
`apps.d/` and `tiku-run`; a `.so` dropped in `apps.d/` appears in the leaf
menu and runs in its own process; the same file moved to `apps.d/trusted/`
runs in-process with no other change; every existing script and unit suite
is green; each new behavior has a mutation that kills a named test.

---

## What was built differently, and why

- **`build/apps`, not `apps.d`.** The Makefile ends with
  `-include $(BUILD)/*.d`; a directory called `apps.d` is caught by that
  glob and stops the build with "Is a directory".
- **A named shared object, not `SCM_RIGHTS`.** Both sides read the wire
  as a byte stream with `read()`, which consumes the bytes an ancillary
  descriptor rides with and drops the descriptor.  The client creates a
  `shm_open` object, sends the NAME in `SURFACE`, and the desktop maps it
  and unlinks it -- no `sendmsg` anywhere, and the wire stays
  fd-agnostic.  The feature is claimed only when `getsockopt(SO_TYPE)`
  says the link is a socket, which is how "same machine" is asked rather
  than assumed.
- **`TIKU_DESK_APPS` no longer redirects the companion search.** It says
  where applications are installed; the runner is found by climbing from
  `argv[0]`.  One variable doing both jobs made the runner unfindable
  whenever applications lived elsewhere.
- **The scan sorts.** `readdir` order is arbitrary, and a leaf menu whose
  rows move between runs cannot be learned -- or tested.
- **The fixture owns what is installed.** `run.sh` builds
  `/tmp/trk_apps` per lane and passes `TIKU_DESK_APPS`; a trusted
  application left in the build tree otherwise opens a window in every
  script (it cost 62 failures once).
- **`offer_companions` is gone.** The directory scan covers what it did,
  and keeping both listed Terminal twice.
- **Mappings are counted on the listener.** `session_free` clears the
  session either way, so a leak is invisible in the freed fields; the
  count is what the mutation test can see.
