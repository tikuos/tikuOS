/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_msg.c - a message that describes itself.
 *
 * The whole of it is three structures deep.  A message is a code and a
 * list of fields; a field is a name, a type and a list of values; a value
 * is a run of bytes and how many.  Everything the interface offers is
 * that, read one way or another.
 *
 * One decision runs through the rest: a value is held in memory in the
 * SAME bytes it goes on the wire in -- little-endian, sizes fixed by type.
 * So flattening is a copy rather than a conversion, and there is exactly
 * one place where a number turns into bytes instead of two that must be
 * kept agreeing.  A number is decoded when somebody asks for it, which
 * costs four shifts and is not where any time goes.
 *
 * The other decision is that a nested message is held FLATTENED, as a run
 * of bytes like any other value.  It is stored as it will be sent, so
 * nesting costs nothing to flatten; and reading one is a call to unflatten
 * on that blob, which stops at that one level.  A message a thousand deep
 * is therefore not a thousand frames of stack -- it is a thousand
 * separate, deliberate asks.  That falls out of the choice rather than
 * being guarded for, which is the better way to not have the problem.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <string.h>

#include "tiku_msg.h"

/*
 * Floats go on the wire as their IEEE 754 bit pattern, little-endian.
 * Every machine this will run on is IEEE; saying so is worth more than
 * the code that would pretend otherwise and be wrong in a different way.
 */

/** @brief One value: bytes, and how many of them. */
typedef struct {
    void   *data;
    size_t  len;
} val_t;

/** @brief One named field: a type, and the values under it. */
typedef struct {
    char            *name;
    tiku_msg_type_t  type;
    val_t           *val;
    int              nval;
    int              cap;
} field_t;

struct tiku_msg {
    uint32_t  what;
    field_t  *field;
    int       nfield;
    int       cap;
};

/*---------------------------------------------------------------------------*/
/* Bytes                                                                     */
/*---------------------------------------------------------------------------*/

static void
enc16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void
enc32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static void
enc64(unsigned char *p, uint64_t v)
{
    enc32(p, (uint32_t)(v & 0xFFFFFFFFu));
    enc32(p + 4, (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

static uint16_t
dec16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t
dec32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t
dec64(const unsigned char *p)
{
    return (uint64_t)dec32(p) | ((uint64_t)dec32(p + 4) << 32);
}

/** @brief How many bytes a value of @p type must be, or 0 when it varies. */
static size_t
fixed_size(tiku_msg_type_t type)
{
    switch (type) {
    case TIKU_MSG_T_BOOL:
    case TIKU_MSG_T_INT8:
        return 1u;
    case TIKU_MSG_T_INT16:
        return 2u;
    case TIKU_MSG_T_INT32:
    case TIKU_MSG_T_FLOAT:
        return 4u;
    case TIKU_MSG_T_INT64:
    case TIKU_MSG_T_DOUBLE:
    case TIKU_MSG_T_POINT:
        return 8u;
    case TIKU_MSG_T_RECT:
        return 16u;
    default:
        return 0u;
    }
}

/*---------------------------------------------------------------------------*/
/* Making and unmaking                                                       */
/*---------------------------------------------------------------------------*/

tiku_msg_t *
tiku_msg_new(uint32_t what)
{
    tiku_msg_t *m = (tiku_msg_t *)calloc(1u, sizeof *m);

    if (m != NULL) {
        m->what = what;
    }
    return m;
}

/** @brief Let go of one field and everything under it. */
static void
field_drop(field_t *f)
{
    int i;

    for (i = 0; i < f->nval; i++) {
        free(f->val[i].data);
    }
    free(f->val);
    free(f->name);
    f->val = NULL;
    f->name = NULL;
    f->nval = 0;
    f->cap = 0;
}

void
tiku_msg_clear(tiku_msg_t *m)
{
    int i;

    if (m == NULL) {
        return;
    }
    for (i = 0; i < m->nfield; i++) {
        field_drop(&m->field[i]);
    }
    m->nfield = 0;
}

void
tiku_msg_free(tiku_msg_t *m)
{
    if (m == NULL) {
        return;
    }
    tiku_msg_clear(m);
    free(m->field);
    free(m);
}

uint32_t
tiku_msg_what(const tiku_msg_t *m)
{
    return (m != NULL) ? m->what : 0u;
}

void
tiku_msg_set_what(tiku_msg_t *m, uint32_t what)
{
    if (m != NULL) {
        m->what = what;
    }
}

/*---------------------------------------------------------------------------*/
/* Fields                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief The field called @p name, or NULL.
 *
 * The FIRST of that name.  A message this interface built never holds two,
 * since adding merges; one read off a wire may, and then the later one is
 * unreachable by name but still carried, which is the safe way round.
 */
static field_t *
field_find(const tiku_msg_t *m, const char *name)
{
    int i;

    if (m == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < m->nfield; i++) {
        if (strcmp(m->field[i].name, name) == 0) {
            return &m->field[i];
        }
    }
    return NULL;
}

/** @brief Room for one more of something. @return 1 when there is. */
static int
grow(void **base, int *cap, int need, size_t unit)
{
    int want;
    void *bigger;

    if (need <= *cap) {
        return 1;
    }
    want = (*cap > 0) ? *cap * 2 : 4;
    while (want < need) {
        if (want > (int)(0x7FFFFFFF / 2)) {
            return 0;
        }
        want *= 2;
    }
    if ((size_t)want > (size_t)-1 / unit) {
        return 0;
    }
    bigger = realloc(*base, (size_t)want * unit);
    if (bigger == NULL) {
        return 0;
    }
    *base = bigger;
    *cap = want;
    return 1;
}

/**
 * @brief Append an empty field, or NULL.
 *
 * The name is given with its length rather than as a C string, so the
 * unflatten path can point straight into the buffer it is reading instead
 * of copying sixty-four kilobytes of stack around to terminate it.
 */
static field_t *
field_add(tiku_msg_t *m, const char *name, size_t n,
          tiku_msg_type_t type)
{
    field_t *f;
    char *copy;

    if (!grow((void **)&m->field, &m->cap, m->nfield + 1, sizeof *m->field)) {
        return NULL;
    }
    copy = (char *)malloc(n + 1u);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, name, n);
    copy[n] = '\0';

    f = &m->field[m->nfield];
    f->name = copy;
    f->type = type;
    f->val = NULL;
    f->nval = 0;
    f->cap = 0;
    m->nfield++;
    return f;
}

/**
 * @brief Put @p len bytes under @p name as a value of @p type.
 *
 * Nothing about @p m changes unless the whole of it works: the bytes are
 * taken first, and a field made for them is unmade again if the value
 * cannot then be hung off it.  A half-added field is the kind of thing
 * that is found much later, in something else.
 */
static int
put(tiku_msg_t *m, const char *name, tiku_msg_type_t type,
    const void *data, size_t len)
{
    field_t *f;
    void *copy;
    int made = 0;

    if (m == NULL || name == NULL || name[0] == '\0') {
        return 0;
    }
    if (strlen(name) > 0xFFFFu) {           /* the wire says u16 */
        return 0;
    }
    if (len > 0u && data == NULL) {
        return 0;
    }
    if (len > 0xFFFFFFFFu) {                /* the wire says u32 */
        return 0;
    }

    copy = malloc(len > 0u ? len : 1u);
    if (copy == NULL) {
        return 0;
    }
    if (len > 0u) {
        memcpy(copy, data, len);
    }

    f = field_find(m, name);
    if (f != NULL && f->type != type) {
        free(copy);                 /* the name already means another thing */
        return 0;
    }
    if (f == NULL) {
        f = field_add(m, name, strlen(name), type);
        if (f == NULL) {
            free(copy);
            return 0;
        }
        made = 1;
    }
    if (!grow((void **)&f->val, &f->cap, f->nval + 1, sizeof *f->val)) {
        if (made) {
            field_drop(f);
            m->nfield--;
        }
        free(copy);
        return 0;
    }
    f->val[f->nval].data = copy;
    f->val[f->nval].len = len;
    f->nval++;
    return 1;
}

/** @brief The one value asked for, or NULL if it is not there as asked. */
static const val_t *
value_of(const tiku_msg_t *m, const char *name, int index,
         tiku_msg_type_t type)
{
    const field_t *f = field_find(m, name);

    if (f == NULL || f->type != type || index < 0 || index >= f->nval) {
        return NULL;
    }
    return &f->val[index];
}

/*---------------------------------------------------------------------------*/
/* Putting things in                                                         */
/*---------------------------------------------------------------------------*/

int
tiku_msg_add_bool(tiku_msg_t *m, const char *name, int v)
{
    unsigned char b = (unsigned char)(v ? 1 : 0);

    return put(m, name, TIKU_MSG_T_BOOL, &b, 1u);
}

int
tiku_msg_add_int8(tiku_msg_t *m, const char *name, int8_t v)
{
    unsigned char b = (unsigned char)v;

    return put(m, name, TIKU_MSG_T_INT8, &b, 1u);
}

int
tiku_msg_add_int16(tiku_msg_t *m, const char *name, int16_t v)
{
    unsigned char b[2];

    enc16(b, (uint16_t)v);
    return put(m, name, TIKU_MSG_T_INT16, b, sizeof b);
}

int
tiku_msg_add_int32(tiku_msg_t *m, const char *name, int32_t v)
{
    unsigned char b[4];

    enc32(b, (uint32_t)v);
    return put(m, name, TIKU_MSG_T_INT32, b, sizeof b);
}

int
tiku_msg_add_int64(tiku_msg_t *m, const char *name, int64_t v)
{
    unsigned char b[8];

    enc64(b, (uint64_t)v);
    return put(m, name, TIKU_MSG_T_INT64, b, sizeof b);
}

/** @brief A float as its four bytes, little-endian. */
static void
enc_f32(unsigned char *p, float v)
{
    uint32_t bits;

    memcpy(&bits, &v, sizeof bits);
    enc32(p, bits);
}

static float
dec_f32(const unsigned char *p)
{
    uint32_t bits = dec32(p);
    float v;

    memcpy(&v, &bits, sizeof v);
    return v;
}

int
tiku_msg_add_float(tiku_msg_t *m, const char *name, float v)
{
    unsigned char b[4];

    enc_f32(b, v);
    return put(m, name, TIKU_MSG_T_FLOAT, b, sizeof b);
}

int
tiku_msg_add_double(tiku_msg_t *m, const char *name, double v)
{
    unsigned char b[8];
    uint64_t bits;

    memcpy(&bits, &v, sizeof bits);
    enc64(b, bits);
    return put(m, name, TIKU_MSG_T_DOUBLE, b, sizeof b);
}

int
tiku_msg_add_string(tiku_msg_t *m, const char *name, const char *v)
{
    if (v == NULL) {
        return 0;
    }
    return put(m, name, TIKU_MSG_T_STRING, v, strlen(v) + 1u);
}

int
tiku_msg_add_ref(tiku_msg_t *m, const char *name, const char *path)
{
    if (path == NULL) {
        return 0;
    }
    return put(m, name, TIKU_MSG_T_REF, path, strlen(path) + 1u);
}

int
tiku_msg_add_blob(tiku_msg_t *m, const char *name,
                       const void *bytes, size_t len)
{
    return put(m, name, TIKU_MSG_T_BLOB, bytes, len);
}

int
tiku_msg_add_point(tiku_msg_t *m, const char *name,
                        float x, float y)
{
    unsigned char b[8];

    enc_f32(b, x);
    enc_f32(b + 4, y);
    return put(m, name, TIKU_MSG_T_POINT, b, sizeof b);
}

int
tiku_msg_add_rect(tiku_msg_t *m, const char *name,
                       tiku_msg_rect_t r)
{
    unsigned char b[16];

    enc_f32(b, r.x);
    enc_f32(b + 4, r.y);
    enc_f32(b + 8, r.w);
    enc_f32(b + 12, r.h);
    return put(m, name, TIKU_MSG_T_RECT, b, sizeof b);
}

int
tiku_msg_add_message(tiku_msg_t *m, const char *name,
                          const tiku_msg_t *sub)
{
    size_t n;
    unsigned char *flat;
    int ok;

    if (sub == NULL) {
        return 0;
    }
    n = tiku_msg_flat_size(sub);
    if (n == 0u) {
        return 0;
    }
    flat = (unsigned char *)malloc(n);
    if (flat == NULL) {
        return 0;
    }
    ok = tiku_msg_flatten(sub, flat, n, NULL) &&
         put(m, name, TIKU_MSG_T_MESSAGE, flat, n);
    free(flat);
    return ok;
}

/*---------------------------------------------------------------------------*/
/* Taking things out                                                         */
/*---------------------------------------------------------------------------*/

int
tiku_msg_find_bool(const tiku_msg_t *m, const char *name,
                        int index, int *out)
{
    const val_t *v = value_of(m, name, index, TIKU_MSG_T_BOOL);

    if (v == NULL || v->len != 1u || out == NULL) {
        return 0;
    }
    *out = (*(const unsigned char *)v->data != 0u) ? 1 : 0;
    return 1;
}

int
tiku_msg_find_int8(const tiku_msg_t *m, const char *name,
                        int index, int8_t *out)
{
    const val_t *v = value_of(m, name, index, TIKU_MSG_T_INT8);

    if (v == NULL || v->len != 1u || out == NULL) {
        return 0;
    }
    *out = (int8_t)*(const unsigned char *)v->data;
    return 1;
}

int
tiku_msg_find_int16(const tiku_msg_t *m, const char *name,
                         int index, int16_t *out)
{
    const val_t *v = value_of(m, name, index, TIKU_MSG_T_INT16);

    if (v == NULL || v->len != 2u || out == NULL) {
        return 0;
    }
    *out = (int16_t)dec16((const unsigned char *)v->data);
    return 1;
}

int
tiku_msg_find_int32(const tiku_msg_t *m, const char *name,
                         int index, int32_t *out)
{
    const val_t *v = value_of(m, name, index, TIKU_MSG_T_INT32);

    if (v == NULL || v->len != 4u || out == NULL) {
        return 0;
    }
    *out = (int32_t)dec32((const unsigned char *)v->data);
    return 1;
}

int
tiku_msg_find_int64(const tiku_msg_t *m, const char *name,
                         int index, int64_t *out)
{
    const val_t *v = value_of(m, name, index, TIKU_MSG_T_INT64);

    if (v == NULL || v->len != 8u || out == NULL) {
        return 0;
    }
    *out = (int64_t)dec64((const unsigned char *)v->data);
    return 1;
}

int
tiku_msg_find_float(const tiku_msg_t *m, const char *name,
                         int index, float *out)
{
    const val_t *v = value_of(m, name, index, TIKU_MSG_T_FLOAT);

    if (v == NULL || v->len != 4u || out == NULL) {
        return 0;
    }
    *out = dec_f32((const unsigned char *)v->data);
    return 1;
}

int
tiku_msg_find_double(const tiku_msg_t *m, const char *name,
                          int index, double *out)
{
    const val_t *v = value_of(m, name, index, TIKU_MSG_T_DOUBLE);
    uint64_t bits;

    if (v == NULL || v->len != 8u || out == NULL) {
        return 0;
    }
    bits = dec64((const unsigned char *)v->data);
    memcpy(out, &bits, sizeof *out);
    return 1;
}

int
tiku_msg_find_point(const tiku_msg_t *m, const char *name,
                         int index, float *x, float *y)
{
    const val_t *v = value_of(m, name, index, TIKU_MSG_T_POINT);
    const unsigned char *p;

    if (v == NULL || v->len != 8u || x == NULL || y == NULL) {
        return 0;
    }
    p = (const unsigned char *)v->data;
    *x = dec_f32(p);
    *y = dec_f32(p + 4);
    return 1;
}

int
tiku_msg_find_rect(const tiku_msg_t *m, const char *name,
                        int index, tiku_msg_rect_t *out)
{
    const val_t *v = value_of(m, name, index, TIKU_MSG_T_RECT);
    const unsigned char *p;

    if (v == NULL || v->len != 16u || out == NULL) {
        return 0;
    }
    p = (const unsigned char *)v->data;
    out->x = dec_f32(p);
    out->y = dec_f32(p + 4);
    out->w = dec_f32(p + 8);
    out->h = dec_f32(p + 12);
    return 1;
}

/**
 * @brief A string value, which ends where it says it does.
 *
 * There is no check here that it is terminated, because there are only
 * two doors into a value of these types and both hold it: add_string and
 * add_ref write the terminator themselves, and unflatten turns away text
 * that arrives without one.  A third check here would be one no test
 * could ever reach, and an unreachable guard is a claim nobody can keep.
 */
static const char *
text_of(const tiku_msg_t *m, const char *name, int index,
        tiku_msg_type_t type)
{
    const val_t *v = value_of(m, name, index, type);

    return (v != NULL) ? (const char *)v->data : NULL;
}

const char *
tiku_msg_find_string(const tiku_msg_t *m, const char *name,
                          int index)
{
    return text_of(m, name, index, TIKU_MSG_T_STRING);
}

const char *
tiku_msg_find_ref(const tiku_msg_t *m, const char *name, int index)
{
    return text_of(m, name, index, TIKU_MSG_T_REF);
}

const void *
tiku_msg_find_blob(const tiku_msg_t *m, const char *name,
                        int index, size_t *len)
{
    const val_t *v = value_of(m, name, index, TIKU_MSG_T_BLOB);

    if (v == NULL) {
        return NULL;
    }
    if (len != NULL) {
        *len = v->len;
    }
    return v->data;
}

tiku_msg_t *
tiku_msg_find_message(const tiku_msg_t *m, const char *name,
                           int index)
{
    const val_t *v = value_of(m, name, index, TIKU_MSG_T_MESSAGE);

    if (v == NULL) {
        return NULL;
    }
    return tiku_msg_unflatten(v->data, v->len, NULL);
}

/*---------------------------------------------------------------------------*/
/* Looking at what is there                                                  */
/*---------------------------------------------------------------------------*/

int
tiku_msg_field_count(const tiku_msg_t *m)
{
    return (m != NULL) ? m->nfield : 0;
}

const char *
tiku_msg_field_at(const tiku_msg_t *m, int i,
                       tiku_msg_type_t *type, int *count)
{
    if (m == NULL || i < 0 || i >= m->nfield) {
        return NULL;
    }
    if (type != NULL) {
        *type = m->field[i].type;
    }
    if (count != NULL) {
        *count = m->field[i].nval;
    }
    return m->field[i].name;
}

int
tiku_msg_count(const tiku_msg_t *m, const char *name)
{
    const field_t *f = field_find(m, name);

    return (f != NULL) ? f->nval : 0;
}

int
tiku_msg_has(const tiku_msg_t *m, const char *name,
                  tiku_msg_type_t type)
{
    const field_t *f = field_find(m, name);

    return (f != NULL && f->type == type && f->nval > 0) ? 1 : 0;
}

int
tiku_msg_remove(tiku_msg_t *m, const char *name)
{
    field_t *f = field_find(m, name);
    int at;

    if (f == NULL) {
        return 0;
    }
    at = (int)(f - m->field);
    field_drop(f);
    if (at < m->nfield - 1) {
        memmove(&m->field[at], &m->field[at + 1],
                (size_t)(m->nfield - at - 1) * sizeof *m->field);
    }
    m->nfield--;
    return 1;
}

const char *
tiku_msg_type_name(tiku_msg_type_t type)
{
    switch (type) {
    case TIKU_MSG_T_BOOL:    return "bool";
    case TIKU_MSG_T_INT8:    return "int8";
    case TIKU_MSG_T_INT16:   return "int16";
    case TIKU_MSG_T_INT32:   return "int32";
    case TIKU_MSG_T_INT64:   return "int64";
    case TIKU_MSG_T_FLOAT:   return "float";
    case TIKU_MSG_T_DOUBLE:  return "double";
    case TIKU_MSG_T_STRING:  return "string";
    case TIKU_MSG_T_BLOB:    return "blob";
    case TIKU_MSG_T_POINT:   return "point";
    case TIKU_MSG_T_RECT:    return "rect";
    case TIKU_MSG_T_MESSAGE: return "message";
    case TIKU_MSG_T_REF:     return "ref";
    default:                 return "unknown";
    }
}

/*---------------------------------------------------------------------------*/
/* The flat form                                                             */
/*---------------------------------------------------------------------------*/

/** @brief @p a + @p b, or 0 if that would wrap.  Sizes come from a wire. */
static size_t
add_sz(size_t a, size_t b)
{
    if (a == 0u || (size_t)-1 - a < b) {
        return 0u;
    }
    return a + b;
}

size_t
tiku_msg_flat_size(const tiku_msg_t *m)
{
    size_t total = 12u;                  /* magic, what, field count */
    int i, j;

    if (m == NULL) {
        return 0u;
    }
    for (i = 0; i < m->nfield; i++) {
        const field_t *f = &m->field[i];

        total = add_sz(total, 10u + strlen(f->name));
        for (j = 0; j < f->nval; j++) {
            total = add_sz(total, add_sz(4u, f->val[j].len));
        }
        if (total == 0u) {
            return 0u;
        }
    }
    return total;
}

int
tiku_msg_flatten(const tiku_msg_t *m, void *buf, size_t max,
                      size_t *wrote)
{
    size_t need = tiku_msg_flat_size(m);
    unsigned char *p = (unsigned char *)buf;
    int i, j;

    if (m == NULL || buf == NULL || need == 0u || need > max) {
        return 0;
    }
    enc32(p, TIKU_MSG_MAGIC);
    enc32(p + 4, m->what);
    enc32(p + 8, (uint32_t)m->nfield);
    p += 12;

    for (i = 0; i < m->nfield; i++) {
        const field_t *f = &m->field[i];
        size_t n = strlen(f->name);

        enc16(p, (uint16_t)n);
        p += 2;
        memcpy(p, f->name, n);
        p += n;
        enc32(p, (uint32_t)f->type);
        enc32(p + 4, (uint32_t)f->nval);
        p += 8;

        for (j = 0; j < f->nval; j++) {
            enc32(p, (uint32_t)f->val[j].len);
            p += 4;
            if (f->val[j].len > 0u) {
                memcpy(p, f->val[j].data, f->val[j].len);
                p += f->val[j].len;
            }
        }
    }
    if (wrote != NULL) {
        *wrote = need;
    }
    return 1;
}

/**
 * @brief Hang @p len bytes off @p f without copying them twice.
 *
 * The unflatten path already knows the bytes are inside the buffer; this
 * takes them once, straight from it.
 */
static int
take(field_t *f, const unsigned char *src, size_t len)
{
    void *copy;

    if (!grow((void **)&f->val, &f->cap, f->nval + 1, sizeof *f->val)) {
        return 0;
    }
    copy = malloc(len > 0u ? len : 1u);
    if (copy == NULL) {
        return 0;
    }
    if (len > 0u) {
        memcpy(copy, src, len);
    }
    f->val[f->nval].data = copy;
    f->val[f->nval].len = len;
    f->nval++;
    return 1;
}

tiku_msg_t *
tiku_msg_unflatten(const void *buf, size_t len, size_t *read)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t at = 12u;
    uint32_t nfield, i;
    tiku_msg_t *m;

    if (buf == NULL || len < 12u || dec32(p) != TIKU_MSG_MAGIC) {
        return NULL;
    }
    nfield = dec32(p + 8);

    /*
     * The buffer bounds the count better than any number picked here
     * could: a field cannot be told in fewer than ten bytes, so a header
     * claiming more than that many is claiming something the bytes in
     * hand cannot hold, whatever it says.
     *
     * The walk below would refuse it too, when it ran out of bytes.  This
     * refuses it now instead, so a header claiming ten million fields
     * costs one division rather than ten million reads -- which is the
     * difference between a bad message and a way to spend an afternoon of
     * somebody else's device.
     */
    if ((size_t)nfield > (len - 12u) / 10u) {
        return NULL;
    }

    m = tiku_msg_new(dec32(p + 4));
    if (m == NULL) {
        return NULL;
    }

    /*
     * Room is taken a field at a time, as bytes are read, and never up
     * front from the count above -- a count is something the buffer says,
     * and nothing is allocated on what a buffer says.
     */
    for (i = 0; i < nfield; i++) {
        uint32_t type, nval, j;
        size_t namelen, want;
        const char *name;
        field_t *f;

        if (len - at < 2u) {
            goto bad;
        }
        namelen = dec16(p + at);
        at += 2u;
        if (namelen == 0u || len - at < namelen) {
            goto bad;               /* a field with no name is not one */
        }
        name = (const char *)(p + at);
        if (memchr(name, '\0', namelen) != NULL) {
            goto bad;               /* a name with a hole in it */
        }
        at += namelen;

        if (len - at < 8u) {
            goto bad;
        }
        type = dec32(p + at);
        nval = dec32(p + at + 4);
        at += 8u;
        if ((size_t)nval > (len - at) / 4u) {
            goto bad;               /* more values than bytes to hold them */
        }

        f = field_add(m, name, namelen, (tiku_msg_type_t)type);
        if (f == NULL) {
            goto bad;
        }
        want = fixed_size((tiku_msg_type_t)type);

        for (j = 0; j < nval; j++) {
            size_t vlen;

            if (len - at < 4u) {
                goto bad;
            }
            vlen = dec32(p + at);
            at += 4u;
            if (len - at < vlen) {
                goto bad;
            }
            if (want != 0u && vlen != want) {
                goto bad;           /* a number that is the wrong size */
            }
            if ((type == (uint32_t)TIKU_MSG_T_STRING ||
                 type == (uint32_t)TIKU_MSG_T_REF) &&
                (vlen == 0u || p[at + vlen - 1u] != '\0')) {
                goto bad;           /* text that does not end */
            }
            if (!take(f, p + at, vlen)) {
                goto bad;
            }
            at += vlen;
        }
    }

    if (read != NULL) {
        *read = at;
    }
    return m;

bad:
    tiku_msg_free(m);
    return NULL;
}

/*
 * The copy goes out through the flat form and back in.  It is the same
 * work either way, and this way there is one description of what a field
 * is made of rather than two -- including for a field of a type this
 * build has never heard of, which is carried across whole because the
 * flat form carries it whole.
 */
tiku_msg_t *
tiku_msg_copy(const tiku_msg_t *m)
{
    size_t n = tiku_msg_flat_size(m);
    unsigned char *flat;
    tiku_msg_t *out;

    if (n == 0u) {
        return NULL;
    }
    flat = (unsigned char *)malloc(n);
    if (flat == NULL) {
        return NULL;
    }
    out = tiku_msg_flatten(m, flat, n, NULL)
              ? tiku_msg_unflatten(flat, n, NULL) : NULL;
    free(flat);
    return out;
}
