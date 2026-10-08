/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_config.h - compile-time tunables for the BASIC engine.
 *
 * Each tunable is #ifndef-guarded so a build can override it.  They set buffer
 * sizes, optional language features and which hardware bridges are compiled
 * in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BASIC_CONFIG_H_
#define TIKU_BASIC_CONFIG_H_

/** Line debugger (the DEBUG command): fixed-size state; it evaluates no
 *  expression and writes no interpreter state. */
#ifndef TIKU_BASIC_DEBUG_ENABLE
#define TIKU_BASIC_DEBUG_ENABLE 1
#endif

/*---------------------------------------------------------------------------*/
/* MEMORY TIER                                                               */
/*---------------------------------------------------------------------------*/

/* The interpreter's working set (line table, variables, control-flow stacks,
 * EVERY and ON CHANGE tables, string heap, DEF FN, arrays, big buffers) comes
 * from one kernel arena, sized from the limits below (tiku_basic_arena.inl)
 * and drawn from the AUTO memory tier; raising a limit grows the arena
 * request.  On region parts SAVE streams through a 4 KB chunk and LOAD parses
 * the program in place, so no static buffer scales with PROGRAM_LINES; the
 * saved file does, and on MSP430/host so does the staging buffer.
 *
 * The defaults follow what the tier can give, which is a property of the
 * board:
 *
 *   BIG  - Ambiq, RP2350, Nordic, STM32N6, RA8P1 and ESP32-C61: generous.
 *   FRAM - MSP430 with MEMORY_MODEL=large: the arena draws on the HIFRAM tier
 *          pool (TIKU_TIER_HIFRAM_SIZE), so SRAM is not the bound.
 *   else - small-model MSP430 and the host harness: the lean limits.
 *
 * Every macro is -D-overridable; these branches choose only the default. */

/**
 * Name of the durable memory in user-facing text (HELP prints "SAVE / LOAD
 * persist across reboots in <label>").  A device header defines it as the
 * part's technology (FRAM, MRAM, Flash, ...); "NVM" is the fallback.
 *
 * @note kernel/memory/tiku_nvm_map.h carries the same fallback, which a host
 *       build does not reach: the host harness stubs tiku_shell.h.
 */
#ifndef TIKU_DEVICE_NVM_LABEL
#define TIKU_DEVICE_NVM_LABEL     "NVM"
#endif

#if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350) || \
    defined(PLATFORM_NORDIC) || defined(PLATFORM_STM32N6) || \
    defined(PLATFORM_RA8P1) || defined(PLATFORM_ESP32C61)
/* On Nordic the LM20's arena has its own 256 KB SRAM bank (RAM2); the L15
 * shares its SRAM with TLS and the radio and takes a smaller PROGRAM_LINES
 * below. */
#define TIKU_BASIC_TIER_BIG  1         /**< generous defaults */
#elif defined(TIKU_MEMORY_MODEL_LARGE)
#define TIKU_BASIC_TIER_FRAM 1         /**< MSP430 large model: HIFRAM arena */
#endif

/* The ESP32-C61 runs its image from its 320 KB SRAM, which leaves the SRAM
 * tier no room for a BIG arena: BASIC attaches the in-package PSRAM, and its
 * arena request admits external memory (TIKU_MEM_ALLOW_EXTERNAL). */
#if defined(PLATFORM_ESP32C61)
/** The arena may come from external memory. */
#define TIKU_BASIC_ARENA_EXTERNAL     1
/** Smallest PSRAM the part ships with: the arena's build-time bound. */
#define TIKU_BASIC_ARENA_EXTERNAL_MIN (2UL * 1024UL * 1024UL)
#endif

/* Apollo510 has 512 KB TCM plus 3 MB SSRAM, so it gets a larger
 * PROGRAM_LINES; as PLATFORM_AMBIQ it inherits every other BIG limit. */
#if defined(AM_PART_APOLLO510)
#define TIKU_BASIC_TIER_HUGE 1         /**< BIG, with more program lines */
#endif

/*---------------------------------------------------------------------------*/
/* CORE LIMITS                                                               */
/*---------------------------------------------------------------------------*/

/**
 * Longest BASIC line, typed or stored.  It is the same on every platform, so a
 * line that fits on one part fits on all; RAM scales with
 * TIKU_BASIC_PROGRAM_LINES.  The interactive reader is LINE_MAX + 16 bytes.
 *
 * @note LINE_MAX must stay at most 255: the SUB and label registries store a
 *       name's offset within its line as a uint8_t.
 */
#ifndef TIKU_BASIC_LINE_MAX
#define TIKU_BASIC_LINE_MAX        144
#endif

/**
 * Program capacity in lines, the knob that varies per platform.  Each line
 * costs LINE_MAX + 4 bytes of arena (text, number, index) and up to
 * LINE_MAX + 8 bytes saved (TIKU_BASIC_SAVE_BUF_BYTES).
 *
 * @note On region parts the saved program is prog.bas in /data, bounded by the
 *       store's capacity: the _Static_assert in tiku_basic_ckpt.inl checks
 *       prog.bas and prog.ckpt against the store, and the one in
 *       tiku_basic_arena.inl checks the arena against its tier.  Line numbers
 *       are uint16_t, so the hard ceiling is 65533 lines.
 */
#ifndef TIKU_BASIC_PROGRAM_LINES
#  if defined(PLATFORM_RP2350)
     /* RP2350's SRAM tier is far smaller than Ambiq's. */
#    define TIKU_BASIC_PROGRAM_LINES 512
#  elif defined(TIKU_DEVICE_NRF54L15)
     /* The L15 shares its SRAM with TLS and the radio: a 256-line arena fits
      * every L15 tier profile. */
#    define TIKU_BASIC_PROGRAM_LINES 256
#  elif defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
     /* The LM20's arena lives in its own SRAM bank (RAM2), and nothing static
      * scales with PROGRAM_LINES, so RAM2 is the bound: each line takes 148
      * arena bytes on top of the fixed working set. */
#    define TIKU_BASIC_PROGRAM_LINES 1400
#  elif defined(PLATFORM_RA8P1)
     /* The /data store bounds RA8P1: prog.bas and prog.ckpt both come out of
      * it, and 512 lines keep one program to a small share of it. */
#    define TIKU_BASIC_PROGRAM_LINES 512
#  elif defined(TIKU_BASIC_TIER_HUGE)
#    define TIKU_BASIC_PROGRAM_LINES 1700
#  elif defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_PROGRAM_LINES 1024
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_PROGRAM_LINES 96
#  else
#    define TIKU_BASIC_PROGRAM_LINES 50
#  endif
#endif

/** GOSUB nesting depth (gosub_sp is a uint8_t, so at most 255). */
#ifndef TIKU_BASIC_GOSUB_DEPTH
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_GOSUB_DEPTH   32
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_GOSUB_DEPTH   16
#  else
#    define TIKU_BASIC_GOSUB_DEPTH    8
#  endif
#endif

/** FOR nesting depth (at most 255). */
#ifndef TIKU_BASIC_FOR_DEPTH
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_FOR_DEPTH     16
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_FOR_DEPTH      8
#  else
#    define TIKU_BASIC_FOR_DEPTH      4
#  endif
#endif

/** WHILE / REPEAT nesting depth (at most 255). */
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

/** PEEK / POKE direct memory access: on by default for MSP430 (peripheral
 *  registers from BASIC) and the host harness (a simulated map), off on
 *  other targets.  Set 0 where unrestricted memory writes are not wanted. */
#ifndef TIKU_BASIC_PEEK_POKE_ENABLE
#if defined(PLATFORM_MSP430) || defined(TIKU_TEST_HOST)
#define TIKU_BASIC_PEEK_POKE_ENABLE 1
#else
#define TIKU_BASIC_PEEK_POKE_ENABLE 0
#endif
#endif

/* Hardware bridges, each on by default; 0 leaves that bridge's words out. */
#ifndef TIKU_BASIC_GPIO_ENABLE
#define TIKU_BASIC_GPIO_ENABLE      1   /**< GPIO words */
#endif
#ifndef TIKU_BASIC_ADC_ENABLE
#define TIKU_BASIC_ADC_ENABLE       1   /**< ADC words */
#endif
#ifndef TIKU_BASIC_I2C_ENABLE
#define TIKU_BASIC_I2C_ENABLE       1   /**< I2C words */
#endif
#ifndef TIKU_BASIC_LED_ENABLE
#define TIKU_BASIC_LED_ENABLE       1   /**< LED words */
#endif
#ifndef TIKU_BASIC_VFS_ENABLE
#define TIKU_BASIC_VFS_ENABLE       1   /**< VFS read/write words */
#endif

/*---------------------------------------------------------------------------*/
/* FULL-PROFILE FEATURE GATES                                                */
/*---------------------------------------------------------------------------*/

/* These default on for the TIER_BIG parts and off for MSP430 and the host;
 * each is -D-overridable.  A gate whose words need a kit also tests that
 * kit's enable macro.
 *
 *   RTC   : NOW / DATE$ / TIME$ / SETTIME wall-clock (DATE$/TIME$ also need
 *           TIKU_KIT_TIME_ENABLE for the calendar breakdown, gated in-file).
 *   MATHX : LOG / EXP / POW / ATAN in Q.3 fixed point.
 *   FILE  : APPEND / FWRITE / FREAD$ logging to /data (via the VFS).
 *   NET   : UDPSEND / MQTTPUB / HTTPGET$; requires the net kit.
 *   SUBS  : multi-line SUB / FUNCTION / LOCAL / CALL with call frames. */
/** Wall-clock words (see the list above). */
#ifndef TIKU_BASIC_RTC_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_RTC_ENABLE    1
#  else
#    define TIKU_BASIC_RTC_ENABLE    0
#  endif
#endif
/** Extended fixed-point math words. */
#ifndef TIKU_BASIC_MATHX_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_MATHX_ENABLE  1
#  else
#    define TIKU_BASIC_MATHX_ENABLE  0
#  endif
#endif
/** /data logging words. */
#ifndef TIKU_BASIC_FILE_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_FILE_ENABLE   1
#  else
#    define TIKU_BASIC_FILE_ENABLE   0
#  endif
#endif
/** Net words: on by default for TIER_BIG parts (Nordic included) that compile
 *  the net kit. */
#ifndef TIKU_BASIC_NET_ENABLE
#  if (defined(TIKU_BASIC_TIER_BIG) || defined(PLATFORM_NORDIC)) && \
      (TIKU_KIT_NET_ENABLE + 0)
#    define TIKU_BASIC_NET_ENABLE    1
#  else
#    define TIKU_BASIC_NET_ENABLE    0
#  endif
#endif
/**
 * BLE words: BLEADV / BLEOFF / BLESEND / BLEUP / BLEGET$, a serial-over-BLE
 * vocabulary on the tiku_ble_serial facade, and BLEBEACON / BLESCAN$ on the
 * broadcast facade (tiku_ble_adv).
 *
 * @note On by default when the Makefile defines TIKU_HAS_BLE (a
 *       connection-capable backend, the EM9305 on apollo510b) or
 *       TIKU_HAS_BLE_ADV (broadcast, the Nordic on-die RADIO); the words of an
 *       absent capability compile out.
 */
#ifndef TIKU_BASIC_BLE_ENABLE
#  if (TIKU_HAS_BLE + 0) || (TIKU_HAS_BLE_ADV + 0)
#    define TIKU_BASIC_BLE_ENABLE    1
#  else
#    define TIKU_BASIC_BLE_ENABLE    0
#  endif
#endif
/** JSON$: the value at a dotted path (keys and array indices) in a JSON
 *  string, read with the codec/json pull-parser.  On by default for TIER_BIG
 *  parts that build TIKU_KIT_CODEC_ENABLE. */
#ifndef TIKU_BASIC_JSON_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG) && (TIKU_KIT_CODEC_ENABLE + 0)
#    define TIKU_BASIC_JSON_ENABLE   1
#  else
#    define TIKU_BASIC_JSON_ENABLE   0
#  endif
#endif
/**
 * BASE64$ / SHA256$ / HMAC$: the crypto kit's base64, SHA-256 and HMAC-SHA256
 * as string builtins; the hashes return lowercase hex.
 *
 * @note The Makefile sets it, and compiles those three kit sources, in every
 *       BASIC build unless TIKU_BASIC_CRYPTO=0; the default below applies
 *       when it does not.
 */
#ifndef TIKU_BASIC_CRYPTO_ENABLE
#  if defined(TIKU_BASIC_TIER_BIG) && (TIKU_KIT_CRYPTO_ENABLE + 0)
#    define TIKU_BASIC_CRYPTO_ENABLE 1
#  else
#    define TIKU_BASIC_CRYPTO_ENABLE 0
#  endif
#endif

/* Error categories, returned by ERR() inside an ON ERROR handler; ERL()
 * returns the line.  NET marks a failure a handler may retry; SYNTAX, TYPE,
 * RANGE and DIVZERO mark an error in the program.  An error raised without a
 * category reads as GENERAL. */
#define TIKU_BASIC_ERR_GENERAL  1   /**< uncategorised */
#define TIKU_BASIC_ERR_SYNTAX   2   /**< malformed statement/expression */
#define TIKU_BASIC_ERR_TYPE     3   /**< string/number type mismatch */
#define TIKU_BASIC_ERR_RANGE    4   /**< array subscript / bounds */
#define TIKU_BASIC_ERR_DIVZERO  5   /**< divide (or MOD) by zero */
#define TIKU_BASIC_ERR_NET      6   /**< HTTP / MQTT / socket failure */
#define TIKU_BASIC_ERR_IO       7   /**< VFS / file access */
#define TIKU_BASIC_ERR_NOMEM    8   /**< string heap / arena exhausted */

/** MQTTWAIT$ payload capacity, in bytes, of a static capture buffer; a
 *  longer PUBLISH body is truncated. */
#ifndef TIKU_BASIC_MQTT_RX_CAP
#  define TIKU_BASIC_MQTT_RX_CAP  256
#endif
/** Multi-line SUB / FUNCTION / LOCAL / CALL (see the list above). */
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

/* Caps on the inputs basic_https_get() concatenates into its request buffer.
 * Callers size their host, path and content-type buffers from them, and
 * HTTPHEADER bounds its block to TIKU_BASIC_HTTP_HDRS_MAX.  A _Static_assert
 * in tiku_basic_https.inl fails the build when the worst-case request exceeds
 * TIKU_BASIC_HTTP_REQ_MAX. */
#ifndef TIKU_BASIC_HTTP_HOST_MAX
#define TIKU_BASIC_HTTP_HOST_MAX    64    /**< host name, incl. NUL */
#endif
#ifndef TIKU_BASIC_HTTP_PATH_MAX
#define TIKU_BASIC_HTTP_PATH_MAX    80    /**< request path, incl. NUL */
#endif
#ifndef TIKU_BASIC_HTTP_CTYPE_MAX
#define TIKU_BASIC_HTTP_CTYPE_MAX   48    /**< POST content type, incl. NUL */
#endif
#ifndef TIKU_BASIC_HTTP_HDRS_MAX
#define TIKU_BASIC_HTTP_HDRS_MAX    192   /**< HTTPHEADER block */
#endif
#ifndef TIKU_BASIC_HTTP_REQ_MAX
#define TIKU_BASIC_HTTP_REQ_MAX     576   /**< assembled request */
#endif

/*---------------------------------------------------------------------------*/
/* MULTI-LETTER VARIABLE NAMES                                               */
/*---------------------------------------------------------------------------*/

/**
 * Named variables (e.g. NAME, COUNT, X1) beyond the 26 single-letter slots.
 * Each slot holds a name of up to TIKU_BASIC_NAMEDVAR_LEN - 1 chars and a
 * value, taken on first use.
 *
 * @note Numeric and string named variables each have a table of this many
 *       slots, so MYVAR and MYVAR$ coexist.
 */
#ifndef TIKU_BASIC_NAMEDVAR_MAX
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_NAMEDVAR_MAX  64
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_NAMEDVAR_MAX  32
#  else
#    define TIKU_BASIC_NAMEDVAR_MAX  16
#  endif
#endif
/** Slots in the native builtin registry (tiku_basic_ext.h), one small SRAM
 *  entry each and shared by every registrant; the bundled words take 6.  0
 *  compiles the registry out. */
#ifndef TIKU_BASIC_EXT_MAX
#define TIKU_BASIC_EXT_MAX          16
#endif

/**
 * The runtime-loadable native-module loader (tiku_basic_module.c).  Opt-in
 * with TIKU_BASIC_MODULE_ENABLE=1 on the make line, which also builds the
 * module image.
 *
 * @note The Makefile supports it only on parts with a module window (Nordic,
 *       RP2350, Apollo4, Apollo510, MSP430 FR5994/FR6989, ESP32-C61) and stops
 *       with an error elsewhere.
 */
#ifndef TIKU_BASIC_MODULE_ENABLE
#define TIKU_BASIC_MODULE_ENABLE    0
#endif

/** Register the bundled native words (GCD, ISQRT, BITCNT, HEXPR, REV$,
 *  ROMAN$; tiku_basic_ext_kits.inl) at the first BASIC session.  Set 0 to
 *  keep the registry but drop the bundle. */
#ifndef TIKU_BASIC_EXT_KITS
#define TIKU_BASIC_EXT_KITS         1
#endif

#ifndef TIKU_BASIC_NAMEDVAR_LEN
#define TIKU_BASIC_NAMEDVAR_LEN     8       /**< 7 chars + NUL */
#endif

/*---------------------------------------------------------------------------*/
/* STRING / DEF FN / ARRAY / SLOT / FIXED-POINT TUNABLES                     */
/*---------------------------------------------------------------------------*/

/**
 * String variables A$..Z$ and the string functions.  Strings live in a heap
 * of TIKU_BASIC_STR_HEAP_BYTES, reset at every RUN; a full heap is compacted
 * once, and the program errors only when the live strings fill it.
 *
 * @note TIKU_BASIC_STR_BUF_CAP caps any one string-expression result.
 */
#ifndef TIKU_BASIC_STRVARS_ENABLE
#define TIKU_BASIC_STRVARS_ENABLE   1
#endif
/** String heap size, in bytes. */
#ifndef TIKU_BASIC_STR_HEAP_BYTES
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_STR_HEAP_BYTES 4096
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_STR_HEAP_BYTES 2048
#  else
#    define TIKU_BASIC_STR_HEAP_BYTES 512
#  endif
#endif
/** Largest string-expression result; each temporary is a stack buffer of
 *  this size. */
#ifndef TIKU_BASIC_STR_BUF_CAP
#  if defined(TIKU_BASIC_TIER_BIG)
/* 1 KB on TIER_BIG parts, whose stacks are large: STRIP$(HTTPGET$(...)) then
 * holds the HTTP header block and part of the body.  BROWSE pages through its
 * own buffer, TIKU_BASIC_BROWSE_BUF. */
#    define TIKU_BASIC_STR_BUF_CAP  1024
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_STR_BUF_CAP  128
#  else
#    define TIKU_BASIC_STR_BUF_CAP  64
#  endif
#endif

/**
 * Big response buffers #0, #1, ...: arena-backed, filled by FETCH and read in
 * place by JSON$, LINE$, BETWEEN$ with a #n source and LEN(#n); one holds a
 * reply longer than TIKU_BASIC_STR_BUF_CAP.
 *
 * @note On by default only for TIER_BIG parts: each buffer takes
 *       TIKU_BASIC_BIGBUF_SIZE bytes of arena.
 */
#ifndef TIKU_BASIC_BIGBUF_COUNT
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_BIGBUF_COUNT 2
#  else
#    define TIKU_BASIC_BIGBUF_COUNT 0
#  endif
#endif
#ifndef TIKU_BASIC_BIGBUF_SIZE
#  define TIKU_BASIC_BIGBUF_SIZE    8192    /**< bytes per big buffer */
#endif

/**
 * DEF FN single-line user functions: a name of up to 7 chars, up to
 * TIKU_BASIC_DEFN_ARGS argument letters and a body under TIKU_BASIC_DEFN_BODY
 * chars; a call binds each argument to its letter for the body.
 */
#ifndef TIKU_BASIC_DEFN_ENABLE
#define TIKU_BASIC_DEFN_ENABLE      1
#endif
/** DEF FN table entries. */
#ifndef TIKU_BASIC_DEFN_MAX
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_DEFN_MAX     16
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_DEFN_MAX      8
#  else
#    define TIKU_BASIC_DEFN_MAX      4
#  endif
#endif
/** DEF FN body buffer, in chars including the NUL. */
#ifndef TIKU_BASIC_DEFN_BODY
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_DEFN_BODY    64
#  else
#    define TIKU_BASIC_DEFN_BODY    40
#  endif
#endif

/**
 * DIM A(n[,m]) and A$(n[,m]): 26 numeric and 26 string array slots whose
 * elements DIM takes from the arena, zero-filled.  RUN, NEW and LOAD release
 * every array; a second DIM of one before that is rejected.
 */
#ifndef TIKU_BASIC_ARRAYS_ENABLE
#define TIKU_BASIC_ARRAYS_ENABLE    1
#endif
/* ARRAY_MAX caps each DIM's per-dimension and total element count;
 * ARRAY_TOTAL_LONGS is the arena pool behind every array's elements, sized
 * for at least one ARRAY_MAX array. */
/** Most elements per dimension, and in total, for one DIM. */
#ifndef TIKU_BASIC_ARRAY_MAX
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_ARRAY_MAX    4096
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_ARRAY_MAX    1024
#  else
#    define TIKU_BASIC_ARRAY_MAX    128
#  endif
#endif
/** Arena elements shared by every DIMmed array. */
#ifndef TIKU_BASIC_ARRAY_TOTAL_LONGS
#  if defined(TIKU_BASIC_TIER_BIG)
#    define TIKU_BASIC_ARRAY_TOTAL_LONGS 4096u
#  elif defined(TIKU_BASIC_TIER_FRAM)
#    define TIKU_BASIC_ARRAY_TOTAL_LONGS 1024u
#  else
#    define TIKU_BASIC_ARRAY_TOTAL_LONGS 128u
#  endif
#endif

/**
 * Named SAVE / LOAD slots (tiku_basic_named_slots.inl); 0 disables named
 * saves.  The unnamed SAVE / LOAD use prog.bas in /data, or the "prog" persist
 * key on MSP430/host.
 *
 * @note On region parts named programs are /data/<name>.bas files, one file
 *       each and as many as /data has room for, so the count and the slot
 *       size apply only to the static table on MSP430/host.
 */
#ifndef TIKU_BASIC_NAMED_SLOTS
#define TIKU_BASIC_NAMED_SLOTS      3
#endif
/** Bytes per named slot (MSP430/host): each holds a whole serialized program
 *  in static durable bytes, so it does not scale with
 *  TIKU_BASIC_PROGRAM_LINES and a larger program is refused. */
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
 * stored as plain integers.  The default scale of 1000 matches
 * PI = 3142 (3.142 * 1000), so FMUL, FDIV and FSTR$ share one Q.3
 * base.  Operations:
 *
 *    +, -                    work directly on Q.3 integers
 *    a * pure_int            works directly
 *    a / pure_int            works directly (truncates)
 *    FMUL(a, b)              a * b / SCALE  (fixed-point multiply)
 *    FDIV(a, b)              a * SCALE / b  (fixed-point divide)
 *    FSTR$(x)                "1.500" -- stringify with the decimal
 *
 * FMUL and FDIV form their products in 64-bit (long long) and cast the
 * quotient back to long. */
#ifndef TIKU_BASIC_FIXED_ENABLE
#define TIKU_BASIC_FIXED_ENABLE     1       /**< decimals and the F words */
#endif
#ifndef TIKU_BASIC_FIXED_SCALE
#define TIKU_BASIC_FIXED_SCALE      1000L   /**< Q.3: three decimal places */
#endif

/** REBOOT: trigger the watchdog and spin.  On for every target, which all
 *  have the watchdog path the shell `reboot` uses; off for host builds,
 *  where the spin would hang the test driver. */
#ifndef TIKU_BASIC_REBOOT_ENABLE
#if defined(TIKU_TEST_HOST)
#define TIKU_BASIC_REBOOT_ENABLE    0
#else
#define TIKU_BASIC_REBOOT_ENABLE    1
#endif
#endif

/** Pi as a Q.3 fixed-point literal (3.142), for coarse trig-style math
 *  without floats, e.g. `LET CIRC = D * PI / 1000`. */
#ifndef TIKU_BASIC_PI_Q3
#define TIKU_BASIC_PI_Q3            3142
#endif

/** Largest serialized program: every line at full length plus a 5-digit
 *  line number, a separator and a newline.  It bounds prog.bas, and sizes the
 *  staging buffer on MSP430/host. */
#ifndef TIKU_BASIC_SAVE_BUF_BYTES
#define TIKU_BASIC_SAVE_BUF_BYTES \
    ((tiku_mem_arch_size_t)(TIKU_BASIC_PROGRAM_LINES * \
                             (TIKU_BASIC_LINE_MAX + 8u)))
#endif

/*---------------------------------------------------------------------------*/
/* INTERNAL CONSTANTS                                                        */
/*---------------------------------------------------------------------------*/

#define BASIC_CTRL_C       0x03       /**< break key */
#define BASIC_PERSIST_KEY  "prog"     /**< persist-store key, MSP430/host */

/*---------------------------------------------------------------------------*/
/* POWER-FAILURE-TRANSPARENT RUN                                             */
/*---------------------------------------------------------------------------*/
/*
 * PERSIST ON checkpoints the interpreter's running state (program counter,
 * stacks, variables, arrays, DEF FN, SUB frames, EVERY and ON CHANGE
 * registrations, error, DATA and PRNG state) at yield boundaries; RUN RESUME
 * continues a program mid-loop after a reset or power cut.  SAVE / LOAD keep
 * the program text; PERSIST keeps the running machine.  Big response buffers
 * are not checkpointed.  Formats and torn-write rules: tiku_basic_ckpt.inl.
 *
 * Storage follows BASIC_NVM_ON_REGION, as SAVE does:
 *   region parts   /data file prog.ckpt   durable, paced per medium (below)
 *   MSP430         FRAM buffer            durable, every yield batch
 *   host           .bss buffer            session-only
 */
/** PERSIST and RUN RESUME (tiku_basic_ckpt.inl). */
#ifndef TIKU_BASIC_PERSIST_RUN_ENABLE
#define TIKU_BASIC_PERSIST_RUN_ENABLE 1
#endif

/* Checkpoint pacing.  0 checkpoints every yield batch, the finest resume
 * granularity, for media whose writes are byte stores with practically
 * unlimited endurance (MSP430 FRAM, rated ~1e15 cycles) and for the host.  A
 * nonzero interval paces media that wear or stall:
 *   Ambiq MRAM         no erase, but each save is a masked-IRQ bootrom
 *                      call: 5 s
 *   Nordic RRAM        no erase, but limited write endurance: 5 s
 *   RA8P1 MRAM         rated 1e5 programs per 32 bytes, like the RRAM: 5 s
 *   other ports        sector-erased flash (RP2350, ESP32-C61, STM32N6),
 *                      rated ~1e5 cycles per sector: 60 s
 * A longer interval lengthens the replay after a power cut: the program
 * re-runs at most the last interval's worth of lines. */
/** Minimum seconds between run-state checkpoints while PERSIST is on; 0
 *  checkpoints every yield batch. */
#ifndef TIKU_BASIC_CKPT_INTERVAL_S
#  if defined(PLATFORM_MSP430) || defined(TIKU_TEST_HOST)
#    define TIKU_BASIC_CKPT_INTERVAL_S 0
#  elif defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC) || \
        defined(PLATFORM_RA8P1)
#    define TIKU_BASIC_CKPT_INTERVAL_S 5
#  else
#    define TIKU_BASIC_CKPT_INTERVAL_S 60
#  endif
#endif

/* Where BASIC's saved program and run-state checkpoint live, keyed on the
 * region layout (TIKU_NVM_HAS_REGION), so a new port with a carved region
 * takes the /data backing without being named here:
 *
 *   BASIC_NVM_ON_REGION = 1 -- files in the /data store on the carved NVM
 *     region (TIKU_NVM_HAS_REGION: every target but MSP430 and host): the
 *     saved program is prog.bas and the checkpoint prog.ckpt.
 *
 *   BASIC_NVM_ON_REGION = 0 -- byte-writable buffers: TIKU_DURABLE FRAM on
 *     MSP430, plain .bss on host (a volatile test harness). */
#include <kernel/memory/tiku_nvm_region.h>
#ifndef TIKU_NVM_HAS_REGION
#error "tiku_nvm_region.h did not define TIKU_NVM_HAS_REGION -- a missing \
include here would silently select the non-region storage backend"
#endif
#if TIKU_NVM_HAS_REGION
#define BASIC_NVM_ON_REGION  1   /**< durable objects are /data files */
#else
#define BASIC_NVM_ON_REGION  0   /**< durable objects are static buffers */
#endif

/** Placement of the byte-writable durable buffers, which only MSP430 (FRAM)
 *  and the host (plain .bss, the empty definition) declare.  Region parts keep
 *  their durable objects in /data and leave the Ambiq definition unused. */
#ifdef PLATFORM_MSP430
#define BASIC_NVM_PERSISTENT TIKU_DURABLE   /* FRAM in place (tiku_mem.h) */
#elif defined(PLATFORM_AMBIQ)
#define BASIC_NVM_PERSISTENT __attribute__((section(".ssram")))
#else
#define BASIC_NVM_PERSISTENT
#endif

/** Placement of the transient SAVE/LOAD scratch: the multi-MB .ssram pool on
 *  Ambiq (zeroed at boot, like .bss), keeping it out of the DTCM; plain .bss
 *  elsewhere. */
#if defined(PLATFORM_AMBIQ)
#define BASIC_SCRATCH __attribute__((section(".ssram")))
#else
#define BASIC_SCRATCH
#endif

#endif /* TIKU_BASIC_CONFIG_H_ */
