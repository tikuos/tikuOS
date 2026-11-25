/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_peer.c - the board's peers: lookup, lifecycle calls, boot binding.
 *
 * Holds no state of its own: each call goes to the peer's driver, which
 * reads the state back from its carrier and controls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <interfaces/peer/tiku_peer.h>
#include <kernel/ipc/tiku_ipc_backend.h>
#include <string.h>

#if (TIKU_PEER_ENABLE + 0)

/** @brief Longest endpoint name the kernel takes, with its terminator. */
#define PEER_ENDPOINT_NAME  31u

const tiku_peer_t *
tiku_peer_get(uint8_t index)
{
    return (index < tiku_peer_count) ? tiku_peer_table[index]
                                     : (const tiku_peer_t *)0;
}

int
tiku_peer_find(const char *name)
{
    uint8_t i;

    if (name == (const char *)0) {
        return TIKU_PEER_ERR_NAME;
    }
    for (i = 0; i < tiku_peer_count; i++) {
        if (strcmp(tiku_peer_table[i]->name, name) == 0) {
            return (int)i;
        }
    }
    return TIKU_PEER_ERR_NAME;
}

int
tiku_peer_service(uint8_t index, const char *service)
{
    const tiku_peer_t *p = tiku_peer_get(index);
    uint8_t i;

    if (p == (const tiku_peer_t *)0 || service == (const char *)0) {
        return TIKU_PEER_ERR_NAME;
    }
    for (i = 0; i < p->service_count; i++) {
        if (strcmp(p->services[i].name, service) == 0) {
            return (int)i;
        }
    }
    return TIKU_PEER_ERR_NAME;
}

int
tiku_peer_start(uint8_t index)
{
    const tiku_peer_t *p = tiku_peer_get(index);

    if (p == (const tiku_peer_t *)0) {
        return TIKU_PEER_ERR_NAME;
    }
    if (p->ops->start == 0 || (p->flags & TIKU_PEER_F_SUPERVISOR) == 0u) {
        return TIKU_PEER_ERR_ROLE;
    }
    return (p->ops->start(p->context) == 0) ? TIKU_PEER_OK
                                            : TIKU_PEER_ERR_DRIVER;
}

int
tiku_peer_stop(uint8_t index)
{
    const tiku_peer_t *p = tiku_peer_get(index);

    if (p == (const tiku_peer_t *)0) {
        return TIKU_PEER_ERR_NAME;
    }
    if (p->ops->stop == 0 || (p->flags & TIKU_PEER_F_SUPERVISOR) == 0u) {
        return TIKU_PEER_ERR_ROLE;
    }
    if ((p->flags & TIKU_PEER_F_ONESHOT) != 0u) {
        return TIKU_PEER_ERR_STATE;
    }
    return (p->ops->stop(p->context) == 0) ? TIKU_PEER_OK
                                           : TIKU_PEER_ERR_DRIVER;
}

int
tiku_peer_reset(uint8_t index)
{
    const tiku_peer_t *p = tiku_peer_get(index);

    if (p == (const tiku_peer_t *)0) {
        return TIKU_PEER_ERR_NAME;
    }
    if (p->ops->reset == 0 || (p->flags & TIKU_PEER_F_SUPERVISOR) == 0u) {
        return TIKU_PEER_ERR_ROLE;
    }
    return (p->ops->reset(p->context) == 0) ? TIKU_PEER_OK
                                            : TIKU_PEER_ERR_DRIVER;
}

int
tiku_peer_status(uint8_t index, tiku_peer_status_t *out)
{
    const tiku_peer_t *p = tiku_peer_get(index);

    if (p == (const tiku_peer_t *)0 || out == (tiku_peer_status_t *)0) {
        return TIKU_PEER_ERR_NAME;
    }
    memset(out, 0, sizeof(*out));
    p->ops->status(p->context, out);
    return TIKU_PEER_OK;
}

int
tiku_peer_prepare_all(void)
{
    uint8_t i;

    for (i = 0; i < tiku_peer_count; i++) {
        const tiku_peer_t *p = tiku_peer_table[i];

        if (p->ops->prepare != 0 && p->ops->prepare(p->context) != 0) {
            return TIKU_PEER_ERR_DRIVER;
        }
    }
    return TIKU_PEER_OK;
}

int
tiku_peer_bind_all(void)
{
    uint8_t i;

    for (i = 0; i < tiku_peer_count; i++) {
        const tiku_peer_t *p = tiku_peer_table[i];
        char want[PEER_ENDPOINT_NAME];
        size_t pn = strlen(p->name);
        size_t sn;
        uint16_t e;

        if (p->service_count == 0u) {
            return TIKU_PEER_ERR_NAME;
        }
        sn = strlen(p->services[0].name);
        if (pn + 1u + sn >= sizeof(want)) {
            return TIKU_PEER_ERR_NAME;
        }
        memcpy(want, p->name, pn);
        want[pn] = '/';
        memcpy(want + pn + 1u, p->services[0].name, sn + 1u);
        for (e = 0; e < tiku_ipc_backend_count; e++) {
            if (strcmp(tiku_ipc_backend_table[e].name, want) == 0) {
                break;
            }
        }
        if ((uint32_t)e + p->service_count > tiku_ipc_backend_count) {
            return TIKU_PEER_ERR_NAME;
        }
        if (p->ops->bind != 0 && p->ops->bind(p->context, e) != 0) {
            return TIKU_PEER_ERR_DRIVER;
        }
    }
    return TIKU_PEER_OK;
}

int
tiku_peer_start_all(void)
{
    uint8_t i;
    int rc = TIKU_PEER_OK;

    for (i = 0; i < tiku_peer_count; i++) {
        if ((tiku_peer_table[i]->flags & TIKU_PEER_F_AUTOSTART) != 0u) {
            int r = tiku_peer_start(i);

            if (r != TIKU_PEER_OK) {
                rc = r;
            }
        }
    }
    return rc;
}

#endif /* TIKU_PEER_ENABLE */
