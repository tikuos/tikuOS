/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * http://tinymcp.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mcp_tls.h - the board's TLS identity: its P-256 key, made on the
 * board and never read back, and the certificate the owner's CA signed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MCP_TLS_H_
#define TIKU_MCP_TLS_H_

#include <stddef.h>
#include <stdint.h>

#include <tikukits/net/tls/server/tiku_kits_crypto_tls_server.h>

#ifndef TIKU_MCP_TLS_CERT_MAX
#define TIKU_MCP_TLS_CERT_MAX 1024u  /**< DER bytes of the board's certificate */
#endif
#ifndef TIKU_MCP_TLS_REC
#define TIKU_MCP_TLS_REC 2560u       /**< a connection's TLS record buffer */
#endif

/** @brief The key and certificate cells, once at boot. */
void tiku_mcp_tls_init(void);

/**
 * @brief What an HTTPS connection proves the board with.
 * @return NULL until the board has a key and a certificate for it
 */
const tiku_kits_crypto_tls_server_cred_t *tiku_mcp_tls_cred(void);

/** @brief Handshake randomness: HMAC-DRBG output, never a TRNG read. */
void tiku_mcp_tls_rng(uint8_t *buf, size_t len);

/**
 * @brief Seed the generator from the TRNG on first use and after
 *        TIKU_MCP_TLS_RESEED outputs; call where no handshake waits.
 */
void tiku_mcp_tls_seed(void);

/**
 * /sys/mcp/tls/key: reads set or none; "new" makes a key from the TRNG,
 * "none" removes it, and either drops the certificate.
 */
int tiku_mcp_tls_key_read(char *buf, size_t max);
int tiku_mcp_tls_key_write(const char *buf, size_t len);

/** @brief /sys/mcp/tls/pub: the public point, 130 hex digits, or none. */
int tiku_mcp_tls_pub_read(char *buf, size_t max);

/**
 * /sys/mcp/tls/cert: reads none, staging, or the DER's length and SHA-256.
 * A write appends base64 in whole groups of four; the DER takes effect once
 * whole and naming the board's public key.  "none" removes it.
 */
int tiku_mcp_tls_cert_read(char *buf, size_t max);
int tiku_mcp_tls_cert_write(const char *buf, size_t len);

#endif /* TIKU_MCP_TLS_H_ */
