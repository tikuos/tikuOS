/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_ext_kits.inl - bundled native BASIC extensions (Tier 2 of
 * kintsugi/loadable.md).
 *
 * The FIRST client of the native builtin registry (tiku_basic_ext.h): a
 * handful of genuinely-useful native words that are NOT interpreter builtins,
 * registered at boot through the SAME public API any kernel service or tikukit
 * uses -- proving the ABI seam end to end.  Nothing here touches the parser's
 * dispatch chain or the crunch/token machinery; adding a word is a handler +
 * one register call, never an edit to the interpreter's #if jungle.
 *
 * External extensions register the same way from their own init (called by the
 * app at boot) and need not touch this file at all; this bundle is the proof
 * of concept, and the default words a full BASIC build ships with.
 *
 * NOT a standalone translation unit -- included from tiku_basic.c after
 * tiku_basic_ext.inl.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if (TIKU_BASIC_EXT_MAX > 0) && TIKU_BASIC_EXT_KITS

/* GCD(a, b): greatest common divisor of |a| and |b|.  GCD(0,0)=0. */
static int
bext_gcd(const long *args, int argc, long *out)
{
    long a = args[0], b = args[1];
    (void)argc;
    if (a < 0) { a = -a; }
    if (b < 0) { b = -b; }
    while (b != 0) {
        long t = a % b;
        a = b;
        b = t;
    }
    *out = a;
    return 0;
}

/* ISQRT(n): floor(sqrt(n)) for n >= 0 (integer, no FPU). */
static int
bext_isqrt(const long *args, int argc, long *out)
{
    unsigned long n, x, bit;
    (void)argc;
    if (args[0] < 0) {
        tiku_basic_ext_error(TIKU_BASIC_ERR_RANGE, "ISQRT of negative");
        return 1;
    }
    n = (unsigned long)args[0];
    x = 0u;
    /* Highest power of 4 <= n, then digit-by-digit (Newton-free, exact). */
    bit = 1uL << ((sizeof(unsigned long) * 8u) - 2u);
    while (bit > n) { bit >>= 2; }
    while (bit != 0u) {
        if (n >= x + bit) {
            n -= x + bit;
            x = (x >> 1) + bit;
        } else {
            x >>= 1;
        }
        bit >>= 2;
    }
    *out = (long)x;
    return 0;
}

/* BITCNT(n): number of set bits in n (population count). */
static int
bext_bitcnt(const long *args, int argc, long *out)
{
    unsigned long v = (unsigned long)args[0];
    long c = 0;
    (void)argc;
    while (v != 0u) {
        v &= v - 1u;             /* clear lowest set bit */
        c++;
    }
    *out = c;
    return 0;
}

/* HEXPR n[, width]: statement -- print n as uppercase hex (no newline), so a
 * program can format bytes/addresses BASIC's decimal PRINT cannot.  Exercises
 * the statement hook + the parse/print/error ABI services. */
static void
bext_hexpr(const char **p)
{
    long v;
    unsigned long u;
    char buf[9];
    int i = 8;
    static const char HX[] = "0123456789ABCDEF";

    if (tiku_basic_ext_parse_expr(p, &v) != 0) {
        return;                  /* error already raised */
    }
    u = (unsigned long)v;
    buf[i] = '\0';
    do {
        buf[--i] = HX[u & 0xFu];
        u >>= 4;
    } while (u != 0u && i > 0);
    tiku_basic_ext_print(&buf[i]);
}

#if TIKU_BASIC_STRVARS_ENABLE

/* REV$(s): the reverse of string s.  Demonstrates a STRING-arg string
 * function -- the handler parses its own args via the ABI. */
static void
bext_rev(const char **p, char *out, size_t cap)
{
    char src[TIKU_BASIC_STR_BUF_CAP];
    size_t n, i;

    if (tiku_basic_ext_expect(p, '(') != 0) { return; }
    if (tiku_basic_ext_parse_strexpr(p, src, sizeof(src)) != 0) { return; }
    if (tiku_basic_ext_expect(p, ')') != 0) { return; }
    n = strlen(src);
    if (n + 1u > cap) {
        tiku_basic_ext_error(TIKU_BASIC_ERR_GENERAL, "string too long");
        return;
    }
    for (i = 0; i < n; i++) {
        out[i] = src[n - 1u - i];
    }
    out[n] = '\0';
}

/* ROMAN$(n): n (1..3999) as a Roman numeral.  Demonstrates a NUMERIC-arg
 * string function. */
static void
bext_roman(const char **p, char *out, size_t cap)
{
    static const struct { int v; const char *s; } R[13] = {
        {1000,"M"},{900,"CM"},{500,"D"},{400,"CD"},{100,"C"},{90,"XC"},
        {50,"L"},{40,"XL"},{10,"X"},{9,"IX"},{5,"V"},{4,"IV"},{1,"I"}
    };
    long   v;
    size_t o = 0;
    int    i;

    if (tiku_basic_ext_expect(p, '(') != 0) { return; }
    if (tiku_basic_ext_parse_expr(p, &v) != 0) { return; }
    if (tiku_basic_ext_expect(p, ')') != 0) { return; }
    if (v < 1 || v > 3999) {
        tiku_basic_ext_error(TIKU_BASIC_ERR_RANGE, "ROMAN$ range 1..3999");
        return;
    }
    for (i = 0; i < 13; i++) {
        size_t sl = strlen(R[i].s);
        while (v >= R[i].v) {
            if (o + sl + 1u > cap) {
                tiku_basic_ext_error(TIKU_BASIC_ERR_GENERAL, "string too long");
                return;
            }
            memcpy(out + o, R[i].s, sl);
            o += sl;
            v -= R[i].v;
        }
    }
    out[o] = '\0';
}

#endif /* TIKU_BASIC_STRVARS_ENABLE */

/* Register the bundled words.  Called once (guarded) at the first BASIC
 * session; idempotent, so a re-call is harmless.  Failures are silent by
 * design -- a full table just means fewer bundled words, never a boot fault. */
static void
basic_ext_register_kits(void)
{
    (void)tiku_basic_register_fn("GCD",    2u, bext_gcd);
    (void)tiku_basic_register_fn("ISQRT",  1u, bext_isqrt);
    (void)tiku_basic_register_fn("BITCNT", 1u, bext_bitcnt);
    (void)tiku_basic_register_stmt("HEXPR", bext_hexpr);
#if TIKU_BASIC_STRVARS_ENABLE
    (void)tiku_basic_register_strfn("REV$",   bext_rev);
    (void)tiku_basic_register_strfn("ROMAN$", bext_roman);
#endif
}

#else  /* no registry, or bundle disabled: nothing to register */

static void basic_ext_register_kits(void) { }

#endif
