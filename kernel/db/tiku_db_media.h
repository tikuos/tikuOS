/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_db_media.h - what the database engine knows about its medium.
 *
 * A port fills one descriptor: the program granule, what a power cut during
 * a program leaves in that granule, and the programs a granule is rated for.
 * Every alignment and wear rule in the engine derives from it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DB_MEDIA_H_
#define TIKU_DB_MEDIA_H_

#include <stdint.h>

/**
 * @brief What a power cut during a program leaves in the line being written.
 */
typedef enum {
    TIKU_DB_TEAR_NONE = 0,   /**< the line lands whole or stays as it was     */
    TIKU_DB_TEAR_BYTE = 1,   /**< a prefix of the bytes written has landed    */
    TIKU_DB_TEAR_LINE = 2    /**< every byte of the line may read as anything */
} tiku_db_tear_t;

/**
 * @brief The medium under one database extent.
 *
 * `line` is the program granule: a 16-byte line on RRAM and MRAM, the
 * architecture word on FRAM.  A record never shares a line with another.
 */
typedef struct {
    uint16_t line;        /**< program granule in bytes, a power of two, 2 to 64 */
    uint8_t  tear;        /**< tiku_db_tear_t                                   */
    uint8_t  reserved;
    uint32_t endurance;   /**< programs one line is rated for; 0 means no limit */
} tiku_db_media_t;

#endif /* TIKU_DB_MEDIA_H_ */
