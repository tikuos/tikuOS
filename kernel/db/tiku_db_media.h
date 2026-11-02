/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_db_media.h - the engine's view of its medium: the kernel descriptor.
 *
 * The database takes a tiku_nvm_media_t (kernel/memory/tiku_nvm_media.h)
 * under these names.  A record never shares a unit with another, and every
 * alignment and wear rule in the engine derives from the descriptor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DB_MEDIA_H_
#define TIKU_DB_MEDIA_H_

#include "kernel/memory/tiku_nvm_media.h"

#define TIKU_DB_TEAR_NONE  TIKU_NVM_TEAR_NONE
#define TIKU_DB_TEAR_BYTE  TIKU_NVM_TEAR_BYTE
#define TIKU_DB_TEAR_LINE  TIKU_NVM_TEAR_LINE

typedef tiku_nvm_tear_t  tiku_db_tear_t;
typedef tiku_nvm_media_t tiku_db_media_t;

#endif /* TIKU_DB_MEDIA_H_ */
