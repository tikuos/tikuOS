/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_model.c - the questions the view is allowed to ask.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_model.h"
#include "tiku_strings.h"
#include "tiku_state.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int
tiku_model_is_container(const tiku_model_t *m)
{
    if (m == NULL) {
        return 0;
    }
    switch (m->kind) {
    case TIKU_KIND_DIRECTORY:
    case TIKU_KIND_VOLUME:
    case TIKU_KIND_ROOT:
    case TIKU_KIND_VIRTUAL_DIR:
    case TIKU_KIND_TRASH:
    case TIKU_KIND_DESKTOP:
    case TIKU_KIND_QUERY:
        return 1;
    case TIKU_KIND_SYMLINK:
        /* ResolveIfLink's answer: a link to a folder sorts, opens and
         * takes drops as the folder it leads to; a broken one is a thing
         * to see, not a place to put something (MA-008). */
        return !m->facts.link_broken &&
               strcmp(m->type, "application/x-vnd.Be-directory") == 0;
    default:
        return 0;
    }
}

int
tiku_model_is_mutable_container(const tiku_model_t *m)
{
    if (!tiku_model_is_container(m)) {
        return 0;
    }
    /* A namespace directory has a fixed shape: nothing can be created,
     * renamed or pasted into /sys.  This single answer is what disables
     * New Folder, Paste and drop-targeting there. */
    if (m->kind == TIKU_KIND_VIRTUAL_DIR ||
        m->kind == TIKU_KIND_ROOT ||
        m->kind == TIKU_KIND_QUERY) {
        return 0;
    }
    return (m->facts.perm & TIKU_P_WRITE) ? 1 : 0;
}

const char *
tiku_model_display_name(const tiku_model_t *m)
{
    if (m == NULL) {
        return "";
    }
    switch (m->kind) {
    case TIKU_KIND_ROOT:    return tiku_str("kind.disks");
    case TIKU_KIND_TRASH:   return tiku_str("kind.trash");
    case TIKU_KIND_DESKTOP: return tiku_str("kind.desktop");
    default:                    return m->name;
    }
}

int
tiku_model_is_writable(const tiku_model_t *m)
{
    if (m == NULL) {
        return 0;
    }
    return (m->facts.perm & TIKU_P_WRITE) ? 1 : 0;
}

/**
 * @brief What a MIME-ish type says a thing is, and what it should wear.
 *
 * Walked twice -- once for an exact match, once for the supertype up to and
 * including the slash -- so "text/x-source-code" finds the text entry
 * without needing its own row.
 */
static const struct {
    const char *type;
    const char *kind;
    const char *icon;
} kTypes[] = {
    { "application/x-vnd.be-directory", "kind.folder",        "folder" },
    { "application/x-vnd.be-symlink",   "kind.symlink", "symlink" },
    { "application/x-vnd.be-volume",    "kind.volume",        "volume" },
    { "application/x-vnd.be-root",      "kind.disks",         "root" },
    { "application/x-vnd.be-query",     "kind.query",         "query" },
    { "application/x-vnd.be-trash",     "kind.trash",         "trash_empty" },
    { "application/x-vnd.be-app",       "kind.application",   "application" },
    /* No bare "application/" row: it would claim octet-stream, which is a
     * generic blob and not a program.  A real executable is recognised by
     * its permission bits before this table is consulted. */
    { "text/",                          "kind.text_file",     "text_file" },
    { "image/",                         "kind.image",         "file" },
    { "audio/",                         "kind.audio",         "file" },
    { "video/",                         "kind.video",         "file" }
};
#define NTYPES ((int)(sizeof kTypes / sizeof kTypes[0]))

const char *
tiku_model_type_at(int i, const char **label)
{
    if (i < 0 || i >= NTYPES) {
        return NULL;
    }
    if (label != NULL) {
        *label = tiku_str(kTypes[i].kind);
    }
    return kTypes[i].type;
}

/** @brief Case-folded comparison of @p n bytes. */
static int
fold_eq(const char *a, const char *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        int ca = a[i], cb = b[i];

        if (ca >= 'A' && ca <= 'Z') { ca += 32; }
        if (cb >= 'A' && cb <= 'Z') { cb += 32; }
        if (ca != cb || ca == '\0') {
            return (ca == cb);
        }
    }
    return 1;
}

/** @brief The table row @p type matches, or -1.  Exact beats supertype. */
static int type_row_walk(const char *type);

/**
 * @brief The table row @p type matches, memoised (IV-011).
 *
 * A type that has no row of its own is remembered AS SUCH, so the miss is
 * paid once -- the alias entry the source inserts after falling back, as a
 * four-slot memo over the walk.  Hits are remembered too: the memo is the
 * answer, whatever the answer was.
 */
static int
type_row(const char *type)
{
    static struct {
        char type[64];
        int  row;
    } memo[4];
    static int next;
    int i, row;

    if (type == NULL || type[0] == '\0') {
        return -1;
    }
    for (i = 0; i < 4; i++) {
        if (memo[i].type[0] != '\0' && strcmp(memo[i].type, type) == 0) {
            return memo[i].row;
        }
    }
    row = type_row_walk(type);
    if (strlen(type) < sizeof memo[next].type) {
        snprintf(memo[next].type, sizeof memo[next].type, "%s", type);
        memo[next].row = row;
        next = (next + 1) % 4;
    }
    return row;
}

static int
type_row_walk(const char *type)
{
    int i;
    const char *slash;

    if (type == NULL || type[0] == '\0') {
        return -1;
    }
    for (i = 0; i < NTYPES; i++) {
        size_t n = strlen(kTypes[i].type);

        if (kTypes[i].type[n - 1u] != '/' && strlen(type) == n &&
            fold_eq(type, kTypes[i].type, n)) {
            return i;
        }
    }
    slash = strchr(type, '/');
    if (slash == NULL) {
        return -1;
    }
    for (i = 0; i < NTYPES; i++) {
        size_t n = strlen(kTypes[i].type);

        if (kTypes[i].type[n - 1u] == '/' &&
            n == (size_t)(slash - type) + 1u &&
            fold_eq(type, kTypes[i].type, n)) {
            return i;
        }
    }
    return -1;
}

int
tiku_model_type_is(const char *type, const char *want)
{
    size_t n;

    if (want == NULL || want[0] == '\0') {
        return 1;               /* asking for nothing admits everything */
    }
    if (type == NULL || type[0] == '\0') {
        return 0;
    }
    n = strlen(want);
    if (fold_eq(type, want, n) && type[n] == '\0') {
        return 1;               /* the type itself */
    }
    /* A SUPERTYPE ends at the slash and admits everything under it, which
     * is what lets a table of a dozen entries answer for types it has
     * never heard of. */
    return (want[n - 1u] == '/' && fold_eq(type, want, n));
}

const char *
tiku_model_kind_string(const tiku_model_t *m)
{
    if (m == NULL) {
        return "";
    }
    /* The type is more specific than the kind, so it answers first: a file
     * is a "Text file" when it says so, and only a bare "File" when it does
     * not. */
    {
        int r = type_row(m->type);

        if (r >= 0 && m->kind == TIKU_KIND_FILE) {
            return tiku_str(kTypes[r].kind);
        }
    }
    switch (m->kind) {
    case TIKU_KIND_FILE:        return tiku_str("kind.file");
    case TIKU_KIND_DIRECTORY:   return tiku_str("kind.folder");
    case TIKU_KIND_SYMLINK:
        return m->facts.link_broken ? "Broken link" : "Symbolic link";
    case TIKU_KIND_VOLUME:      return tiku_str("kind.volume");
    case TIKU_KIND_ROOT:        return tiku_str("kind.disks");
    case TIKU_KIND_VIRTUAL_DIR: return tiku_str("kind.namespace_folder");
    case TIKU_KIND_DEVICE_NODE: return tiku_str("kind.device_node");
    case TIKU_KIND_QUERY:       return tiku_str("kind.query");
    case TIKU_KIND_PRINTER:     return tiku_str("kind.printer");
    case TIKU_KIND_TRASH:       return tiku_str("kind.trash");
    case TIKU_KIND_DESKTOP:     return tiku_str("kind.desktop");
    default:                        return tiku_str("kind.unknown");
    }
}

/** @brief Does @p path end in the component @p leaf. */
static int
leaf_is(const char *path, const char *leaf)
{
    const char *slash = strrchr(path, '/');

    return strcmp(slash != NULL ? slash + 1 : path, leaf) == 0;
}

const char *
tiku_model_icon_name(const tiku_model_t *m)
{
    if (m == NULL) {
        return "file";
    }
    switch (m->kind) {
    case TIKU_KIND_TRASH:
        /* Two icons, one kind: whether the Trash has anything in it is the
         * one piece of state the icon exists to show. */
        return m->facts.trash_full ? "trash_full" : "trash_empty";
    case TIKU_KIND_DESKTOP:
        return "desktop";
    case TIKU_KIND_SYMLINK:
        if (!m->facts.link_broken) {
            /* The TARGET's art: attributes and label stay the link's, only
             * the icon comes from what it points at (IV-042).  The model
             * already carries the target's nature in its type -- the
             * backend stats through a resolving link. */
            int r = type_row(m->type);

            if (r >= 0 && kTypes[r].icon[0] != '\0') {
                return kTypes[r].icon;
            }
            return "file";
        }
        /* Haiku's broken-link blob is byte-identical to its symlink one, so
         * there is no art to distinguish them and inventing some would be a
         * different icon set.  The Kind column carries the distinction, and
         * the drawing dims a broken row. */
        return "symlink";
    case TIKU_KIND_QUERY:
        return "query";
    case TIKU_KIND_VOLUME:
        /* A share is not a disk, and the art set has always carried a
         * separate blob for one that nothing ever selected (IV-006). */
        if (m->facts.vol_flags & TIKU_VOL_SHARED) {
            return "share";
        }
        return (m->facts.vol_flags & TIKU_VOL_BOOT) ? "root" : "volume";
    case TIKU_KIND_ROOT:
        return "root";
    case TIKU_KIND_PRINTER:
        return "printer";
    case TIKU_KIND_DEVICE_NODE:
        /* A live value has no counterpart in a file system, so it borrows the
         * plain document icon rather than pretending to be something it is
         * not; the Value column is what tells the user it is live. */
        return "file";
    case TIKU_KIND_VIRTUAL_DIR:
        /* Haiku ships an icon for exactly this idea -- a directory whose
         * contents are produced rather than stored. */
        return "folder_virtual";
    case TIKU_KIND_DIRECTORY:
        /* Tracker picks a special folder's icon by comparing node refs against
         * the ones it looked up at startup; with no such registry we match the
         * leaf name, which is right for the standard places and falls back to
         * the plain folder everywhere else. */
        if (leaf_is(m->path, "Desktop"))   { return "desktop"; }
        if (leaf_is(m->path, "Downloads")) { return "folder_downloads"; }
        if (leaf_is(m->path, "Documents")) { return "folder_people"; }
        if (leaf_is(m->path, "Mail"))      { return "folder_mail"; }
        if (leaf_is(m->path, "People"))    { return "folder_people"; }
        if (leaf_is(m->path, "Fonts"))     { return "folder_fonts"; }
        if (leaf_is(m->path, "queries"))   { return "folder_queries"; }
        if (leaf_is(m->path, "develop"))   { return "folder_develop"; }
        if (leaf_is(m->path, "config") ||
            leaf_is(m->path, ".config"))   { return "folder_config"; }
        if (leaf_is(m->path, "apps") ||
            leaf_is(m->path, "Applications")) { return "folder_apps"; }
        if (leaf_is(m->path, "preferences")) { return "folder_prefs"; }
        if (leaf_is(m->path, "system") ||
            leaf_is(m->path, "boot"))      { return "folder_system"; }
        {
            const char *home = getenv("HOME");
            if (home != NULL && strcmp(m->path, home) == 0) {
                return "home";
            }
        }
        return "folder";
    case TIKU_KIND_FILE:
    default:
        break;
    }
    if (m->facts.perm & TIKU_P_EXEC) {
        return "executable";
    }
    {
        /* Case-folded, and by supertype as well as exact: a type registry
         * that only matched lowercase exact strings would miss most of what
         * it is asked about. */
        int r = type_row(m->type);

        if (r >= 0) {
            return kTypes[r].icon;
        }
    }
    return "file";
}

void
tiku_backend_close(tiku_backend_t *b)
{
    if (b != NULL && b->ops != NULL && b->ops->close != NULL) {
        b->ops->close(b);
    }
}
