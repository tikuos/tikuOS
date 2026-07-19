# Loadable extensions & modules — Tier 2 and Tier 3 (implementation)

How Tiku BASIC lets native code — kernel services, tikukits, and (Tier 3)
*separately-compiled, runtime-installed* modules — add new BASIC words without
editing the interpreter. This documents **what is actually built**, file by
file, with the data flow. Design rationale lives in `kintsugi/loadable.md`;
this is the "how it works" companion.

The three tiers (from `loadable.md`):

| Tier | What loads | When | Status |
|---|---|---|---|
| 1 | BASIC **source** modules (`IMPORT`) | at the REPL | done |
| 2 | **native** words compiled INTO the firmware | at boot | **done** |
| 3 | **native** modules compiled SEPARATELY, installed at runtime | on `MODLOAD` | **MVP done (nordic)** |

The unifying idea: **one ABI**. Tier 2 defines a small, stable contract —
"here is how a native function becomes a BASIC word." Tier 3 reuses that exact
contract, reached through a jump table, so code that arrives at runtime plugs
in the same way boot-time code does.

---

## Tier 2 — the native builtin registry

### The idea

Before Tier 2, adding a native BASIC word meant editing the interpreter's
dispatch source (a `match_kw` case, a token-table entry, `#if` guards). Tier 2
replaces that with a **registry**: a small table of `{name → handler}` that the
interpreter consults at the *fallthrough* of each dispatch chain. A feature
registers its word once; the interpreter never learns the feature's name.

### The public API — `kernel/shell/basic/tiku_basic_ext.h`

Three ways to register, by the kind of word:

```c
/* numeric function: MYFN(a[, b]) -> a number.  arity 0..2. */
int tiku_basic_register_fn (const char *name, uint8_t arity,
                            tiku_basic_ext_nfn fn);
/* string function: MYFN$(...) -> a string.  name MUST end in '$'. */
int tiku_basic_register_strfn(const char *name, tiku_basic_ext_strfn fn);
/* statement: MYSTMT <args>   (no return value). */
int tiku_basic_register_stmt(const char *name, tiku_basic_ext_stmt_fn fn);
```

The three handler shapes:

```c
/* numeric: the interpreter pre-parses (a[,b]) and hands you the values. */
typedef int  (*tiku_basic_ext_nfn)  (const long *args, int argc, long *out);
/* statement + string: SELF-PARSING -- cursor sits past the name; you parse
   your own args and (string) write the result into out[cap]. */
typedef void (*tiku_basic_ext_stmt_fn)(const char **p);
typedef void (*tiku_basic_ext_strfn) (const char **p, char *out, size_t cap);
```

The **services** a handler may call (and nothing else — this is the stable
surface Tier 3 also uses):

```c
int  tiku_basic_ext_parse_expr   (const char **p, long *out);        /* number */
int  tiku_basic_ext_parse_strexpr(const char **p, char *buf, size_t cap);
void tiku_basic_ext_print(const char *s);          /* console (PRINT's stream) */
void tiku_basic_ext_error(int cat, const char *msg);        /* raise an error  */
int  tiku_basic_ext_expect(const char **p, char ch);    /* consume '(' ',' ')' */
```

### The table — `tiku_basic_state.inl`

```c
typedef struct {
    char    name[TIKU_BASIC_EXT_NAME_MAX];  /* "" = free slot */
    uint8_t kind;                           /* 0 stmt, 1 numeric fn, 2 str fn */
    uint8_t arity;                          /* numeric fns: 0..2 */
    union { tiku_basic_ext_stmt_fn stmt;
            tiku_basic_ext_nfn     nfn;
            tiku_basic_ext_strfn   strfn; } u;
} basic_ext_entry_t;
static basic_ext_entry_t basic_ext_tab[TIKU_BASIC_EXT_MAX];   /* default 16 */
```

The table lives in SRAM, is registered at boot, and is **outside** the BASIC
arena and the checkpoint — it is firmware configuration, not program state.

### The dispatch hooks — where the registry plugs in

Each of the interpreter's three dispatch chains falls through to the registry
*after* every builtin and *before* the variable / error path, so a registered
name is a reserved word:

| Kind | Hook site | Behaviour |
|---|---|---|
| numeric fn | `tiku_basic_call.inl` (~730) | pre-parses `(a[,b])`, calls `u.nfn` |
| statement | `tiku_basic_dispatch.inl` (~430) | calls `u.stmt(p)` (self-parses) |
| string fn | `tiku_basic_string.inl` (~1461) | calls `u.strfn(p,out,cap)` (self-parses) |

Crucially, registered names are **never in the A2 token table**, so in a stored
(crunched) program they arrive as raw text and reach these hooks through
`match_kw`'s text path — they work identically in immediate mode and in a
`RUN` program.

### Dispatch flow (numeric function example)

```
BASIC line:  PRINT GCD(48, 36)
    |
  expression parser  -> builtin function chain (SIN, ABS, LEN, ...)  -> no match
    |
  registry fallthrough (tiku_basic_call.inl):
     for each slot: name=="GCD" && kind==1 ?  -> yes
        parse (48, 36) per arity=2
        u.nfn({48,36}, 2, &out)  ->  bext_gcd -> out = 12
    |
  12   (printed)
```

### Registration — the bundled kit

The registry needs clients. `tiku_basic_ext_kits.inl` is the first one — a
bundle of genuinely-useful native words registered through the **public API**
(zero edits to the dispatch chain):

```c
static void basic_ext_register_kits(void) {
    tiku_basic_register_fn("GCD",    2, bext_gcd);     /* GCD(a,b)      */
    tiku_basic_register_fn("ISQRT",  1, bext_isqrt);   /* ISQRT(n)      */
    tiku_basic_register_fn("BITCNT", 1, bext_bitcnt);  /* BITCNT(n)     */
    tiku_basic_register_stmt("HEXPR",   bext_hexpr);   /* HEXPR n       */
    tiku_basic_register_strfn("REV$",   bext_rev);     /* REV$(s)       */
    tiku_basic_register_strfn("ROMAN$", bext_roman);   /* ROMAN$(n)     */
}
```

`basic_session_begin()` (`tiku_basic_shell.inl`) calls this **once**, guarded,
before any dispatch — extensions persist across BASIC sessions.

### Adding your own word (the whole point)

From a kernel service or tikukit's init — no interpreter file touched:

```c
static int my_crc(const long *a, int argc, long *out) {
    *out = (long)tiku_kits_crc32((const uint8_t*)&a[0], 4); return 0;
}
void my_kit_init(void) { tiku_basic_register_fn("CRC32", 1, my_crc); }
/* now BASIC can say:  PRINT CRC32(x) */
```

### Tier 2 file map

| File | Role |
|---|---|
| `tiku_basic_ext.h` | public API + handler/service typedefs |
| `tiku_basic_ext.inl` | register impl, name validation, the ABI services |
| `tiku_basic_state.inl` | the registry table + entry struct |
| `tiku_basic_dispatch.inl` / `_call.inl` / `_string.inl` | the three dispatch hooks |
| `tiku_basic_ext_kits.inl` | the bundled native words (first client) |
| `tiku_basic_shell.inl` | one-time boot registration |
| `tiku_basic_config.h` | `TIKU_BASIC_EXT_MAX` (16), `TIKU_BASIC_EXT_KITS` |

Proven: LM20 hardware + host smoke (`test_basic_smoke`, 342/0), including
crunched-program dispatch, string-expr composition, and the error path.

---

## Tier 3 — runtime-loadable native modules (MVP)

### The idea

A **module** is machine code compiled *separately* from the firmware, that
arrives at runtime (over a link, or embedded as the delivery source in the
MVP), is **installed into non-volatile memory**, and **executes in place** to
register Tier-2 words. Because it is separately compiled, it links **no**
firmware symbols — it reaches every service through a **jump table**.

Two verbs:
- **install** — write the module image into the NVM slot (durable).
- **activate** — validate the resident image and call its entry point, which
  registers its words. Runs on install *and* at every boot (the code is
  durable; only the volatile Tier-2 table needs re-populating).

### Why RRAM, not SRAM — the forcing function

The nordic MPU enforces **W^X: SRAM is execute-never** (`tiku_mpu_arch.c`). So
a module **cannot** run from a RAM buffer — it must run from **RRAM**, which is
executable. That constraint is a gift: RRAM is byte-writable in place, so the
install is a `memcpy` behind the write gate (no erase, no bank swap), and the
module is **durable for free** — it survives a power cycle because it lives in
NVM. This is the novel "execute-in-place from byte-writable NVM" design, forced
by the security policy.

### The module image format — `tiku_basic_module.h`

A module image is a 16-byte header at the slot base, then code:

```
  slot+0x00   tiku_module_header_t {
                  magic       = 'TMOD' (0x544D4F44)   <- the install GATE
                  abi_version = 1
                  init_off    = 0x11 (= 16 | 1, Thumb)
                  reserved    = 0
              }
  slot+0x10   module_init  (entry: header.init_off points here)
  slot+...    .text / .rodata  (handlers, string literals)
```

### The jump table — how separately-compiled code calls the firmware

The module can't link `tiku_basic_register_fn` etc. directly (it's compiled
alone). Instead the loader passes it a table of the Tier-2 services:

```c
typedef struct {
    uint32_t abi_version;
    int  (*register_fn)   (const char*, uint8_t, tiku_basic_ext_nfn);
    int  (*register_strfn)(const char*, tiku_basic_ext_strfn);
    int  (*register_stmt) (const char*, tiku_basic_ext_stmt_fn);
    int  (*parse_expr)    (const char**, long*);
    int  (*parse_strexpr) (const char**, char*, size_t);
    void (*print)(const char*);
    void (*error)(int, const char*);
    int  (*expect)(const char**, char);
} tiku_basic_syscalls_t;

typedef void (*tiku_module_init_fn)(const tiku_basic_syscalls_t *sys);
```

Note this is **exactly the Tier-2 ABI**, re-exposed as a struct of pointers.
The module's `module_init(sys)` calls `sys->register_fn(...)`; its handlers, if
they needed to, would call `sys->parse_expr` / `sys->print` / `sys->error`.

### The RRAM slot — `arch/nordic/devices/nrf54lm20a.ld`

A fixed 4 KB executable-RRAM slot, carved just below the durable-persist
region (FS shrinks 4 KB, its base unchanged):

```
  __tiku_module_slot = __tiku_nvm_rram_start - 0x1000    (= 0x1F8000)
```

The module is *linked at this exact VMA* (`modules/mod_demo.ld`), so every
internal reference resolves at run address — no relocation.

### Install — gate-last, durable — `tiku_basic_module.c`

```c
int tiku_basic_module_load(void) {
    saved = tiku_mpu_unlock_nvm();          /* open the RRAM write (WEN) gate  */
    for (i = 4; i < len; i++) slot[i] = src[i];  /* body first...              */
    for (i = 0; i < 4; i++)  slot[i] = src[i];   /* ...then the MAGIC word LAST */
    tiku_mpu_lock_nvm(saved);
    asm("dsb; isb");                        /* code just written -> sync pipe  */
    return tiku_basic_module_activate();
}
```

Writing the magic **last** is the gate-last discipline: a power cut mid-install
leaves an *invalid* magic, so the half-written module never activates. Never a
brick — either the old state or a fully-written module.

### Activate — XIP execution

```c
int tiku_basic_module_activate(void) {
    hdr = (tiku_module_header_t*)TIKU_MODULE_CARVE_ADDR;
    if (hdr->magic != 'TMOD' || hdr->abi_version != 1) return -1;   /* no module */
    init = (tiku_module_init_fn)(TIKU_MODULE_CARVE_ADDR + hdr->init_off);
    init(&module_syscalls);                 /* <-- executes XIP from RRAM       */
    return 0;
}
```

`init` is a function pointer *into RRAM*; calling it runs the module's code in
place. It registers `MODFIB`/`MODMUL` through the table and returns.

### Boot re-activation — how it survives reboot

`basic_session_begin()` calls `tiku_basic_module_activate()` once per boot. The
**code** is durable (RRAM persists); only the **registration** (the SRAM
Tier-2 table) is lost on reset. So a boot re-runs `module_init` from the
still-resident slot and the words come back — no re-install.

```
   install once ──> RRAM slot [ TMOD | code ]  (durable)
        │
   ┌────┴─── power cycle ───┐
   │ SRAM table wiped       │ RRAM slot intact
   ▼                        ▼
  boot: session_begin -> activate -> module_init(sys) -> words re-registered
```

### The build — separately compiled, then embedded

`modules/mod_demo.c` is compiled with its **own** toolchain invocation and
linker script, *not* as part of the firmware:

```
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -ffreestanding -nostdlib \
    -T modules/mod_demo.ld  mod_demo.c -o mod_demo.elf     (VMA = 0x1F8000)
objcopy -O binary  mod_demo.elf  mod_demo.bin              (124-byte flat blob)
objcopy -I binary -O elf32-littlearm  mod_demo.bin  mod_demo_img.o
                                       (-> _binary_mod_demo_bin_start/_end)
```

For the MVP the blob is **embedded** in the firmware as the *install source*
(the "bytes that would arrive over the air"); the durable NVM install still
happens at runtime. Delivering the blob over HTTPS/VFS instead is a transport
detail — the transport already exists.

### The demo module — `modules/mod_demo.c`

```c
#define TIKU_MODULE_BUILD 1
#include "../tiku_basic_module.h"

static int mod_fib(const long *a, int c, long *o){ /* nth Fibonacci */ }
static int mod_mul(const long *a, int c, long *o){ *o = a[0]*a[1]; return 0; }

__attribute__((section(".modhdr"),used))               /* placed at slot+0    */
const tiku_module_header_t mod_header = { 'TMOD', 1, 16|1, 0 };

__attribute__((section(".modinit"),used))              /* placed at slot+16   */
void module_init(const tiku_basic_syscalls_t *sys){
    sys->register_fn("MODFIB", 1, mod_fib);
    sys->register_fn("MODMUL", 2, mod_mul);
}
```

The handlers are **pure** (no `sys` callback), so the module holds no writable
state — the minimal proof.

### The proof (LM20 hardware)

```
> basic
> MODLOAD                 module loaded
> PRINT MODFIB(10)        55
> PRINT MODMUL(6,7)       42
--- power cycle (nrfutil reset) ---
> basic
> PRINT MODFIB(10)        55          <- still works: durable in RRAM, re-activated
> PRINT MODFIB(20)+MODMUL(3,4)   6777 <- composes in expressions
```

### What the MVP proves vs. what it defers

Proven: separate compilation · jump-table callback · durable byte-writable-NVM
install (gate-last) · execute-in-place · registration callable from BASIC ·
reboot durability.

Deferred (documented next steps):
- **Stateful handlers.** Handlers here are pure. A handler that calls back
  through `sys` (parse/print) needs the module's `.data` (its saved `sys`
  pointer) written under the WEN gate at init — a small extension.
- **OTA delivery.** The blob is embedded as the install source; real delivery
  is HTTPS/VFS → `tiku_basic_module_load()` reading from a buffer/file.
- **Other platforms.** MSP430 FRAM is an equally-clean second target
  (byte-writable + XIP); RP2350 flash (erase-first) and Ambiq MRAM
  (bootrom-mediated) are second-class — exactly the durability-inverts-across-
  NVM thesis (`kintsugi/kintsugi_sensys` / `loadable.md`).

### Tier 3 file map

| File | Role |
|---|---|
| `tiku_basic_module.h` | module ABI: header, jump table, slot address, loader API |
| `tiku_basic_module.c` | the loader: install (gate-last), activate (XIP), status |
| `modules/mod_demo.c` | the separately-compiled demo module |
| `modules/mod_demo.ld` | module linker script (VMA = the RRAM slot) |
| `arch/nordic/devices/nrf54lm20a.ld` | reserves the 4 KB executable RRAM slot |
| `Makefile` (module block) | the separate compile → blob → embed sub-build |
| `tiku_basic_repl.inl` | `MODLOAD` / `MODACT` REPL commands |
| `tiku_basic_shell.inl` | boot re-activation |
| `tiku_basic_config.h` | `TIKU_BASIC_MODULE_ENABLE` (opt-in, nordic-only) |

Enable with `TIKU_BASIC_MODULE_ENABLE=1` on a nordic BASIC build.

---

## The through-line

```
  Tier 2                         Tier 3
  ------                         ------
  register_fn/strfn/stmt         same functions, reached via a jump table
  parse/print/error/expect       same services, as struct-of-pointers
  handler compiled INTO firmware handler compiled SEPARATELY, installed to NVM
  registered at boot             registered when the module is activated
```

Tier 2 settled and battle-tested the ABI surface; Tier 3 reaches that identical
surface from separately-compiled, runtime-installed, durable NVM-resident code.
That is why finishing Tier 2 de-risked Tier 3 — the hard part (a narrow, stable
interpreter-extension contract) was already proven before anything depended on
it.
