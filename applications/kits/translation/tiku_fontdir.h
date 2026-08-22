/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fontdir.h - the faces a person dropped in.
 *
 * A folder is the interface: drop a .ttf in it and the family appears in
 * the Fonts window, the way art dropped in the icons folder appears on
 * whatever it names.  Files are grouped by the family they call
 * themselves, not by what they are called, so "DejaVuSans.ttf" and
 * "DejaVuSans-Bold.ttf" are one entry with two weights.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FONTDIR_H_
#define TIKU_FONTDIR_H_

#include "tiku_model.h"
#include "tiku_state.h"

/** @brief How many families the folder may offer. */
#define TIKU_FONTDIR_MAX 16

typedef struct {
    char name[64];
    char regular[TIKU_PATH_MAX];
    char bold[TIKU_PATH_MAX];
} tiku_fontdir_entry_t;

typedef struct {
    tiku_fontdir_entry_t font[TIKU_FONTDIR_MAX];
    int                      count;
} tiku_fontdir_t;

/**
 * @brief Every face in @p dir, grouped by family and named in order.
 *
 * @return how many families were found.
 */
int tiku_fontdir_scan(tiku_fontdir_t *out, const char *dir);

/** @brief The entry called @p name, or NULL. */
const tiku_fontdir_entry_t *tiku_fontdir_find(
    const tiku_fontdir_t *dir, const char *name);

#endif /* TIKU_FONTDIR_H_ */
