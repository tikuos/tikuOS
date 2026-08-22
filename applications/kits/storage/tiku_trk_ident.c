/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_ident.c - scored device identity.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_ident.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/** @brief FNV-1a over the fields that stand in for a missing die id. */
static uint64_t
ident_hash(const tiku_trk_identity_t *id)
{
    static const char sep = '\x1f';
    uint64_t h = 1469598103934665603ULL;
    const char *parts[3];
    int i;

    parts[0] = id->id;
    parts[1] = id->mcu;
    parts[2] = id->name;
    for (i = 0; i < 3; i++) {
        const char *p = parts[i];
        while (*p != '\0') {
            h ^= (unsigned char)*p++;
            h *= 1099511628211ULL;
        }
        h ^= (unsigned char)sep;
        h *= 1099511628211ULL;
    }
    return h;
}

int
tiku_trk_ident_provisional(const tiku_trk_identity_t *id)
{
    return (id == NULL || id->uid[0] == '\0');
}

int
tiku_trk_ident_key(const tiku_trk_identity_t *id, char *out, size_t max)
{
    if (id == NULL || out == NULL || max == 0u) {
        return 0;
    }
    if (!tiku_trk_ident_provisional(id)) {
        return snprintf(out, max, "uid-%s", id->uid);
    }
    return snprintf(out, max, "prov-%016llx",
                    (unsigned long long)ident_hash(id));
}

/** @brief Both present and equal. */
static int
same(const char *a, const char *b)
{
    return (a[0] != '\0' && b[0] != '\0' && strcmp(a, b) == 0);
}

/** @brief Both present and different. */
static int
differ(const char *a, const char *b)
{
    return (a[0] != '\0' && b[0] != '\0' && strcmp(a, b) != 0);
}

int
tiku_trk_ident_score(const tiku_trk_identity_t *known,
                     const tiku_trk_identity_t *seen)
{
    int s = 0;

    if (known == NULL || seen == NULL) {
        return 0;
    }
    /* The veto: a die identifier that disagrees settles the question, no
     * matter how much of the soft identity coincides. */
    if (differ(known->uid, seen->uid)) {
        return -1;
    }
    if (same(known->uid,  seen->uid))  { s += TIKU_TRK_W_UID;  }
    if (same(known->id,   seen->id))   { s += TIKU_TRK_W_ID;   }
    if (same(known->mcu,  seen->mcu))  { s += TIKU_TRK_W_MCU;  }
    if (same(known->name, seen->name)) { s += TIKU_TRK_W_NAME; }
    return s;
}

int
tiku_trk_ident_matches(const tiku_trk_identity_t *known,
                       const tiku_trk_identity_t *seen)
{
    int s = tiku_trk_ident_score(known, seen);

    return (s >= TIKU_TRK_BIND) ? 1 : 0;
}
