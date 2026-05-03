/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_trig.inl - SIN / COS via 65-entry quarter-circle LUT.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Inputs and outputs are Q.3 fixed point (radians x 1000, sin
 * output x 1000).  Reduces the argument modulo 2*pi, maps it to
 * [0, pi/2] via reflection, then linearly interpolates between LUT
 * samples.  Max error ~5e-4, well inside Q.3 precision.  ~130 bytes
 * of rodata.
 *
 * TAN is implemented as SIN/COS at the call site in expr_call() so
 * we don't need a separate tan() helper here.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_FIXED_ENABLE

/*---------------------------------------------------------------------------*/
/* QUARTER-CIRCLE LUT                                                        */
/*---------------------------------------------------------------------------*/

/* sin_lut[i] = round(sin(i * pi / 128) * 1000) for i = 0..64.
 * 65 entries covers [0, pi/2]; reflection handles the other
 * quadrants.  Linear interpolation between samples gives ~5e-4 max
 * error, well within Q.3 precision (1e-3).  130 bytes of rodata. */
static const int16_t basic_sin_lut[65] = {
       0,   25,   49,   74,   98,  122,  147,  171,
     195,  219,  243,  267,  290,  314,  337,  360,
     383,  405,  428,  450,  471,  493,  514,  535,
     556,  576,  596,  615,  634,  653,  672,  690,
     707,  724,  741,  757,  773,  788,  803,  818,
     831,  845,  858,  870,  882,  893,  904,  914,
     924,  933,  942,  950,  957,  964,  970,  976,
     981,  985,  989,  992,  995,  997,  999, 1000,
    1000,
};

/*---------------------------------------------------------------------------*/
/* SIN / COS                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Compute sin(x) in Q.3 fixed point.
 *
 * Reduces the argument modulo 2*pi, maps it to [0, pi/2] via the
 * usual sign / reflection identities, then linearly interpolates
 * between two adjacent LUT samples.  Negative arguments work
 * naturally: SIN(-PI/2) = -1000.
 *
 * @param angle_q3  Angle in radians, scaled by 1000.
 * @return          sin(angle) x 1000 (range -1000..+1000).
 */
static long
basic_sin_q3(long angle_q3)
{
    const long PI    = (long)TIKU_BASIC_PI_Q3;          /* 3142 */
    const long TWOPI = 2L * (long)TIKU_BASIC_PI_Q3;     /* 6284 */
    const long HPI   = (long)TIKU_BASIC_PI_Q3 / 2L;     /* 1571 */
    long sign = 1;
    long idx_q;
    long lut_idx;
    long lut_frac;
    long a, b;

    /* Reduce to [0, 2*pi).  Handle negatives by adding multiples. */
    angle_q3 %= TWOPI;
    if (angle_q3 < 0) {
        angle_q3 += TWOPI;
    }

    /* sin(pi + x) = -sin(x) */
    if (angle_q3 >= PI) {
        angle_q3 -= PI;
        sign = -sign;
    }
    /* sin(pi/2 + x) = sin(pi/2 - x) -- reflect into [0, pi/2]. */
    if (angle_q3 > HPI) {
        angle_q3 = PI - angle_q3;
    }

    /* Map angle in [0, HPI] to [0, 64] LUT index, in Q.6 to keep
     * the linear-interp fraction.  idx_q = angle * 64 / HPI; the
     * fractional part is (idx_q & 63). */
    if (angle_q3 <= 0) {
        return 0;
    }
    if (angle_q3 >= HPI) {
        return sign * 1000L;
    }
    idx_q    = (angle_q3 * 64L * 64L) / HPI;     /* Q.6 index */
    lut_idx  = idx_q >> 6;
    lut_frac = idx_q & 63;
    if (lut_idx >= 64) {
        return sign * 1000L;
    }
    a = (long)basic_sin_lut[lut_idx];
    b = (long)basic_sin_lut[lut_idx + 1];
    return sign * (a + ((b - a) * lut_frac) / 64);
}

/**
 * @brief Compute cos(x) in Q.3 fixed point as sin(x + pi/2).
 *
 * @param angle_q3  Angle in radians, scaled by 1000.
 * @return          cos(angle) x 1000 (range -1000..+1000).
 */
static long
basic_cos_q3(long angle_q3)
{
    return basic_sin_q3(angle_q3 + (long)TIKU_BASIC_PI_Q3 / 2L);
}

#endif /* TIKU_BASIC_FIXED_ENABLE */
