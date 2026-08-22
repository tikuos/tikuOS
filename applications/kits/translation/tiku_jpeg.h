/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpeg.h - baseline JPEG decoding.
 *
 * Most pictures on a real disk are JPEGs, so a thumbnailer that could not
 * read one would be a thumbnailer for screenshots.  Baseline sequential
 * only: progressive is a different scan structure and says so rather than
 * producing half a picture.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_JPEG_H_
#define TIKU_JPEG_H_

#include <stdint.h>

/**
 * @brief Decode a baseline JPEG into 0xAARRGGBB pixels.
 *
 * @param out  Receives a malloc'd buffer of w*h pixels; the caller frees.
 * @return 0 on success, 1 when the file is a JPEG this cannot decode
 *         (progressive, arithmetic, 12-bit), -1 when it is malformed.
 */
int tiku_jpeg_decode(const unsigned char *src, long n, int *w, int *h,
                         uint32_t **out);

#endif /* TIKU_JPEG_H_ */
