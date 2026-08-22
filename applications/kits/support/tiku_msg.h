/*
 * Tiku Desktop -- graphical interface to TikuOS devices.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_msg.h - a message that describes itself.
 *
 * Everything else on the wire here is a struct both ends were compiled
 * against: small and quick, and a hard contract.  That contract holds
 * while the two ends are one build.  It stops holding the moment a
 * device on the end of a cable is running last month's firmware, which
 * is the whole direction this interface is being built for.
 *
 * So: a message carries its own shape.  A code saying what it is, and
 * under it any number of NAMED fields, each with a type and one or more
 * values.  A reader asks for what it knows by name, and what it does not
 * know it steps over -- which is the version tolerance the fixed structs
 * cannot offer, and the reason this exists.
 *
 * It is Be's BMessage in shape, and deliberately not in four places:
 *
 *   - every value carries its LENGTH, so a reader can skip a type it has
 *     never heard of.  A BMessage reader that meets an unknown type is
 *     lost; this one is merely incurious.
 *   - the flat form is little-endian throughout, stated byte by byte, so
 *     a host and a device of different architectures agree.
 *   - the flat size can be asked for before writing, so the hot paths
 *     can flatten into a buffer they already own.
 *   - nothing here instantiates a class named in the data.  Be's
 *     archiving did, and a message that arrives over a wire is not
 *     something to hand a class name to.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MSG_H_
#define TIKU_MSG_H_

#include <stddef.h>
#include <stdint.h>

/** @brief What a value is.  The number is part of the wire format. */
typedef enum {
    TIKU_MSG_T_BOOL    = 1,
    TIKU_MSG_T_INT8    = 2,
    TIKU_MSG_T_INT16   = 3,
    TIKU_MSG_T_INT32   = 4,
    TIKU_MSG_T_INT64   = 5,
    TIKU_MSG_T_FLOAT   = 6,
    TIKU_MSG_T_DOUBLE  = 7,
    TIKU_MSG_T_STRING  = 8,     /* UTF-8, stored with its terminator     */
    TIKU_MSG_T_BLOB    = 9,     /* bytes, meaning left to the two ends   */
    TIKU_MSG_T_POINT   = 10,    /* two floats                            */
    TIKU_MSG_T_RECT    = 11,    /* four floats: x, y, w, h               */
    TIKU_MSG_T_MESSAGE = 12,    /* another message, whole                */
    TIKU_MSG_T_REF     = 13     /* a path, which is what a ref is here   */
} tiku_msg_type_t;

typedef struct tiku_msg tiku_msg_t;

/** @brief A rectangle as a message carries one. */
typedef struct {
    float x, y, w, h;
} tiku_msg_rect_t;

/*---------------------------------------------------------------------------*/
/* Making and unmaking                                                       */
/*---------------------------------------------------------------------------*/

/** @brief An empty message whose code is @p what.  NULL if out of room. */
tiku_msg_t *tiku_msg_new(uint32_t what);

/** @brief Give back everything @p m holds, nested messages included. */
void tiku_msg_free(tiku_msg_t *m);

/** @brief Empty it of fields, keeping the code and the room. */
void tiku_msg_clear(tiku_msg_t *m);

/** @brief A message with the same code and the same fields.  NULL on fail. */
tiku_msg_t *tiku_msg_copy(const tiku_msg_t *m);

uint32_t tiku_msg_what(const tiku_msg_t *m);
void tiku_msg_set_what(tiku_msg_t *m, uint32_t what);

/*---------------------------------------------------------------------------*/
/* Putting things in                                                         */
/*                                                                           */
/* Adding a name that is already there appends another value to it, so a     */
/* field is a list whenever it is asked to be.  Every one returns 1 for      */
/* done and 0 for out of room or a bad argument.                             */
/*---------------------------------------------------------------------------*/

int tiku_msg_add_bool(tiku_msg_t *m, const char *name, int v);
int tiku_msg_add_int8(tiku_msg_t *m, const char *name, int8_t v);
int tiku_msg_add_int16(tiku_msg_t *m, const char *name, int16_t v);
int tiku_msg_add_int32(tiku_msg_t *m, const char *name, int32_t v);
int tiku_msg_add_int64(tiku_msg_t *m, const char *name, int64_t v);
int tiku_msg_add_float(tiku_msg_t *m, const char *name, float v);
int tiku_msg_add_double(tiku_msg_t *m, const char *name, double v);
int tiku_msg_add_string(tiku_msg_t *m, const char *name,
                             const char *v);
int tiku_msg_add_blob(tiku_msg_t *m, const char *name,
                           const void *bytes, size_t len);
int tiku_msg_add_point(tiku_msg_t *m, const char *name,
                            float x, float y);
int tiku_msg_add_rect(tiku_msg_t *m, const char *name,
                           tiku_msg_rect_t r);
int tiku_msg_add_ref(tiku_msg_t *m, const char *name,
                          const char *path);

/**
 * @brief Put @p sub under @p name.
 *
 * The sub-message is COPIED in: the caller keeps ownership of what it
 * passed, and may free it at once.
 */
int tiku_msg_add_message(tiku_msg_t *m, const char *name,
                              const tiku_msg_t *sub);

/*---------------------------------------------------------------------------*/
/* Taking things out                                                         */
/*                                                                           */
/* @p index picks among a field's values, 0 for the first.  Every one        */
/* returns 1 when it found what was asked for, and leaves @p out alone       */
/* otherwise -- a field that is missing, of another type, or short of that   */
/* index all read the same way, because to the caller they are the same      */
/* thing: this message does not tell me that.                                */
/*---------------------------------------------------------------------------*/

int tiku_msg_find_bool(const tiku_msg_t *m, const char *name,
                            int index, int *out);
int tiku_msg_find_int8(const tiku_msg_t *m, const char *name,
                            int index, int8_t *out);
int tiku_msg_find_int16(const tiku_msg_t *m, const char *name,
                             int index, int16_t *out);
int tiku_msg_find_int32(const tiku_msg_t *m, const char *name,
                             int index, int32_t *out);
int tiku_msg_find_int64(const tiku_msg_t *m, const char *name,
                             int index, int64_t *out);
int tiku_msg_find_float(const tiku_msg_t *m, const char *name,
                             int index, float *out);
int tiku_msg_find_double(const tiku_msg_t *m, const char *name,
                              int index, double *out);
int tiku_msg_find_point(const tiku_msg_t *m, const char *name,
                             int index, float *x, float *y);
int tiku_msg_find_rect(const tiku_msg_t *m, const char *name,
                            int index, tiku_msg_rect_t *out);

/**
 * @brief The string under @p name.
 *
 * @return a pointer INTO the message, good until the message changes or
 *         goes away, or NULL.  Nothing is copied.
 */
const char *tiku_msg_find_string(const tiku_msg_t *m,
                                      const char *name, int index);

/** @brief The path under @p name, or NULL.  As above: not copied. */
const char *tiku_msg_find_ref(const tiku_msg_t *m,
                                   const char *name, int index);

/**
 * @brief The bytes under @p name, and how many.
 *
 * @return a pointer INTO the message, or NULL.  Nothing is copied.
 */
const void *tiku_msg_find_blob(const tiku_msg_t *m,
                                    const char *name, int index,
                                    size_t *len);

/**
 * @brief The message under @p name, built fresh.
 *
 * @return a message the CALLER frees, or NULL.
 */
tiku_msg_t *tiku_msg_find_message(const tiku_msg_t *m,
                                            const char *name, int index);

/*---------------------------------------------------------------------------*/
/* Looking at what is there                                                  */
/*                                                                           */
/* What makes a message worth having over a struct: it can be asked what it  */
/* holds.  A log, a test, a bridge to another protocol can all walk one      */
/* without knowing anything about it beforehand.                             */
/*---------------------------------------------------------------------------*/

/** @brief How many named fields. */
int tiku_msg_field_count(const tiku_msg_t *m);

/**
 * @brief The @p i th field's name, and what is under it.
 *
 * @param type Filled with the field's type; may be NULL.
 * @param count Filled with how many values it has; may be NULL.
 * @return the name, or NULL past the end.
 */
const char *tiku_msg_field_at(const tiku_msg_t *m, int i,
                                   tiku_msg_type_t *type, int *count);

/** @brief How many values @p name has (0 when it is not there at all). */
int tiku_msg_count(const tiku_msg_t *m, const char *name);

/** @brief Whether @p name is there, and of @p type.  0 for either no. */
int tiku_msg_has(const tiku_msg_t *m, const char *name,
                      tiku_msg_type_t type);

/** @brief Take @p name out altogether.  @return 1 when there was one. */
int tiku_msg_remove(tiku_msg_t *m, const char *name);

/** @brief What a type is called, for a log or a failing test to say. */
const char *tiku_msg_type_name(tiku_msg_type_t type);

/*---------------------------------------------------------------------------*/
/* The flat form                                                             */
/*                                                                           */
/*   u32 magic 'TKMS' | u32 what | u32 field count                           */
/*   per field:  u16 name length | name | u32 type | u32 value count         */
/*               per value:  u32 length | bytes                              */
/*                                                                           */
/* Little-endian throughout, written a byte at a time, so the bytes mean the */
/* same on both ends of a cable whatever they are made of.  Every value      */
/* carries its length, which is what lets a reader step over a field whose   */
/* type it has never met.                                                    */
/*---------------------------------------------------------------------------*/

/** @brief The magic four bytes a flattened message opens with. */
#define TIKU_MSG_MAGIC 0x534D4B54u      /* 'TKMS', little-endian */

/** @brief How many bytes @p m would flatten to.  0 for NULL. */
size_t tiku_msg_flat_size(const tiku_msg_t *m);

/**
 * @brief Write @p m into @p buf.
 *
 * @param max How much room @p buf has.
 * @param wrote Filled with how much was used; may be NULL.
 * @return 1 when it fitted and was written, 0 otherwise.
 */
int tiku_msg_flatten(const tiku_msg_t *m, void *buf, size_t max,
                          size_t *wrote);

/**
 * @brief Read a message out of @p buf.
 *
 * Every length in the buffer is checked against what is left of it before
 * it is believed: these bytes arrive from a wire, and a message must
 * never be a way in.
 *
 * @param read Filled with how many bytes the message took; may be NULL.
 * @return a message the caller frees, or NULL if the bytes are not one.
 */
tiku_msg_t *tiku_msg_unflatten(const void *buf, size_t len,
                                         size_t *read);

#endif /* TIKU_MSG_H_ */
