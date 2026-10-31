/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * config.h - Portable fixed-point Opus build with caller-owned scratch memory.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OPUS_CONFIG_H_
#define TIKU_OPUS_CONFIG_H_
#define OPUS_BUILD 1
#define FIXED_POINT 1
#define DISABLE_FLOAT_API 1
#define DISABLE_DEBUG_FLOAT 1
#define NONTHREADSAFE_PSEUDOSTACK 1
#define ENABLE_HARDENING 1
#define OVERRIDE_celt_fatal 1
#define CUSTOM_SUPPORT 1
#define HAVE_LRINT 1
#define HAVE_LRINTF 1
#define PACKAGE_VERSION "1.6.1"
#endif
