/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_media.c - this port's medium descriptor as one constant.
 *
 * The four facts of arch/<port>/tiku_nvm_media_arch.h, read through
 * tiku_nvm_media() by code that takes a descriptor at run time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/memory/tiku_nvm_media.h"

static const tiku_nvm_media_t port_media = TIKU_NVM_MEDIA_INIT;

const tiku_nvm_media_t *tiku_nvm_media(void)
{
    return &port_media;
}
