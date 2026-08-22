/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_fontdir.h - the faces a person dropped in.
 *
 * A folder is the interface: drop a .ttf in it and the family appears in
 * the Fonts window, the way art dropped in the icons folder appears on
 * whatever it names.  Files are grouped by the family they call
 * themselves, not by what they are called, so "DejaVuSans.ttf" and
 * "DejaVuSans-Bold.ttf" are one entry with two weights.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FONTDIR_H_
#define TIKU_TRK_FONTDIR_H_

#include "tiku_trk_model.h"
#include "tiku_trk_state.h"

/** @brief How many families the folder may offer. */
#define TIKU_TRK_FONTDIR_MAX 16

typedef struct {
    char name[64];
    char regular[TIKU_TRK_PATH_MAX];
    char bold[TIKU_TRK_PATH_MAX];
} tiku_trk_fontdir_entry_t;

typedef struct {
    tiku_trk_fontdir_entry_t font[TIKU_TRK_FONTDIR_MAX];
    int                      count;
} tiku_trk_fontdir_t;

/**
 * @brief Every face in @p dir, grouped by family and named in order.
 *
 * @return how many families were found.
 */
int tiku_trk_fontdir_scan(tiku_trk_fontdir_t *out, const char *dir);

/** @brief The entry called @p name, or NULL. */
const tiku_trk_fontdir_entry_t *tiku_trk_fontdir_find(
    const tiku_trk_fontdir_t *dir, const char *name);

#endif /* TIKU_TRK_FONTDIR_H_ */
