/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_image_desc.h - Flash application identity.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32S31_IMAGE_DESC_H_
#define TIKU_ESP32S31_IMAGE_DESC_H_
/** @brief Execute from mapped flash and return its application name. */
const char *tiku_s31_image_project(void);
#endif
