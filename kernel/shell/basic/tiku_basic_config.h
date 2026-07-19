/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_config.h - compile-time tunables for the Tiku BASIC
 *                       interpreter engine.
 *
 * Every macro is `#ifndef`-guarded so a build can override it
 * through -D flags or EXTRA_CFLAGS.  They control buffer sizes,
 * optional language features (string vars, DEF FN, arrays,
 * fixed-point), and which hardware bridges (GPIO / ADC / I2C / LED
 * / VFS / REBOOT) are compiled in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BASIC_CONFIG_H_
#define TIKU_BASIC_CONFIG_H_

/*---------------------------------------------------------------------------*/
/* MEMORY TIER -- drives the default limits below                            */
/*---------------------------------------------------------------------------*/

/* The interpreter's entire working set (line table, variables, control-flow
 * stacks, string heap, arrays) is allocated from ONE kernel arena, sized from
 * the limits below (see tiku_basic_arena.inl) and drawn from the AUTO memory
 * tier.  So a bigger limit just asks the arena for more -- it does NOT cost
 * static BSS (only ~50 bytes of pointers are fixed).  The only static cost that
 * scales is the FRAM/MRAM/flash save buffer (= PROGRAM_LINES * (LINE_MAX + 8)).
 *
 * The sensible default therefore depends on how much that tier can give, which
 * is a property of the board.  Three tiers:
 *
 *   BIG  - Apollo (3 MB / 384 KB SSRAM) and RP2350 (520 KB SRAM): generous.
 *   FRAM - MSP430 FR5994/FR6989 with MEMORY_MODEL=large: the arena routes to
 *          HIFRAM (256 KB FRAM), so the 8 KB SRAM is not the bound -> roomy.
 *   else - FR5969 (2 KB SRAM) and the host test harness: the original lean
 *          limits, left EXACTLY as they were.
 *
 * Every macro stays -D-overridable; these branches only choose the default. */
#if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350)
#define TIKU_BASIC_TIER_BIG  1
#elif defined(TIKU_MEMORY_MODEL_LARGE) || defined(PLATFORM_NORDIC)
/* The nRF54L15 sits between the SMALL parts and the 512 KB BIG parts: 256 KB
 * SRAM.  The BIG tier's arena-backed FETCH buffers (sized for 512 KB) overflow
 * that, but the SMALL 64-byte defaults are too tight -- a 64-hex SHA256$ digest
 * (let alone its 88-char BASE64$) and the 60-line mem-stress program don't fit.
 * The FRAM (middle) tier is exactly the right size: 128-byte string scratch,
 * 2 KB string heap, 96 program lines.  It is pure sizing (no FRAM-specific
 * behaviour), so it fits the nRF54L15's RRAM/SRAM split cleanly. */
#define TIKU_BASIC_TIER_FRAM 1
#endif

/* Apollo510 (Apollo5) has the most RAM of the BIG-class parts -- 512 KB TCM
 * plus 3 MB SSRAM -- so it gets the largest PROGRAM_LINES.  It is also
 * PLATFORM_AMBIQ, so it already inherits every BIG secondary limit above; only
 * the program size differs from Apollo4 Lite / RP2350. */
#if defined(AM_PART_APOLLO510)
#define TIKU_BASIC_TIER_HUGE 1
#endif

/*---------------------------------------------------------------------------*/
/* CORE LIMITS                                                               */
/*---------------------------------------------------------------------------*/

/* Uniform across every platform: a BASIC line -- and each stored program line
 * -- can be this long everywhere, so a command that fits on Apollo also fits on
 * RP2350/MSP430 (portable, no silent per-part truncation of e.g. an HTTPPOST$
 * with a JSON body).  RAM is scaled instead via TIKU_BASIC_PROGRAM_LINES (how
 * MANY lines fit) per tier below.  The interactive reader is line_buf[LINE_MAX +
 * 16]; keeping LINE_MAX <= 255 keeps every byte index within a uint8_t. */
#ifndef TIKU_BASIC_LINE_MAX
#define TIKU_BASIC_LINE_MAX        144
#endif

/* PROGRAM_LINES scales program CAPACITY (line count) to the arena/RAM -- the
 * knob that varies per platform now that LINE_MAX is uniform.  It sizes the
 * prog[] arena block (PROGRAM_LINES * LINE_MAX) and the SAVE buffer
 * (PROGRAM_LINES * (LINE_MAX + 8)).  The SAVE buffer is DURABLE: on Ambiq it
 * lives in the carved NVM-region tail, so it must fit TIKU_NVM_RESERVED_BYTES
 * (256 KB) -- that tail, NOT SSRAM, is what caps HUGE (a _Static_assert in
 * tiku_basic_persist.inl enforces it).  .persistent FRAM on MSP430 / .bss
 * elsewhere.  At LINE_MAX=144 that is ~152 bytes/line:
 *   HUGE (Apollo510) 1700 -> ~239 KB prog + ~252 KB save   (fits 256 KB NVM tail)
 *   BIG  (Apollo4)    1024 -> ~147 KB prog + ~156 KB save   (fits; 1.6 MB SSRAM)
 *   RP2350             512 ->  ~74 KB prog +  ~78 KB .bss   (~160 KB arena / 520 KB SRAM)
 *   FRAM (MSP430)       96 ->  ~14 KB prog +  ~15 KB FRAM   (FR5994/6989, 256 KB FRAM)
 *   else (host/small)   50 ->   ~7 KB
 * RP2350 is split out from Apollo (both TIER_BIG) because its arena is an order
 * of magnitude smaller, so it can't afford 1024 x 144.  Line numbers are
 * uint16_t, so the hard ceiling is 65533 lines. */
#ifndef TIKU_BASIC_PROGRAM_LINES
#  if defined(PLATFORM_RP2350)
#    define TIKU_BASIC_PROGRAM_LINES 512
#  elif defined(TIKU_BASIC_TIER_HUGE)
#    define TIKU_BASIC_PROGRAM_LINES 1700   /* capped by the 256 KB NVM save tail */
#  elif defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_PROGRAM_LINES 1024
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_PROGRAM_LINES 96
#  else
#    define TIKU_BASIC_PROGRAM_LINES 50
#  endif
#endif

#ifndef TIKU_BASIC_GOSUB_DEPTH       /* sp is uint8_t -> hard max 255 */
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_GOSUB_DEPTH   32
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_GOSUB_DEPTH   16
#  else
#    define TIKU_BASIC_GOSUB_DEPTH    8
#  endif
#endif

#ifndef TIKU_BASIC_FOR_DEPTH
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_FOR_DEPTH     16
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_FOR_DEPTH      8
#  else
#    define TIKU_BASIC_FOR_DEPTH      4
#  endif
#endif

#ifndef TIKU_BASIC_LOOP_DEPTH
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_LOOP_DEPTH    16
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_LOOP_DEPTH     8
#  else
#    define TIKU_BASIC_LOOP_DEPTH     4
#  endif
#endif

/*---------------------------------------------------------------------------*/
/* OPTIONAL LANGUAGE FEATURES                                                */
/*---------------------------------------------------------------------------*/

/* Direct memory access via PEEK / POKE. Defaults on for MSP430 (the
 * intended use case: poking GPIO / peripheral registers from BASIC),
 * off everywhere else. Set to 0 in builds where giving the user
 * unrestricted memory writes is not desirable. */
#ifndef TIKU_BASIC_PEEK_POKE_ENABLE
#ifdef PLATFORM_MSP430
#define TIKU_BASIC_PEEK_POKE_ENABLE 1
#else
#define TIKU_BASIC_PEEK_POKE_ENABLE 1   /* host harness exercises this too */
#endif
#endif

/* Hardware bridges. Each can be disabled individually for ultra-thin
 * BASIC builds; defaults are on so a fresh user can blink an LED in
 * three lines. */
#ifndef TIKU_BASIC_GPIO_ENABLE
#define TIKU_BASIC_GPIO_ENABLE      1
#endif
#ifndef TIKU_BASIC_ADC_ENABLE
#define TIKU_BASIC_ADC_ENABLE       1
#endif
#ifndef TIKU_BASIC_I2C_ENABLE
#define TIKU_BASIC_I2C_ENABLE       1
#endif
#ifndef TIKU_BASIC_LED_ENABLE
#define TIKU_BASIC_LED_ENABLE       1
#endif
#ifndef TIKU_BASIC_VFS_ENABLE
#define TIKU_BASIC_VFS_ENABLE       1
#endif

/*---------------------------------------------------------------------------*/
/* FULL-PROFILE FEATURE GATES                                                */
/*---------------------------------------------------------------------------*/

/* These default ON for the resource-rich Cortex-M parts (RP2350 / Apollo4 /
 * Apollo510 -- all TIER_BIG) and OFF for the FRAM/small parts (MSP430), giving
 * a "lite" BASIC on MSP430 and a "full" BASIC on the bigger controllers. Each
 * is still -D-overridable. Several also need their kit compiled in (the gate
 * encodes that dependency) so an enabled feature can never dangle at link.
 *
 *   RTC   : NOW / DATE$ / TIME$ / SETTIME wall-clock (DATE$/TIME$ also need
 *           TIKU_KIT_TIME_ENABLE for the calendar breakdown -- gated in-file).
 *   MATHX : LOG / EXP / POW / ATAN + the '^' operator (fixed-point).
 *   FILE  : APPEND / FWRITE / FREAD$ logging to /data (via the VFS).
 *   NET   : UDPSEND / MQTTPUB / HTTPGET$ -- requires the net kit.
 *   SUBS  : multi-line SUB / FUNCTION / LOCAL / CALL with call frames. */
#ifndef TIKU_BASIC_RTC_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_RTC_ENABLE    1
#  else
#    define TIKU_BASIC_RTC_ENABLE    0
#  endif
#endif
#ifndef TIKU_BASIC_MATHX_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_MATHX_ENABLE  1
#  else
#    define TIKU_BASIC_MATHX_ENABLE  0
#  endif
#endif
#ifndef TIKU_BASIC_FILE_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_FILE_ENABLE   1
#  else
#    define TIKU_BASIC_FILE_ENABLE   0
#  endif
#endif
#ifndef TIKU_BASIC_NET_ENABLE
/* Net words ride the big tier's roomy buffers by default; nRF54L runs the
 * FRAM tier (BIG's buffers overflow its 256 KB SRAM) but has the full net
 * kit + CRACEN TRNG, so it opts in too -- HTTPGET$/HTTPPOST$ results are
 * simply capped at the FRAM-tier string length (status line + head fit). */
#  if (defined(TIKU_BASIC_TIER_BIG) || defined(PLATFORM_NORDIC)) && \
      (TIKU_KIT_NET_ENABLE + 0)
#    define TIKU_BASIC_NET_ENABLE    1
#  else
#    define TIKU_BASIC_NET_ENABLE    0
#  endif
#endif
/* BLE : BLEADV / BLEOFF / BLESEND / BLEBEACON + BLEUP / BLEGET$ -- a "serial
 *       over BLE" vocabulary on the driver-agnostic facade
 *       (interfaces/bluetooth/tiku_ble_serial), plus BLEBEACON / BLESCAN$ on
 *       the broadcast/observer facade (tiku_ble_adv).  GENERAL, not
 *       chip-specific: on whenever the build has ANY BLE radio backend,
 *       which the Makefile signals via the generic capabilities --
 *       TIKU_HAS_BLE (connection-capable: EM9305 on apollo510b) and/or
 *       TIKU_HAS_BLE_ADV (broadcast: nRF54L15 on-die RADIO).  Words for an
 *       absent capability are compiled out individually inside the .inl.
 *       -D-overridable like the rest. */
#ifndef TIKU_BASIC_BLE_ENABLE
#  if (TIKU_HAS_BLE + 0) || (TIKU_HAS_BLE_ADV + 0)
#    define TIKU_BASIC_BLE_ENABLE    1
#  else
#    define TIKU_BASIC_BLE_ENABLE    0
#  endif
#endif
/* JSON$ -- extract a value by dotted path (keys + array indices) from a JSON
 * string; the agent primitive for parsing API/LLM replies. Wraps the
 * codec/json pull-parser, so it needs TIKU_KIT_CODEC_ENABLE (which compiles
 * tikukits/codec/json). BIG-only. */
#ifndef TIKU_BASIC_JSON_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG) && (TIKU_KIT_CODEC_ENABLE + 0)
#    define TIKU_BASIC_JSON_ENABLE   1
#  else
#    define TIKU_BASIC_JSON_ENABLE   0
#  endif
#endif
/* BASE64$ / SHA256$ / HMAC$ -- expose the crypto kit (base64, SHA-256,
 * HMAC-SHA256) as string builtins so programs can sign API requests
 * (Authorization headers) and hash data on-device.  Wraps
 * tikukits/crypto/{base64,sha256,hmac}; hashes return lowercase hex.
 * BIG-only.  Auto-on when the full crypto kit is compiled; the Makefile
 * otherwise forces -DTIKU_BASIC_CRYPTO_ENABLE=1 and pulls just those
 * three sources whenever BASIC is built (TIKU_BASIC_CRYPTO=0 drops it). */
#ifndef TIKU_BASIC_CRYPTO_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG) && (TIKU_KIT_CRYPTO_ENABLE + 0)
#    define TIKU_BASIC_CRYPTO_ENABLE 1
#  else
#    define TIKU_BASIC_CRYPTO_ENABLE 0
#  endif
#endif

/* ERR category codes returned by the ERR() builtin inside an ON ERROR
 * handler.  The classification is deliberately coarse: it exists so a
 * handler can branch on "is this worth retrying?" (NET) versus "is my
 * program wrong?" (RANGE/DIVZERO/TYPE).  Sites that cannot cheaply
 * classify leave the error as GENERAL.  ERL() returns the line number. */
#define TIKU_BASIC_ERR_GENERAL  1   /* uncategorised */
#define TIKU_BASIC_ERR_SYNTAX   2   /* malformed statement/expression */
#define TIKU_BASIC_ERR_TYPE     3   /* string/number type mismatch */
#define TIKU_BASIC_ERR_RANGE    4   /* array subscript / bounds */
#define TIKU_BASIC_ERR_DIVZERO  5   /* divide (or MOD) by zero */
#define TIKU_BASIC_ERR_NET      6   /* HTTP / MQTT / socket failure */
#define TIKU_BASIC_ERR_IO       7   /* VFS / file access */
#define TIKU_BASIC_ERR_NOMEM    8   /* string heap / arena exhausted */

/* MQTTWAIT$ inbound payload cap.  Received PUBLISH bodies longer than
 * this are truncated.  Kept small (commands are short) so the static
 * capture buffer costs little SRAM. */
#ifndef TIKU_BASIC_MQTT_RX_CAP
#  define TIKU_BASIC_MQTT_RX_CAP  256
#endif
#ifndef TIKU_BASIC_SUBS_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_SUBS_ENABLE   1
#  else
#    define TIKU_BASIC_SUBS_ENABLE   0
#  endif
#endif

/*---------------------------------------------------------------------------*/
/* HTTP REQUEST ASSEMBLY BUDGET (HTTPGET$ / HTTPPOST$)                        */
/*---------------------------------------------------------------------------*/

/* Single source of truth for the bounded inputs basic_https_get() concatenates
 * into its request buffer.  The caller (tiku_basic_string.inl) sizes its
 * host/path/content-type buffers from these; HTTPHEADER bounds its block to
 * TIKU_BASIC_HTTP_HDRS_MAX; and a _Static_assert in tiku_basic_https.inl proves
 * the worst-case assembled request still fits TIKU_BASIC_HTTP_REQ_MAX.  So the
 * req[] budget is enforced at compile time across all three files -- bumping any
 * cap without growing REQ_MAX breaks the build instead of overflowing req[]. */
#ifndef TIKU_BASIC_HTTP_HOST_MAX
#define TIKU_BASIC_HTTP_HOST_MAX    64
#endif
#ifndef TIKU_BASIC_HTTP_PATH_MAX
#define TIKU_BASIC_HTTP_PATH_MAX    80
#endif
#ifndef TIKU_BASIC_HTTP_CTYPE_MAX
#define TIKU_BASIC_HTTP_CTYPE_MAX   48
#endif
#ifndef TIKU_BASIC_HTTP_HDRS_MAX
#define TIKU_BASIC_HTTP_HDRS_MAX    192
#endif
#ifndef TIKU_BASIC_HTTP_REQ_MAX
#define TIKU_BASIC_HTTP_REQ_MAX     576
#endif

/*---------------------------------------------------------------------------*/
/* MULTI-LETTER VARIABLE NAMES                                               */
/*---------------------------------------------------------------------------*/

/* Beyond the 26 single-letter A..Z slots, BASIC supports a small
 * pool of named variables (e.g. NAME, COUNT, X1).  Each slot
 * stores a name up to TIKU_BASIC_NAMEDVAR_LEN-1 chars (NUL-
 * terminated) plus the value cell, allocated lazily on first use.
 * Numeric and string named vars share the same maximum count but
 * occupy independent name tables, so MYVAR and MYVAR$ can coexist. */
#ifndef TIKU_BASIC_NAMEDVAR_MAX
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_NAMEDVAR_MAX  64
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_NAMEDVAR_MAX  32
#  else
#    define TIKU_BASIC_NAMEDVAR_MAX  16
#  endif
#endif
/* Native builtin registry (tiku_basic_ext.h): boot-time registered
 * statement/function words from kernel services and tikukits.  Slots are a
 * few bytes of SRAM each; 0 compiles the whole feature out. */
#ifndef TIKU_BASIC_EXT_MAX
#define TIKU_BASIC_EXT_MAX          16      /* bundled kit uses 6; rest for
                                            * kernel services + tikukits */
#endif

/* Ship the bundled native words (GCD/ISQRT/BITCNT/HEXPR, tiku_basic_ext_kits
 * .inl) through the registry at boot.  On by default when the registry exists;
 * set 0 to keep the seam but drop the bundle (each word is a handler + a few
 * bytes of table). */
#ifndef TIKU_BASIC_EXT_KITS
#define TIKU_BASIC_EXT_KITS         1
#endif

#ifndef TIKU_BASIC_NAMEDVAR_LEN
#define TIKU_BASIC_NAMEDVAR_LEN     8       /* 7 chars + NUL */
#endif

/*---------------------------------------------------------------------------*/
/* STRING / DEF FN / ARRAY / SLOT / FIXED-POINT TUNABLES                     */
/*---------------------------------------------------------------------------*/

/* String variables A$..Z$ + string functions. The heap is reset at
 * the start of every RUN, so a single program run is bounded by
 * TIKU_BASIC_STR_HEAP_BYTES of total string allocation. There is no
 * GC -- if you exceed the heap mid-run, the program errors. The
 * single-statement scratch buffer caps any one string-expression
 * result at TIKU_BASIC_STR_BUF_CAP bytes. */
#ifndef TIKU_BASIC_STRVARS_ENABLE
#define TIKU_BASIC_STRVARS_ENABLE   1
#endif
#ifndef TIKU_BASIC_STR_HEAP_BYTES
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_STR_HEAP_BYTES 4096
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_STR_HEAP_BYTES 2048
#  else
#    define TIKU_BASIC_STR_HEAP_BYTES 512
#  endif
#endif
#ifndef TIKU_BASIC_STR_BUF_CAP
#  if defined(TIKU_BASIC_TIER_BIG)
/* 1 KB on the big-RAM parts (Apollo/RP2350): each string-expression temporary
 * is a stack buffer of this size, and the stacks here are huge, so this lets
 * STRIP$()/HTTPGET$ clear the HTTP header block and show a chunk of the body
 * without risking deep-nesting overflow. BROWSE uses its own 16 KB buffer for
 * whole pages, so this only bounds the in-BASIC-string path. */
#    define TIKU_BASIC_STR_BUF_CAP  1024
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_STR_BUF_CAP  128
#  else
#    define TIKU_BASIC_STR_BUF_CAP  64
#  endif
#endif

/* Big response buffers (referenced as #0, #1, ...): arena-backed, filled by the
 * FETCH statement, read in place by the extractors (JSON$/LINE$/BETWEEN$ with a
 * #n source, LEN(#n)). They hold a whole HTTP/LLM reply past the STR_BUF_CAP
 * scratch limit -- FETCH writes straight into the arena, bypassing the 1 KB
 * stack buffer. BIG-tier only (each buffer is real arena RAM). */
#ifndef TIKU_BASIC_BIGBUF_COUNT
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_BIGBUF_COUNT 2
#  else
#    define TIKU_BASIC_BIGBUF_COUNT 0
#  endif
#endif
#ifndef TIKU_BASIC_BIGBUF_SIZE
#  define TIKU_BASIC_BIGBUF_SIZE    8192
#endif

/* DEF FN single-line user functions. Each entry stores a name (up
 * to 7 chars), the argument-letter index, and an expression body
 * up to TIKU_BASIC_DEFN_BODY chars long. Calls evaluate the body
 * with the call-site argument bound to the named variable. */
#ifndef TIKU_BASIC_DEFN_ENABLE
#define TIKU_BASIC_DEFN_ENABLE      1
#endif
#ifndef TIKU_BASIC_DEFN_MAX
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_DEFN_MAX     16
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_DEFN_MAX      8
#  else
#    define TIKU_BASIC_DEFN_MAX      4
#  endif
#endif
#ifndef TIKU_BASIC_DEFN_BODY
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_DEFN_BODY    64
#  else
#    define TIKU_BASIC_DEFN_BODY    40
#  endif
#endif

/* DIM A(n) integer arrays. 26 slots (one per A..Z); each starts
 * unallocated. DIM bumps an arena allocation. Re-DIMming the same
 * variable in a single session is rejected. Arrays are zero-filled
 * at every RUN start to match basic_vars semantics. */
#ifndef TIKU_BASIC_ARRAYS_ENABLE
#define TIKU_BASIC_ARRAYS_ENABLE    1
#endif
/* ARRAY_MAX caps each DIM's per-dimension and total element count;
 * ARRAY_TOTAL_LONGS is the shared arena pool that backs every array's element
 * storage (must be >= one ARRAY_MAX array, more for several).  They scale
 * together.  ARRAY_TOTAL_LONGS is consumed in tiku_basic_arena.inl, which is
 * included after this header, so defining it here wins. */
#ifndef TIKU_BASIC_ARRAY_MAX
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_ARRAY_MAX    4096
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_ARRAY_MAX    1024
#  else
#    define TIKU_BASIC_ARRAY_MAX    128
#  endif
#endif
#ifndef TIKU_BASIC_ARRAY_TOTAL_LONGS
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_ARRAY_TOTAL_LONGS 4096u
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_ARRAY_TOTAL_LONGS 1024u
#  else
#    define TIKU_BASIC_ARRAY_TOTAL_LONGS 128u
#  endif
#endif

/* Multi-slot SAVE / LOAD. The default unnamed SAVE / LOAD continue
 * to use the original "prog" persist key; named slots live in
 * their own .persistent table. */
#ifndef TIKU_BASIC_NAMED_SLOTS
#define TIKU_BASIC_NAMED_SLOTS      3
#endif
/* Each named slot must hold a serialized program, so it scales with the
 * program size (these are static .persistent bytes, like the main save buf). */
#ifndef TIKU_BASIC_NAMED_SLOT_BYTES
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_NAMED_SLOT_BYTES 2048
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_NAMED_SLOT_BYTES 1024
#  else
#    define TIKU_BASIC_NAMED_SLOT_BYTES 192
#  endif
#endif

/* Fixed-point math.  Numbers with a decimal point in source (`1.5`,
 * `0.001`, ...) are scaled by TIKU_BASIC_FIXED_SCALE on parse and
 * stored as plain integers. The default scale of 1000 matches the
 * existing PI = 3142 (= 3.142 * 1000) convention; this lets FMUL,
 * FDIV, and FSTR$ all share that same Q.3 base. Operations:
 *
 *    +, -                    work directly on Q.3 integers
 *    a * pure_int            works directly
 *    a / pure_int            works directly (truncates)
 *    FMUL(a, b)              a * b / SCALE  (true fixed-point mul)
 *    FDIV(a, b)              a * SCALE / b  (true fixed-point div)
 *    FSTR$(x)                "1.500" -- stringify with the decimal
 *
 * Intermediate products use 64-bit (long long) so values up to a few
 * thousand multiply safely without overflow. */
#ifndef TIKU_BASIC_FIXED_ENABLE
#define TIKU_BASIC_FIXED_ENABLE     1
#endif
#ifndef TIKU_BASIC_FIXED_SCALE
#define TIKU_BASIC_FIXED_SCALE      1000L
#endif

/* REBOOT triggers the watchdog and spins; only meaningful on real
 * silicon. Defaults off in host builds because the spin would hang
 * the test driver.  (Was msp430-only -- the same stale single-platform
 * gate shape as the 2026-07 durability audit's bug class; found live on
 * the LM20 when the F1 resume proof dispatched REBOOT and got "? syntax".
 * All real parts have the watchdog path: the shell `reboot` command uses
 * it everywhere.) */
#ifndef TIKU_BASIC_REBOOT_ENABLE
#if defined(PLATFORM_MSP430) || defined(PLATFORM_RP2350) || \
    defined(PLATFORM_AMBIQ)  || defined(PLATFORM_NORDIC)
#define TIKU_BASIC_REBOOT_ENABLE    1
#else
#define TIKU_BASIC_REBOOT_ENABLE    0
#endif
#endif

/* Pi as a Q.3 fixed-point literal (3.142). Lets the user do
 * coarse-grained trig-style math without floats: e.g.
 * `LET CIRC = D * PI / 1000`. */
#ifndef TIKU_BASIC_PI_Q3
#define TIKU_BASIC_PI_Q3            3142
#endif

/* FRAM serialization buffer for the saved program. Sized to fit the
 * worst-case stored representation: every line at full length plus
 * a 5-digit line number, a separator, and a newline. */
#ifndef TIKU_BASIC_SAVE_BUF_BYTES
#define TIKU_BASIC_SAVE_BUF_BYTES \
    ((tiku_mem_arch_size_t)(TIKU_BASIC_PROGRAM_LINES * \
                             (TIKU_BASIC_LINE_MAX + 8u)))
#endif

/*---------------------------------------------------------------------------*/
/* INTERNAL CONSTANTS                                                        */
/*---------------------------------------------------------------------------*/

#define BASIC_CTRL_C       0x03
#define BASIC_PERSIST_KEY  "prog"

/*---------------------------------------------------------------------------*/
/* F1: POWER-FAILURE-TRANSPARENT RUN (execution-state checkpoint / resume)    */
/*---------------------------------------------------------------------------*/
/*
 * PERSIST ON checkpoints the reified interpreter state (program counter,
 * control-flow stacks, scalar + string variables, error / DATA / PRNG state)
 * into durable NVM at yield boundaries; RUN RESUME continues a program mid-loop
 * after a reset or power cut instead of restarting.  This is distinct from SAVE
 * / LOAD, which persist the *program text*: PERSIST persists the *running
 * machine*.  See tiku_basic_ckpt.inl for the slot formats and torn-write
 * discipline.
 *
 * v1 checkpoints the core scalar machine.  Arrays (DIM), big response buffers,
 * DEF FN, and the EVERY / ON CHANGE timer tables are NOT yet in the checkpoint
 * -- PERSIST ON warns if any are live so the boundary is never silent.
 *
 * Durability by substrate (same envelope as SAVE, see BASIC_NVM_ON_REGION):
 *   MSP430          .persistent FRAM      -- durable, per-batch checkpoints
 *   Ambiq / RP2350  carved NVM region     -- durable, interval-gated (below)
 *   Nordic / host   .bss                  -- session-only (the nRF54 durable
 *                   .persistent RRAM reserve is 8 KB; BASIC's buffers do not
 *                   fit -- enlarging it relocates the TFS/persist layout, a
 *                   deliberate port change deferred to its own pass) */
#ifndef TIKU_BASIC_PERSIST_RUN_ENABLE
#define TIKU_BASIC_PERSIST_RUN_ENABLE 1
#endif

/* Minimum seconds between run-state checkpoints, counted from PERSIST ON.
 * 0 = checkpoint every yield batch, the finest resume granularity -- right
 * where NVM writes are cheap byte stores with effectively unlimited endurance
 * (MSP430 FRAM ~1e15).  Nonzero paces the program-op substrates:
 *   RP2350 QSPI flash : each checkpoint read-modify-ERASES its 4 KB sectors
 *                       (~1e5 cycles); 60 s ~= 69 days of continuous armed
 *                       running before the rated limit.
 *   Ambiq MRAM        : bootrom word-program, no erase, but each save is a
 *                       masked-IRQ bootrom call -- 5 s keeps that jitter rare.
 *   Nordic RRAM       : byte-writable, no erase, but ~1e5-class write
 *                       endurance -- 5 s paces the wear like Ambiq.
 * Coarser interval = longer replay window after a power cut (the program
 * re-runs at most the last interval's worth of lines). */
#ifndef TIKU_BASIC_CKPT_INTERVAL_S
#  if defined(PLATFORM_RP2350)
#    define TIKU_BASIC_CKPT_INTERVAL_S 60
#  elif defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC)
#    define TIKU_BASIC_CKPT_INTERVAL_S 5
#  else
#    define TIKU_BASIC_CKPT_INTERVAL_S 0
#  endif
#endif

/* Where BASIC's durable slots (saved program + F1 run-state checkpoint) live.
 * Two backings, ONE decision point, keyed on the REGION LAYOUT rather than a
 * platform list so a new port with a reserved tail lights up automatically:
 *
 *   BASIC_NVM_ON_REGION = 1 -- the carved NVM region's reserved tail
 *     (TIKU_NVM_RESERVED_BYTES > 0: Ambiq MRAM, RP2350 QSPI flash, Nordic
 *     RRAM).  The slots are fixed offsets in the tail, written via
 *     tiku_tier_nvm_write (bootrom / erase+program / memcpy-behind-WEN);
 *     the _Static_assert in tiku_basic_ckpt.inl proves both slots fit.
 *
 *   BASIC_NVM_ON_REGION = 0 -- byte-writable buffers.  On MSP430 they carry
 *     TIKU_DURABLE (.persistent FRAM).  On host they land in plain .bss
 *     (volatile test harness). */
#include <kernel/memory/tiku_nvm_region.h>
#if TIKU_NVM_RESERVED_BYTES > 0
#define BASIC_NVM_ON_REGION  1
#else
#define BASIC_NVM_ON_REGION  0
#endif

#ifdef PLATFORM_MSP430
#define BASIC_NVM_PERSISTENT TIKU_DURABLE   /* FRAM in place (tiku_mem.h) */
#elif defined(PLATFORM_AMBIQ)
/* On the region-backed parts the persist-store buffers are not built (the
 * durable slots live in the carved region).  This attribute is unused there,
 * kept defined for symmetry. */
#define BASIC_NVM_PERSISTENT __attribute__((section(".ssram")))
#else
#define BASIC_NVM_PERSISTENT
#endif

/* Transient SAVE/LOAD serialization scratch (a worst-case whole-program image:
 * ~90 KB at 1024 lines, ~176 KB at 2048). The two buffers are never live at the
 * same time, but together they would dominate the scarce DTCM (384-512 KB), so
 * on Ambiq they ride the multi-MB .ssram pool (zeroed at boot, like .bss); the
 * durable copy lives in the NVM region. Elsewhere the scratch stays in .bss. */
#if defined(PLATFORM_AMBIQ)
#define BASIC_SCRATCH __attribute__((section(".ssram")))
#else
#define BASIC_SCRATCH
#endif

#endif /* TIKU_BASIC_CONFIG_H_ */
