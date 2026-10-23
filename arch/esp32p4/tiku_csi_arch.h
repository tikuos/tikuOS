/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_csi_arch.h - P4 MIPI CSI-2 receiver: the D-PHY's supply and clocks,
 * its lane rate, the host controller's lanes, and the error counters a
 * capture reads back.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_CSI_ARCH_H_
#define TIKU_ESP32P4_CSI_ARCH_H_
#include <stdint.h>

#define TIKU_P4_CSI_OK          0
#define TIKU_P4_CSI_ERR_PARAM  (-1)    /**< lanes or rate out of range */
#define TIKU_P4_CSI_ERR_POWER  (-2)    /**< the D-PHY supply did not come up */

/** @brief Lane rates the D-PHY takes, in Mbit/s a lane. */
#define TIKU_P4_CSI_MBPS_MIN    90u
#define TIKU_P4_CSI_MBPS_MAX    1500u

/** @brief PHY_STOPSTATE bits: data lane 0, data lane 1, the clock lane;
 *         set while the lane idles in LP-11. */
#define TIKU_P4_CSI_STOP_D0     (1u << 0)
#define TIKU_P4_CSI_STOP_D1     (1u << 1)
#define TIKU_P4_CSI_STOP_CLK    (1u << 16)

/** @brief What the host controller reported since the last read. */
struct tiku_p4_csi_status {
    uint32_t main;              /**< INT_ST_MAIN: which groups below fired */
    uint32_t phy_fatal;         /**< start-of-transmission errors */
    uint32_t pkt_fatal;         /**< header ECC errors past correction */
    uint32_t frame_fatal;       /**< frame boundary, sequence and CRC
                                 *   errors of the virtual channels, ORed */
    uint32_t pld_crc;           /**< payload CRC errors */
    uint32_t data_id;           /**< packets of a data type it refused */
    uint32_t ecc_corrected;     /**< header errors it corrected */
    uint32_t phy;               /**< escape and sync errors of the lanes */
    uint32_t stopstate;         /**< PHY_STOPSTATE when read */
};

/**
 * @brief Power the D-PHY (LDO VO3, 2.5 V), clock and reset the CSI bridge
 *        and the host behind it, set the PHY's band for @p lane_mbps and
 *        the host for @p lanes lanes, and release the PHY and the host.
 *
 * @return TIKU_P4_CSI_OK, TIKU_P4_CSI_ERR_PARAM for lanes other than 1 or 2
 *         or a rate outside the PHY's bands, TIKU_P4_CSI_ERR_POWER when the
 *         supply does not read back on.
 */
int tiku_p4_csi_start(uint8_t lanes, uint32_t lane_mbps);
/** @brief Hold the PHY and the host in reset and stop the host's clock. */
void tiku_p4_csi_stop(void);
/** @brief The D-PHY band code for @p lane_mbps, or 0xFF outside the bands. */
uint8_t tiku_p4_csi_hsfreq(uint32_t lane_mbps);
/** @brief Read, and with that clear, the host's error status into @p s. */
void tiku_p4_csi_status(struct tiku_p4_csi_status *s);
#endif
