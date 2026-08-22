/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ident.h - the one device identity, keyed on by everything durable.
 *
 * A device is recognised by a scored match over the identity nodes it already
 * publishes, modelled on Tracker's MatchArchivedVolume.  Every store keys on
 * the string this produces, so there is exactly one spelling of "which board
 * is this".
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_IDENT_H_
#define TIKU_IDENT_H_

#include <stddef.h>

#define TIKU_DEVID_MAX 40

/** @brief The identity facts read from a device, any of them possibly empty. */
typedef struct {
    char uid[40];      /* /sys/device/uid  -- a die identifier where present */
    char id[40];       /* /sys/device/id                                     */
    char mcu[32];      /* /sys/device/mcu                                    */
    char name[40];     /* /sys/device/name -- user-settable, so weak         */
} tiku_identity_t;

/** @brief Weights: a uid alone binds; without one, id+mcu+name together do. */
#define TIKU_W_UID   8
#define TIKU_W_ID    4
#define TIKU_W_MCU   2
#define TIKU_W_NAME  1
#define TIKU_BIND    7

/**
 * @brief Canonical device key: "uid-<hex>" when a uid exists, else "prov-<hex>".
 *
 * The provisional form is derived from the weaker fields and cannot be trusted
 * to distinguish two identical boards; callers must surface that (see
 * tiku_ident_provisional()).
 *
 * @return Bytes written, excluding the terminator.
 */
int tiku_ident_key(const tiku_identity_t *id, char *out, size_t max);

/** @brief Whether the key is a guess rather than a die identifier. */
int tiku_ident_provisional(const tiku_identity_t *id);

/**
 * @brief Score @p seen against a remembered @p known identity.
 *
 * A uid that differs is an absolute veto however much else matches: without
 * that rule a renamed board would adopt another board's arrangement.
 *
 * @return 0..15, or -1 for the veto.
 */
int tiku_ident_score(const tiku_identity_t *known,
                         const tiku_identity_t *seen);

/** @brief Whether @p seen should be treated as @p known. */
int tiku_ident_matches(const tiku_identity_t *known,
                           const tiku_identity_t *seen);

#endif /* TIKU_IDENT_H_ */
