/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * custom_support.h - Refuse heap allocation inside the Opus library.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OPUS_CUSTOM_SUPPORT_H_
#define TIKU_OPUS_CUSTOM_SUPPORT_H_
#include <stddef.h>
#define OVERRIDE_OPUS_ALLOC
#define OVERRIDE_OPUS_REALLOC
#define OVERRIDE_OPUS_FREE
#define OVERRIDE_OPUS_ALLOC_SCRATCH
static inline void *opus_alloc(size_t n) { (void)n; return NULL; }
static inline void *opus_realloc(void *p,size_t n) { (void)p;(void)n;return NULL; }
static inline void opus_free(void *p) { (void)p; }
static inline void *opus_alloc_scratch(size_t n) { (void)n; return NULL; }
#endif
