/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_icons.h - the embedded HVIF icon set.
 *
 * The standard Tracker artwork, compiled in as HVIF blobs rather than
 * bitmaps: one vector definition serves every size, and the renderer
 * rasterises at the size actually asked for.  Names are the Tracker's own
 * vocabulary ("folder", "trash_full", "folder_queries"), not Haiku's
 * resource ids.
 *
 * Artwork from Haiku (haiku-os.org), MIT licensed; the TrackerIcons.rdef
 * blobs additionally carry the Open Tracker License (Be Incorporated,
 * 1991-2000).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef TIKU_ICONS_H_
#define TIKU_ICONS_H_

#include <stddef.h>
#include <stdint.h>

/** @brief How many icons are baked in. */
#define TIKU_ICON_COUNT 34

/** @brief How many more a directory may add on top of them. */
#define TIKU_ICON_EXTRA 32

/** @brief One named HVIF blob. */
typedef struct {
    const char    *name;
    const uint8_t *data;
    size_t         len;
} tiku_icon_t;

/** @brief Number of icons in the set; always TIKU_ICON_COUNT. */
int tiku_icons_count(void);

/** @brief The @p i th icon in set order, or NULL if @p i is out of range. */
const tiku_icon_t *tiku_icons_at(int i);

/**
 * @brief Look an icon up by short name.
 *
 * @param name One of the set's names, e.g. "folder" or "trash_full".
 * @return The blob, or NULL if nothing carries that name.
 */
const tiku_icon_t *tiku_icons_find(const char *name);

/**
 * @brief Take every icon in @p dir into the set, by its file name.
 *
 * A file called `folder.hvif` becomes the icon named `folder`, replacing
 * the baked one; a name nothing bakes is added.  This is how a program
 * or a device says what it looks like without a rebuild.
 *
 * @return how many files were taken.
 */
int tiku_icons_load_dir(const char *dir);

/**
 * @brief Render @p name at @p size x @p size, once per size.
 *
 * The result is premultiplied 0xAARRGGBB owned by the cache, valid until
 * tiku_icons_flush().  Callers blit it; they never free it.
 *
 * @param name Icon name.
 * @param size Edge length in pixels, 1..1024.
 * @return The bitmap, or NULL for an unknown name or a failed render.
 */
const uint32_t *tiku_icons_bitmap(const char *name, int size);

/**
 * @brief The selected look, cached beside the plain render (IV-019).
 *
 * Derived once from the plain art -- rgb blended toward @p wash by
 * @p mix/255 with alpha kept -- and stored, so a composite of a selected
 * icon costs what a plain one does.
 */
const uint32_t *tiku_icons_bitmap_washed(const char *name, int size,
                                             uint32_t wash, unsigned mix);

/** @brief How many selected looks have been DERIVED, ever: the once-ness. */
unsigned tiku_icons_washed_builds(void);

/**
 * @brief How many bytes of rendered icon the cache may hold (IV-012).
 *
 * A slot count alone does not bound anything: one 1024 px icon is four
 * megabytes, and a table of icons with four slots each would hold hundreds.
 * The oldest render goes when a new one would cross this.
 */
#define TIKU_ICONS_BUDGET (512u * 1024u)

/** @brief How many bytes of rendered icon are held right now. */
size_t tiku_icons_cached_bytes(void);

/** @brief Whether that exact render is still held, which says what went. */
int tiku_icons_cached(const char *name, int size);

/** @brief Drop every cached bitmap. */
void tiku_icons_flush(void);

#endif /* TIKU_ICONS_H_ */
