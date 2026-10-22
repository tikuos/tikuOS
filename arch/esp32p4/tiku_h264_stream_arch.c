/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_h264_stream_arch.c - P4 H.264: SPS, PPS and slice headers written
 * bit by bit (ITU-T H.264 7.3, 7.4.1.1 emulation prevention), and the
 * frame-level rate controller behind the hardware's QP register.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_cpu_common.h"
#include "tiku_h264_stream_arch.h"

#define LOG2_MAX_FRAME_NUM 8u
#define SLICE_TYPE_I       7u
#define SLICE_TYPE_P       5u
#define LEVEL_IDC_MAX      51u

/** @brief The bits of one NAL unit, a byte at a time: @p p holds the byte
 *         in progress, @p zero_run the 0x00 bytes just emitted. */
typedef struct {
    uint8_t *start, *p, *end;
    unsigned bits_left;
    unsigned zero_run;
} bits_t;

/* H.264 Annex A, Table A-1: the smallest level whose macroblocks a
 * second and a frame hold the picture. */
static const uint32_t levels[][3] = {
    {11880u, 396u, 20u},    {19800u, 792u, 21u},   {20250u, 1620u, 22u},
    {40500u, 1620u, 30u},   {108000u, 3600u, 31u}, {216000u, 5120u, 32u},
    {245760u, 8192u, 41u},  {522240u, 8704u, 42u}, {589824u, 22080u, 50u},
};

TIKU_P4_XIP_TEXT
static uint32_t level_idc(uint16_t width, uint16_t height, uint8_t fps)
{
    uint32_t mbs = ((width + 15u) / 16u) * ((height + 15u) / 16u);
    uint32_t i;

    for (i = 0u; i < sizeof levels / sizeof levels[0]; i++) {
        if (mbs * fps <= levels[i][0] && mbs <= levels[i][1]) {
            return levels[i][2];
        }
    }
    return LEVEL_IDC_MAX;
}

TIKU_P4_XIP_TEXT
static int full(const bits_t *b)
{
    return b->p >= b->end;
}

/** @brief One finished byte into the stream, a 0x03 before it when two
 *         0x00 came before and it is 0x00..0x03. */
TIKU_P4_XIP_TEXT
static void emit(bits_t *b, uint8_t byte)
{
    if (b->zero_run >= 2u && byte <= 3u) {
        if (!full(b)) {
            b->p[0] = 0x03u;
        }
        b->p++;
        b->zero_run = 0u;
    }
    if (!full(b)) {
        b->p[0] = byte;
    }
    b->p++;
    b->zero_run = byte == 0u ? b->zero_run + 1u : 0u;
}

TIKU_P4_XIP_TEXT
static void put1(bits_t *b, uint32_t v)
{
    b->bits_left--;
    if (!full(b)) {
        b->p[0] |= (uint8_t)((v & 1u) << b->bits_left);
    }
    if (b->bits_left == 0u) {
        emit(b, full(b) ? 0u : b->p[0]);
        if (!full(b)) {
            b->p[0] = 0u;
        }
        b->bits_left = 8u;
    }
}

TIKU_P4_XIP_TEXT
static void put(bits_t *b, uint32_t v, unsigned n)
{
    while (n-- > 0u) {
        put1(b, (v >> n) & 1u);
    }
}

/** @brief Exp-Golomb code of @p v: the leading zeros of its length. */
TIKU_P4_XIP_TEXT
static void put_ue(bits_t *b, uint32_t v)
{
    uint32_t x = v + 1u;
    unsigned len = 0u;

    while ((x >> len) > 1u) {
        len++;
    }
    put(b, 0u, len);
    put(b, x, len + 1u);
}

TIKU_P4_XIP_TEXT
static void put_se(bits_t *b, int32_t v)
{
    put_ue(b, v <= 0 ? (uint32_t)(-v) * 2u : (uint32_t)v * 2u - 1u);
}

TIKU_P4_XIP_TEXT
static void trailing(bits_t *b)
{
    put1(b, 1u);
    put(b, 0u, b->bits_left & 7u);
}

TIKU_P4_XIP_TEXT
static uint32_t bits_used(const bits_t *b)
{
    return (8u - b->bits_left) + (uint32_t)(b->p - b->start) * 8u;
}

/** @brief Open @p buf for a NAL unit behind a 4-byte start code of
 *         @p code; @return 0 when @p cap cannot hold one. */
TIKU_P4_XIP_TEXT
static int open_nal(bits_t *b, uint8_t *buf, uint32_t cap, uint32_t code)
{
    if (buf == NULL || cap < 8u) {
        return 0;
    }
    buf[0] = (uint8_t)code;
    buf[1] = (uint8_t)(code >> 8);
    buf[2] = (uint8_t)(code >> 16);
    buf[3] = (uint8_t)(code >> 24);
    b->start = buf + 4;
    b->p = buf + 4;
    b->end = buf + cap;
    b->bits_left = 8u;
    b->zero_run = 0u;
    b->p[0] = 0u;
    return 1;
}

TIKU_P4_XIP_TEXT
uint32_t tiku_p4_h264_sps(uint8_t *buf, uint32_t cap, uint16_t width,
                          uint16_t height, uint8_t fps)
{
    bits_t b;
    uint32_t mb_w = (width + 15u) / 16u, mb_h = (height + 15u) / 16u;
    uint32_t crop = (width % 16u) != 0u || (height % 16u) != 0u;

    if (!open_nal(&b, buf, cap, 0x01000000u)) {
        return 0u;
    }
    put(&b, 0u, 1);                     /* forbidden_zero_bit */
    put(&b, 3u, 2);                     /* nal_ref_idc */
    put(&b, 7u, 5);                     /* nal_unit_type: SPS */
    put(&b, 66u, 8);                    /* profile_idc: baseline */
    put(&b, 1u, 1);                     /* constraint_set0 */
    put(&b, 1u, 1);                     /* constraint_set1 */
    put(&b, 0u, 4);                     /* constraint_set2..5 */
    put(&b, 0u, 2);                     /* reserved_zero_2bits */
    put(&b, level_idc(width, height, fps), 8);
    put_ue(&b, 0u);                     /* seq_parameter_set_id */
    put_ue(&b, LOG2_MAX_FRAME_NUM - 4u);
    put_ue(&b, 2u);                     /* pic_order_cnt_type */
    put_ue(&b, 1u);                     /* num_ref_frames */
    put(&b, 0u, 1);                     /* gaps_in_frame_num_value_allowed */
    put_ue(&b, mb_w - 1u);
    put_ue(&b, mb_h - 1u);
    put(&b, 1u, 1);                     /* frame_mbs_only_flag */
    put(&b, 0u, 1);                     /* direct_8x8_inference_flag */
    put(&b, crop, 1);                   /* frame_cropping_flag */
    if (crop) {
        put_ue(&b, 0u);
        put_ue(&b, (mb_w * 16u - width) / 2u);
        put_ue(&b, 0u);
        put_ue(&b, (mb_h * 16u - height) / 2u);
    }
    put(&b, 1u, 1);                     /* vui_parameters_present_flag */
    put(&b, 0u, 4);                     /* aspect, overscan, signal, chroma */
    put(&b, 1u, 1);                     /* timing_info_present_flag */
    put(&b, 1u, 32);                    /* num_units_in_tick */
    put(&b, (uint32_t)fps * 2u, 32);    /* time_scale */
    put(&b, 1u, 1);                     /* fixed_frame_rate_flag */
    put(&b, 0u, 4);                     /* nal/vcl hrd, pic_struct, restr. */
    trailing(&b);
    return bits_used(&b) + 32u;
}

TIKU_P4_XIP_TEXT
uint32_t tiku_p4_h264_pps(uint8_t *buf, uint32_t cap, uint8_t qp)
{
    bits_t b;

    if (!open_nal(&b, buf, cap, 0x01000000u)) {
        return 0u;
    }
    put(&b, 0u, 1);
    put(&b, 3u, 2);                     /* nal_ref_idc */
    put(&b, 8u, 5);                     /* nal_unit_type: PPS */
    put_ue(&b, 0u);                     /* pic_parameter_set_id */
    put_ue(&b, 0u);                     /* seq_parameter_set_id */
    put(&b, 0u, 1);                     /* entropy_coding_mode: CAVLC */
    put(&b, 0u, 1);                     /* bottom_field_pic_order */
    put_ue(&b, 0u);                     /* num_slice_groups_minus1 */
    put_ue(&b, 0u);                     /* num_ref_idx_l0_default_active */
    put_ue(&b, 0u);                     /* num_ref_idx_l1_default_active */
    put(&b, 0u, 1);                     /* weighted_pred_flag */
    put(&b, 0u, 2);                     /* weighted_bipred_idc */
    put_se(&b, (int32_t)qp - 26);       /* pic_init_qp_minus26 */
    put_se(&b, (int32_t)qp - 26);       /* pic_init_qs_minus26 */
    put_se(&b, 0);                      /* chroma_qp_index_offset */
    put(&b, 1u, 1);                     /* deblocking_filter_control_present */
    put(&b, 0u, 1);                     /* constrained_intra_pred_flag */
    put(&b, 0u, 1);                     /* redundant_pic_cnt_present_flag */
    trailing(&b);
    return bits_used(&b) + 32u;
}

TIKU_P4_XIP_TEXT
uint32_t tiku_p4_h264_slice(uint8_t *buf, uint32_t cap, int idr,
                            uint8_t frame_num, int8_t qp_delta)
{
    bits_t b;

    if (!open_nal(&b, buf, cap, 0xFFFFFFFFu)) {
        return 0u;
    }
    put(&b, 0u, 1);
    put(&b, idr ? 3u : 2u, 2);          /* nal_ref_idc */
    put(&b, idr ? 5u : 1u, 5);          /* nal_unit_type: IDR or slice */
    put_ue(&b, 0u);                     /* first_mb_in_slice */
    put_ue(&b, idr ? SLICE_TYPE_I : SLICE_TYPE_P);
    put_ue(&b, 0u);                     /* pic_parameter_set_id */
    put(&b, frame_num, LOG2_MAX_FRAME_NUM);
    if (idr) {
        put_ue(&b, 0u);                 /* idr_pic_id */
    } else {
        put(&b, 0u, 1);                 /* num_ref_idx_active_override */
        put(&b, 0u, 1);                 /* ref_pic_list_modification_l0 */
    }
    if (idr) {
        put(&b, 0u, 1);                 /* no_output_of_prior_pics_flag */
        put(&b, 0u, 1);                 /* long_term_reference_flag */
    } else {
        put(&b, 0u, 1);                 /* adaptive_ref_pic_marking_mode */
    }
    put_se(&b, qp_delta);               /* slice_qp_delta */
    put_se(&b, 0);                      /* disable_deblocking_filter_idc */
    put_se(&b, 0);                      /* slice_alpha_c0_offset_div2 */
    put_se(&b, 0);                      /* slice_beta_offset_div2 */
    return bits_used(&b) + 32u;
}

/* The controller: the reference's frame-level loop. */

static const uint8_t init_mad[] = {1u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 9u, 9u,
                                   9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u};

#define CLIP3(lo, hi, v) ((v) > (hi) ? (hi) : ((v) < (lo) ? (lo) : (v)))

TIKU_P4_XIP_TEXT
void tiku_p4_h264_rc_init(tiku_p4_h264_rc_t *rc, uint8_t qp_min,
                          uint8_t qp_max, uint32_t bitrate, uint8_t fps,
                          uint16_t mb_count)
{
    unsigned i, idx;
    float mad;

    rc->frame = 0u;
    rc->qp_min = qp_min;
    rc->qp_max = qp_max;
    rc->qpm = (int8_t)((qp_min + qp_max) >> 1);
    rc->eqp = 0;
    rc->qp_average = (uint8_t)rc->qpm;
    rc->bits_per_frame = fps ? bitrate / fps : bitrate;
    if (rc->bits_per_frame == 0u) {
        rc->bits_per_frame = 1u;
    }
    rc->ebits = 0;
    rc->ebits_clip = bitrate;
    rc->err_sum = 0.0f;
    rc->mb_count = mb_count;
    idx = 53u / ((unsigned)rc->qpm + 1u);
    if (idx >= sizeof init_mad) {
        idx = sizeof init_mad - 1u;
    }
    mad = 8.0f / (float)init_mad[idx];
    for (i = 0u; i < 4u; i++) {
        rc->bits_last[i] = rc->bits_per_frame;
        rc->mad[i] = mad;
    }
    rc->bits_last_average = rc->bits_per_frame;
    rc->mad_last_average = mad;
    rc->mad_pred = mad;
    rc->target_bits = rc->bits_per_frame;
}

TIKU_P4_XIP_TEXT
void tiku_p4_h264_rc_start(tiku_p4_h264_rc_t *rc, int idr, uint32_t *rate,
                           uint32_t *pred_mad, uint8_t *qp)
{
    int32_t target = (int32_t)((rc->bits_per_frame * 10u -
                                4u * rc->bits_last_average) / 6u);
    int q;

    if ((int64_t)rc->bits_per_frame * 10 < (int64_t)rc->bits_last_average * 4 ||
        target < 1) {
        target = 1;
    }
    rc->target_bits = (uint32_t)target;
    rc->mad_pred = rc->bits_last_average == 0u
                       ? rc->mad_last_average
                       : rc->mad_last_average * (float)rc->target_bits /
                             (float)rc->bits_last_average;
    if (rc->mad_pred < 1.0f) {
        rc->mad_pred = 1.0f;
    }
    q = (int)rc->qp_average + (int)rc->eqp;
    rc->qpm = (int8_t)q;
    *qp = (uint8_t)CLIP3((int)rc->qp_min, (int)rc->qp_max, q);
    *rate = 1u;
    *pred_mad = (uint32_t)rc->mad_pred;
    if (!idr && rc->mb_count) {
        float r = 256.0f * (float)(rc->target_bits / rc->mb_count) /
                  rc->mad_pred / 15.0f;

        *rate = r < 1.0f ? 1u : (uint32_t)r;
    }
}

/** @brief @p v as a float.  |v| is at most the bit rate, a 32-bit value,
 *         so it converts through 32 bits in one FPU instruction. */
static float bits_float(int64_t v)
{
    return v < 0 ? -(float)(uint32_t)-v : (float)(uint32_t)v;
}

void tiku_p4_h264_rc_end(tiku_p4_h264_rc_t *rc, uint32_t bits,
                         uint32_t qp_sum, uint32_t mad_sum)
{
    float bits_err, err_frame;
    int64_t frame_err, e;
    unsigned i;

    if (rc->mb_count == 0u || rc->bits_per_frame == 0u) {
        return;
    }
    rc->qp_average = (uint8_t)(qp_sum / rc->mb_count);
    frame_err = (int64_t)bits - (int64_t)rc->bits_per_frame;
    e = rc->ebits + frame_err;
    rc->ebits = CLIP3(-rc->ebits_clip, rc->ebits_clip, e);
    rc->mad[rc->frame & 3u] = (float)mad_sum / (float)rc->mb_count;
    rc->bits_last[rc->frame & 3u] = bits;
    rc->bits_last_average = 0u;
    rc->mad_last_average = 0.0f;
    for (i = 0u; i < 4u; i++) {
        rc->bits_last_average += rc->bits_last[i];
        rc->mad_last_average += rc->mad[i];
    }
    rc->bits_last_average >>= 2;
    rc->mad_last_average /= 4.0f;
    bits_err = (float)rc->bits_last_average / (float)rc->bits_per_frame - 1.0f;
    if (rc->err_sum > 10.0f) {
        rc->err_sum = rc->err_sum * 0.85f + bits_err;
    } else {
        rc->err_sum += bits_err;
    }
    rc->eqp = (int8_t)CLIP3(-5, 5, (int)rc->err_sum);
    err_frame = bits_float(rc->ebits) / (float)rc->bits_per_frame;
    if (err_frame > 0.2f && rc->eqp < 1) {
        rc->eqp = 1;
    } else if (err_frame < -0.2f && rc->eqp > -1) {
        rc->eqp = -1;
    }
    rc->frame++;
}
