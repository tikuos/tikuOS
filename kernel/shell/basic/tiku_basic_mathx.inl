/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_mathx.inl - extended fixed-point math for the full BASIC
 * profile: LOG (natural log), EXP, POW, ATAN. Q.3 fixed-point (scale
 * 1000), modeled on the bit-iterative SQR and the LUT trig already in
 * tiku_basic_call.inl / tiku_basic_trig.inl. All intermediates use
 * 64-bit (long long); results are coarse (~3 decimals) by the Q.3
 * representation. NOT a standalone translation unit -- included from
 * tiku_basic.c BEFORE tiku_basic_call.inl so the call dispatch can
 * reach these helpers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_MATHX_ENABLE

#define MX_SCALE  ((long long)TIKU_BASIC_FIXED_SCALE)   /* 1000 */
#define MX_LN2    693L                                  /* ln(2) in Q.3   */
#define MX_E      2718L                                 /* e     in Q.3   */

/* sqrt(x) for x in Q.3, result in Q.3 -- same bit-by-bit isqrt as the
 * SQR builtin, broken out as a helper for ATAN's argument reduction. */
static long mx_sqrt_q3(long x)
{
    long long t, res = 0, bit;
    if (x <= 0) return 0;
    t = (long long)x * MX_SCALE;
    bit = 1LL << 30;
    while (bit > t) bit >>= 2;
    while (bit > 0) {
        if (t >= res + bit) { t -= res + bit; res = (res >> 1) + bit; }
        else                {                  res >>= 1; }
        bit >>= 2;
    }
    return (long)res;
}

/* ln(x) for x in Q.3, x > 0. Returns Q.3. Reduce x = m * 2^k with m in
 * [1,2): ln(x) = k*ln2 + ln(m); ln(m) = 2*atanh(t), t = (m-1)/(m+1),
 * via the odd-power series (t small, converges fast). */
static long basic_log_q3(long x)
{
    long k = 0;
    long long m, t, t2, term, sum;
    int i;
    if (x <= 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "LOG domain (x>0)");
        return 0;
    }
    m = x;
    while (m >= 2 * MX_SCALE) { m /= 2; k++; }
    while (m <      MX_SCALE) { m *= 2; k--; }
    t  = ((m - MX_SCALE) * MX_SCALE) / (m + MX_SCALE);   /* (m-1)/(m+1) Q.3 */
    t2 = (t * t) / MX_SCALE;
    term = t; sum = 0;
    for (i = 1; i <= 11; i += 2) { sum += term / i; term = (term * t2) / MX_SCALE; }
    sum *= 2;
    return (long)((long long)k * MX_LN2 + sum);
}

/* e^x for x in Q.3. Returns Q.3. Split x = n + f (n integer real part,
 * f in [0,1) Q.3): e^x = e^n * e^f; e^n by repeated *e, e^f by Taylor.
 * Negative x via reciprocal. Guarded against overflow. */
static long basic_exp_q3(long x)
{
    long n, f, i2;
    long long e_n = MX_SCALE, e_f, term;
    int i, neg = 0;
    if (x < 0) { neg = 1; x = -x; }
    n = x / (long)MX_SCALE;
    f = x % (long)MX_SCALE;
    if (n > 14) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "EXP overflow");
        return 0;
    }
    for (i2 = 0; i2 < n; i2++) e_n = (e_n * MX_E) / MX_SCALE;
    e_f = MX_SCALE; term = MX_SCALE;
    for (i = 1; i <= 8; i++) { term = (term * f) / MX_SCALE / i; e_f += term; }
    {
        long long r = (e_n * e_f) / MX_SCALE;
        if (neg) r = (r != 0) ? (MX_SCALE * MX_SCALE) / r : 0;
        return (long)r;
    }
}

/* b^e for b,e in Q.3, b > 0: POW = exp(e * ln(b)). */
static long basic_pow_q3(long b, long e)
{
    long lb, prod;
    if (b <= 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "POW base must be > 0");
        return 0;
    }
    lb = basic_log_q3(b);
    if (basic_error) return 0;
    prod = (long)(((long long)e * (long long)lb) / MX_SCALE);   /* e*ln(b) */
    return basic_exp_q3(prod);
}

/* atan(x) for x in Q.3, result radians in Q.3. |x|>1 -> pi/2 - atan(1/x);
 * then two argument halvings (atan(x)=2*atan(x/(1+sqrt(1+x^2)))) so the
 * odd-power series converges quickly. */
static long basic_atan_q3(long x)
{
    int neg = 0, gt1 = 0, i;
    long half_pi = (long)TIKU_BASIC_PI_Q3 / 2L;          /* pi/2 in Q.3 */
    long long t, t2, term, sum;
    if (x < 0) { neg = 1; x = -x; }
    if (x > (long)MX_SCALE) { gt1 = 1; x = (long)((MX_SCALE * MX_SCALE) / x); }
    for (i = 0; i < 2; i++) {
        long long xx = ((long long)x * x) / MX_SCALE;    /* x^2 Q.3 */
        long s = mx_sqrt_q3((long)(MX_SCALE + xx));       /* sqrt(1+x^2) */
        x = (long)(((long long)x * MX_SCALE) / (MX_SCALE + s));
    }
    t = x; t2 = (t * t) / MX_SCALE; term = t; sum = 0;
    for (i = 1; i <= 9; i += 2) {
        if ((i / 2) & 1) sum -= term / i; else sum += term / i;
        term = (term * t2) / MX_SCALE;
    }
    sum *= 4;                                             /* undo 2 halvings */
    {
        long r = (long)sum;
        if (gt1) r = half_pi - r;
        return neg ? -r : r;
    }
}

#endif /* TIKU_BASIC_MATHX_ENABLE */
