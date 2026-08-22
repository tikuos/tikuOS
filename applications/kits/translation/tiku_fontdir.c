/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fontdir.c - the faces a person dropped in (see the header).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fontdir.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "tiku_ttf.h"

/** @brief The entry for @p name, making one if there is room. */
static tiku_fontdir_entry_t *
slot_for(tiku_fontdir_t *out, const char *name)
{
    int i;

    for (i = 0; i < out->count; i++) {
        if (strcmp(out->font[i].name, name) == 0) {
            return &out->font[i];
        }
    }
    if (out->count >= TIKU_FONTDIR_MAX) {
        return NULL;
    }
    {
        tiku_fontdir_entry_t *e = &out->font[out->count++];

        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%s", name);
        return e;
    }
}

int
tiku_fontdir_scan(tiku_fontdir_t *out, const char *dir)
{
    struct dirent *entry;
    DIR *d;

    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    if (dir == NULL || dir[0] == '\0') {
        return 0;
    }
    d = opendir(dir);
    if (d == NULL) {
        return 0;               /* no folder yet is not an error */
    }
    while ((entry = readdir(d)) != NULL) {
        char path[TIKU_PATH_MAX];
        tiku_ttf_t *ttf;
        tiku_fontdir_entry_t *slot;
        const char *family;

        if (entry->d_name[0] == '.') {
            continue;
        }
        if ((size_t)snprintf(path, sizeof path, "%s/%s", dir,
                             entry->d_name) >= sizeof path) {
            continue;
        }
        /* By what the file HOLDS: the folder is a drop target, so it will
         * have a stray README in it sooner or later. */
        if (!tiku_ttf_is_font(path)) {
            continue;
        }
        ttf = tiku_ttf_open(path);
        if (ttf == NULL) {
            continue;
        }
        family = tiku_ttf_family(ttf);
        slot = (family != NULL && family[0] != '\0') ? slot_for(out, family)
                                                     : NULL;
        if (slot != NULL) {
            char *into = tiku_ttf_bold(ttf) ? slot->bold : slot->regular;

            if (into[0] == '\0') {
                snprintf(into, TIKU_PATH_MAX, "%s", path);
            }
        }
        tiku_ttf_close(ttf);
    }
    (void)closedir(d);

    {
        /* A family dropped in as its bold alone is still a family: it
         * would be worse to hide it than to draw both weights from it. */
        int i, j;

        for (i = 0; i < out->count; i++) {
            if (out->font[i].regular[0] == '\0') {
                snprintf(out->font[i].regular, TIKU_PATH_MAX, "%s",
                         out->font[i].bold);
            }
        }
        /* Named order, so the list a person learns is the list they see
         * again: a directory hands its entries back however it likes. */
        for (i = 1; i < out->count; i++) {
            tiku_fontdir_entry_t hold = out->font[i];

            for (j = i; j > 0 &&
                        strcmp(out->font[j - 1].name, hold.name) > 0; j--) {
                out->font[j] = out->font[j - 1];
            }
            out->font[j] = hold;
        }
    }
    return out->count;
}

const tiku_fontdir_entry_t *
tiku_fontdir_find(const tiku_fontdir_t *dir, const char *name)
{
    int i;

    if (dir == NULL || name == NULL || name[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < dir->count; i++) {
        if (strcmp(dir->font[i].name, name) == 0) {
            return &dir->font[i];
        }
    }
    return NULL;
}
