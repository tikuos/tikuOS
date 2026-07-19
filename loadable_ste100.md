# Loadable extensions and modules — Tier 2 and Tier 3 (implementation)

**Note.** This document uses ASD-STE100 Simplified Technical English. Sentences
are short and active. Each term has one meaning. Code blocks and identifiers
keep their exact form.

This document tells you how native code adds new BASIC words. Native code is
kernel services, tikukits, and Tier 3 modules. A Tier 3 module is a separate
compilation. You install it at run time. The interpreter source does not
change. The design reasons are in `kintsugi/loadable.md`. This document is the
companion. It tells you how the code operates.

The three tiers (from `loadable.md`):

| Tier | What loads | When | Status |
|---|---|---|---|
| 1 | BASIC **source** modules (`IMPORT`) | at the REPL | done |
| 2 | **native** words compiled INTO the firmware | at boot | **done** |
| 3 | **native** modules compiled SEPARATELY, installed at run time | on `MODLOAD` | **MVP done (nordic)** |

Tier 2 and Tier 3 use one ABI. The ABI is a small, stable contract. The
contract tells you how a native function becomes a BASIC word. Tier 2 sets the
contract. Tier 3 uses the same contract through a jump table. Thus code that
arrives at run time connects in the same way as boot-time code.

---

## Tier 2 — the native builtin registry

### Function

Before Tier 2, you added a native word with a change to the interpreter
dispatch source. The change was a `match_kw` line, a token-table entry, and
`#if` conditions. Tier 2 removes this. Tier 2 uses a registry. The registry is
a table of name-to-handler pairs. The interpreter reads the table at the end of
each dispatch chain. A feature registers its word one time. The interpreter
does not learn the feature name.

### The API — `kernel/shell/basic/tiku_basic_ext.h`

```c
/* numeric function: MYFN(a[, b]) -> a number.  arity 0..2. */
int tiku_basic_register_fn (const char *name, uint8_t arity,
                            tiku_basic_ext_nfn fn);
/* string function: MYFN$(...) -> a string.  name MUST end in '$'. */
int tiku_basic_register_strfn(const char *name, tiku_basic_ext_strfn fn);
/* statement: MYSTMT <args>   (no return value). */
int tiku_basic_register_stmt(const char *name, tiku_basic_ext_stmt_fn fn);
```

There are three functions. Each function is for one kind of word:

- `tiku_basic_register_fn` registers a numeric function. It returns a number.
- `tiku_basic_register_strfn` registers a string function. The name must end
  with `$`.
- `tiku_basic_register_stmt` registers a statement. It has no return value.

There are three handler shapes:

```c
/* numeric: the interpreter pre-parses (a[,b]) and hands you the values. */
typedef int  (*tiku_basic_ext_nfn)  (const long *args, int argc, long *out);
/* statement + string: SELF-PARSING -- cursor sits past the name; you parse
   your own args and (string) write the result into out[cap]. */
typedef void (*tiku_basic_ext_stmt_fn)(const char **p);
typedef void (*tiku_basic_ext_strfn) (const char **p, char *out, size_t cap);
```

A numeric handler is different from the other two. The interpreter parses the
arguments for a numeric handler. The interpreter gives the values to the
handler. A statement handler and a string handler parse their own arguments. On
entry, the cursor is after the name.

A handler can call these services only. The services are the stable surface.
Tier 3 also uses these services:

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

The table is in SRAM. The firmware fills the table at boot. The table is not in
the BASIC arena. The table is not in the checkpoint. The table is firmware
configuration. It is not program state.

### The dispatch hooks

Each dispatch chain has a hook at its end. The hook is after all built-in
words. The hook is before the variable path and the error path. Thus a
registered name is a reserved word.

| Kind | Hook site | Behaviour |
|---|---|---|
| numeric fn | `tiku_basic_call.inl` (~730) | parses `(a[,b])`, calls `u.nfn` |
| statement | `tiku_basic_dispatch.inl` (~430) | calls `u.stmt(p)` |
| string fn | `tiku_basic_string.inl` (~1461) | calls `u.strfn(p,out,cap)` |

A registered name is not in the token table. Thus a stored program keeps the
name as raw text. The name reaches the hook through the text path of
`match_kw`. The name operates in the same way in immediate mode and in a `RUN`
program.

### Dispatch flow (numeric function)

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

### Registration

The registry needs clients. `tiku_basic_ext_kits.inl` is the first client. It
is a set of native words. The words are useful. The words are not built-in. The
file registers the words through the public API. The file does not change the
dispatch chain.

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

`basic_session_begin()` calls this function one time. A guard stops a second
call. The call is before any dispatch. The words stay for all BASIC sessions.

### Add your own word

You add a word from a kernel service or a tikukit. You do not change an
interpreter file.

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
| `tiku_basic_ext.inl` | register code, name checks, the ABI services |
| `tiku_basic_state.inl` | the registry table + entry struct |
| `tiku_basic_dispatch.inl` / `_call.inl` / `_string.inl` | the three dispatch hooks |
| `tiku_basic_ext_kits.inl` | the bundled native words (first client) |
| `tiku_basic_shell.inl` | boot registration |
| `tiku_basic_config.h` | `TIKU_BASIC_EXT_MAX` (16), `TIKU_BASIC_EXT_KITS` |

Result. The LM20 hardware shows correct operation. The host test
`test_basic_smoke` shows 342 pass results and 0 fail results. The test includes
stored-program dispatch, string-expression use, and the error path.

---

## Tier 3 — runtime-loadable native modules (MVP)

### Function

A module is machine code. You compile the module separately from the firmware.
The module arrives at run time. In the MVP, the firmware holds the module bytes
as the source. The loader installs the module into non-volatile memory. The
module operates in place. It registers Tier-2 words.

The module is a separate compilation. Thus it links no firmware symbols. It
reaches each service through a jump table.

There are two operations:

- Install. The loader writes the module image into the NVM slot. The image is
  durable.
- Activate. The loader checks the image and calls the module entry point. The
  entry point registers the words. Activate runs at install and at each boot.
  The code is durable. Only the SRAM table needs a refill.

### Why RRAM, not SRAM

The nordic MPU makes SRAM execute-never (W^X). Thus a module cannot operate from
a RAM buffer. A module must operate from RRAM. RRAM is executable. RRAM is also
byte-writable in place. Thus the install is a `memcpy` operation behind the
write gate. There is no erase. There is no bank swap. The module is durable. It
stays after a power cycle, because it is in NVM.

### The module image format — `tiku_basic_module.h`

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

### The jump table

The module cannot link `tiku_basic_register_fn` directly. The module is a
separate compilation. Thus the loader gives the module a table of the Tier-2
services:

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

This table is the Tier-2 ABI. The table is a set of function pointers. The
module `module_init(sys)` calls `sys->register_fn(...)`. A handler can also call
`sys->parse_expr`, `sys->print`, or `sys->error`.

### The RRAM slot — `arch/nordic/devices/nrf54lm20a.ld`

```
  __tiku_module_slot = __tiku_nvm_rram_start - 0x1000    (= 0x1F8000)
```

The slot is 4 KB. It is below the durable-persist region. The FS gets 4 KB
less. The FS base does not change. The module links at this exact address
(VMA). Thus each internal reference is correct at run time. There is no
relocation.

### Install — gate-last — `tiku_basic_module.c`

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

The loader writes the magic word last. This is the gate-last rule.

If the power stops during an install, the magic is not complete. Thus an
incomplete module does not become active. The result is the old state or a
complete module. The result is never a failed device.

### Activate — XIP operation

```c
int tiku_basic_module_activate(void) {
    hdr = (tiku_module_header_t*)TIKU_MODULE_CARVE_ADDR;
    if (hdr->magic != 'TMOD' || hdr->abi_version != 1) return -1;   /* no module */
    init = (tiku_module_init_fn)(TIKU_MODULE_CARVE_ADDR + hdr->init_off);
    init(&module_syscalls);                 /* <-- executes XIP from RRAM       */
    return 0;
}
```

`init` is a function pointer into RRAM. The call runs the module code in place.
The module registers `MODFIB` and `MODMUL` through the table. Then the module
returns.

### Boot re-activation

`basic_session_begin()` calls `tiku_basic_module_activate()` one time at each
boot. The code is durable. RRAM keeps the code. A reset clears only the SRAM
table. Thus a boot runs `module_init` again from the resident slot. The words
come back. There is no second install.

```
   install once ──> RRAM slot [ TMOD | code ]  (durable)
        │
   ┌────┴─── power cycle ───┐
   │ SRAM table cleared     │ RRAM slot kept
   ▼                        ▼
  boot: session_begin -> activate -> module_init(sys) -> words re-registered
```

### The build

You compile `modules/mod_demo.c` with its own command and linker script. It is
not part of the firmware compilation.

```
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -ffreestanding -nostdlib \
    -T modules/mod_demo.ld  mod_demo.c -o mod_demo.elf     (VMA = 0x1F8000)
objcopy -O binary  mod_demo.elf  mod_demo.bin              (124-byte flat blob)
objcopy -I binary -O elf32-littlearm  mod_demo.bin  mod_demo_img.o
                                       (-> _binary_mod_demo_bin_start/_end)
```

In the MVP, the firmware holds the blob as the install source. The install into
NVM is at run time. Delivery of the blob over HTTPS or the VFS is a transport
task. The transport is available.

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

The handlers are pure. They do not call `sys`. Thus the module holds no state.
This is the minimal proof.

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

### Proof and deferred work

The MVP shows these results:

- separate compilation
- jump-table call
- durable install into byte-writable NVM (gate-last)
- execute-in-place
- registration, callable from BASIC
- durability after reboot

The MVP defers these tasks:

- Stateful handlers. The handlers are pure now. A handler that calls `sys`
  needs the module `.data`. The loader must write the `.data` behind the WEN
  gate at init. This is a small addition.
- OTA delivery. The blob is the install source now. Real delivery uses HTTPS or
  the VFS. The loader reads the blob from a buffer or a file.
- Other platforms. MSP430 FRAM is a clean second target. It is byte-writable
  and executable. RP2350 flash is second-class. Flash needs an erase before a
  write. Ambiq MRAM is second-class. MRAM writes go through the bootrom. This
  difference is the durability thesis. Durability changes across NVM types.

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

To use the feature, set `TIKU_BASIC_MODULE_ENABLE=1` on a nordic BASIC build.

---

## The relation between Tier 2 and Tier 3

```
  Tier 2                         Tier 3
  ------                         ------
  register_fn/strfn/stmt         same functions, reached through a jump table
  parse/print/error/expect       same services, as a struct of pointers
  handler compiled INTO firmware handler compiled SEPARATELY, installed to NVM
  registered at boot             registered when the module activates
```

Tier 2 set the ABI surface. Tier 2 showed correct operation. Tier 3 reaches the
same surface from a separate compilation. The code installs at run time. The
code is durable in NVM. Tier 2 came first. Thus Tier 3 had less risk. The
difficult part was the stable contract. The contract was correct before Tier 3
used it.
