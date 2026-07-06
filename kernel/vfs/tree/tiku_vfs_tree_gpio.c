/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_gpio.c - /dev/gpio and /dev/gpio_dir VFS nodes
 *
 * Pin-level GPIO access through the filesystem:
 *
 *   /dev/gpio/<port>/<pin>  (rw) read level; write 0/1/t/i
 *   /dev/gpio_dir/<port>    (r)  eight-character direction summary
 *
 * VFS read/write handlers carry no user argument, so a pair of
 * common workers (gpio_pin_read/gpio_pin_write) is wrapped by
 * macro-generated per-pin functions that hardcode the port/pin
 * constants — 64 wrappers of ~10 bytes each on a 4-port device,
 * cheaper and simpler than threading a context pointer through the
 * node struct.  Ports are gated on the TIKU_DEVICE_HAS_PORTn
 * device macros so only real silicon shows up in the tree.
 *
 * Everything dispatches through interfaces/gpio/tiku_gpio.h, which
 * owns the port-register lookup tables and bounds checking.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_gpio.h"
#include <interfaces/gpio/tiku_gpio.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* /dev/gpio_dir — per-port direction summary                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Shared renderer for one port's direction summary.
 *
 * Renders one character per pin 0..7 followed by a newline:
 * 'O' output, 'I' input, '?' when the driver cannot determine the
 * direction (e.g. pin routed to a peripheral function).  Example
 * for a port with pins 2 and 3 as outputs:
 *
 *   "IIOOIIII\n"
 *
 * The pos < max-4 guard leaves room for the final character,
 * newline and terminator in degenerate tiny buffers.  Wrapped by
 * the GPIO_DIR() macro below so each port gets a plain
 * tiku_vfs_read_fn.
 *
 * @param port  Port number (1-based, matching MSP430 P1..P4)
 * @param buf   Output buffer for the rendered text
 * @param max   Capacity of @p buf in bytes
 * @return Bytes written
 */
static int
gpio_dir_read(uint8_t port, char *buf, size_t max)
{
    int pos = 0;
    uint8_t pin;
    for (pin = 0; pin < 8 && pos < (int)max - 4; pin++) {
        int d = tiku_gpio_get_dir(port, pin);
        pos += snprintf(buf + pos, max - pos, "%c",
                        d == 1 ? 'O' : (d == 0 ? 'I' : '?'));
    }
    if (pos < (int)max - 1) {
        buf[pos++] = '\n';
        buf[pos] = '\0';
    }
    return pos;
}

/**
 * @brief Generate a fixed-port wrapper around gpio_dir_read().
 */
#define GPIO_DIR(p)                                                         \
    static int gpio_dir_##p(char *buf, size_t max) {                        \
        return gpio_dir_read(p, buf, max);                                  \
    }

#if TIKU_DEVICE_HAS_PORT1
GPIO_DIR(1)
#endif
#if TIKU_DEVICE_HAS_PORT2
GPIO_DIR(2)
#endif
#if TIKU_DEVICE_HAS_PORT3
GPIO_DIR(3)
#endif
#if TIKU_DEVICE_HAS_PORT4
GPIO_DIR(4)
#endif

/*---------------------------------------------------------------------------*/
/* /dev/gpio — per-pin VFS nodes                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Shared read worker for one GPIO pin.
 *
 * Renders the current input level as "0\n" or "1\n", or "err\n"
 * when the driver rejects the port/pin combination.  Reads the PxIN
 * register, so the value is meaningful for pins in input mode and
 * reflects the driven level for outputs.
 *
 * @param port  Port number (1-based)
 * @param pin   Pin number (0..7)
 * @param buf   Output buffer for the rendered text
 * @param max   Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
gpio_pin_read(uint8_t port, uint8_t pin, char *buf, size_t max)
{
    int v = tiku_gpio_read(port, pin);
    if (v < 0) {
        return snprintf(buf, max, "err\n");
    }
    return snprintf(buf, max, "%u\n", (unsigned)v);
}

/**
 * @brief Shared write worker for one GPIO pin.
 *
 * The first byte of the payload selects the action:
 *
 *   '0'  drive low (switches the pin to output)
 *   '1'  drive high (switches the pin to output)
 *   't'  toggle the output latch
 *   'i'  reconfigure as input with pull-up
 *
 * Unrecognised bytes are silently ignored (return 0) so that e.g.
 * an accidental trailing space does not surface as a write error
 * in shell scripts.
 *
 * @param port  Port number (1-based)
 * @param pin   Pin number (0..7)
 * @param buf   Input text; only buf[0] is examined
 * @param len   Input length in bytes (unused)
 * @return 0 always
 */
static int
gpio_pin_write(uint8_t port, uint8_t pin, const char *buf, size_t len)
{
    if (len == 0) {
        return TIKU_VFS_EINVAL;
    }
    if (buf[0] == '1') {
        tiku_gpio_write(port, pin, 1);
    } else if (buf[0] == '0') {
        tiku_gpio_write(port, pin, 0);
    } else if (buf[0] == 't') {
        tiku_gpio_toggle(port, pin);
    } else if (buf[0] == 'i') {
        tiku_gpio_dir_in(port, pin);
    } else {
        /* Was a silent no-op success; reject so an agent's bad write is
         * legible.  Accepts 0 / 1 / t(oggle) / i(nput). */
        return TIKU_VFS_EINVAL;
    }
    return 0;
}

/**
 * @brief Generate a read and a write handler for one port/pin pair.
 *
 * Each wrapper is ~10 bytes of code (load constants + tail call);
 * generating them beats storing port/pin in the node struct, which
 * would grow every node in the entire VFS by two bytes for the
 * benefit of this module alone.
 */
#define GPIO_PIN(p, b)                                                      \
    static int gpio_r_##p##_##b(char *buf, size_t max) {                    \
        return gpio_pin_read(p, b, buf, max);                               \
    }                                                                       \
    static int gpio_w_##p##_##b(const char *buf, size_t len) {              \
        return gpio_pin_write(p, b, buf, len);                              \
    }

/**
 * Pin name strings "0".."7", shared by every port directory: node
 * names are pointers, so all four ports reference the same eight
 * literals instead of duplicating them.
 */
static const char pn0[] = "0", pn1[] = "1", pn2[] = "2", pn3[] = "3",
                  pn4[] = "4", pn5[] = "5", pn6[] = "6", pn7[] = "7";

/* Generate handlers for each available port */
#if TIKU_DEVICE_HAS_PORT1
GPIO_PIN(1,0) GPIO_PIN(1,1) GPIO_PIN(1,2) GPIO_PIN(1,3)
GPIO_PIN(1,4) GPIO_PIN(1,5) GPIO_PIN(1,6) GPIO_PIN(1,7)
#endif
#if TIKU_DEVICE_HAS_PORT2
GPIO_PIN(2,0) GPIO_PIN(2,1) GPIO_PIN(2,2) GPIO_PIN(2,3)
GPIO_PIN(2,4) GPIO_PIN(2,5) GPIO_PIN(2,6) GPIO_PIN(2,7)
#endif
#if TIKU_DEVICE_HAS_PORT3
GPIO_PIN(3,0) GPIO_PIN(3,1) GPIO_PIN(3,2) GPIO_PIN(3,3)
GPIO_PIN(3,4) GPIO_PIN(3,5) GPIO_PIN(3,6) GPIO_PIN(3,7)
#endif
#if TIKU_DEVICE_HAS_PORT4
GPIO_PIN(4,0) GPIO_PIN(4,1) GPIO_PIN(4,2) GPIO_PIN(4,3)
GPIO_PIN(4,4) GPIO_PIN(4,5) GPIO_PIN(4,6) GPIO_PIN(4,7)
#endif

/**
 * @brief Build one pin-file node entry for a port table.
 */
#define GPIO_NODE(p, b) \
    { pn##b, TIKU_VFS_FILE, gpio_r_##p##_##b, gpio_w_##p##_##b, NULL, 0 }

/** Per-port pin tables: /dev/gpio/<port>/0../7 (eight files each) */
#if TIKU_DEVICE_HAS_PORT1
static const tiku_vfs_node_t gpio_p1[] = {
    GPIO_NODE(1,0), GPIO_NODE(1,1), GPIO_NODE(1,2), GPIO_NODE(1,3),
    GPIO_NODE(1,4), GPIO_NODE(1,5), GPIO_NODE(1,6), GPIO_NODE(1,7),
};
#endif
#if TIKU_DEVICE_HAS_PORT2
static const tiku_vfs_node_t gpio_p2[] = {
    GPIO_NODE(2,0), GPIO_NODE(2,1), GPIO_NODE(2,2), GPIO_NODE(2,3),
    GPIO_NODE(2,4), GPIO_NODE(2,5), GPIO_NODE(2,6), GPIO_NODE(2,7),
};
#endif
#if TIKU_DEVICE_HAS_PORT3
static const tiku_vfs_node_t gpio_p3[] = {
    GPIO_NODE(3,0), GPIO_NODE(3,1), GPIO_NODE(3,2), GPIO_NODE(3,3),
    GPIO_NODE(3,4), GPIO_NODE(3,5), GPIO_NODE(3,6), GPIO_NODE(3,7),
};
#endif
#if TIKU_DEVICE_HAS_PORT4
static const tiku_vfs_node_t gpio_p4[] = {
    GPIO_NODE(4,0), GPIO_NODE(4,1), GPIO_NODE(4,2), GPIO_NODE(4,3),
    GPIO_NODE(4,4), GPIO_NODE(4,5), GPIO_NODE(4,6), GPIO_NODE(4,7),
};
#endif

/*---------------------------------------------------------------------------*/
/* NODE TABLES                                                               */
/*---------------------------------------------------------------------------*/

/**
 * /dev/gpio directory table — one "1".."4" subdirectory per
 * available port, each pointing at its eight-pin table above.
 *
 * Exported so tiku_vfs_tree_dev.c can attach it as the "gpio"
 * directory; the entry count is TIKU_VFS_TREE_GPIO_NPORTS, derived
 * from the same TIKU_DEVICE_HAS_PORTn flags that gate the entries
 * (asserted below).
 */
const tiku_vfs_node_t tiku_vfs_tree_gpio_children[] = {
#if TIKU_DEVICE_HAS_PORT1
    { "1", TIKU_VFS_DIR, NULL, NULL, gpio_p1, 8 },
#endif
#if TIKU_DEVICE_HAS_PORT2
    { "2", TIKU_VFS_DIR, NULL, NULL, gpio_p2, 8 },
#endif
#if TIKU_DEVICE_HAS_PORT3
    { "3", TIKU_VFS_DIR, NULL, NULL, gpio_p3, 8 },
#endif
#if TIKU_DEVICE_HAS_PORT4
    { "4", TIKU_VFS_DIR, NULL, NULL, gpio_p4, 8 },
#endif
};

/**
 * /dev/gpio_dir directory table — one direction-summary file per
 * available port.
 *
 * Exported alongside the pin tree; same port gating and count.
 */
const tiku_vfs_node_t tiku_vfs_tree_gpio_dir_children[] = {
#if TIKU_DEVICE_HAS_PORT1
    { "1", TIKU_VFS_FILE, gpio_dir_1, NULL, NULL, 0 },
#endif
#if TIKU_DEVICE_HAS_PORT2
    { "2", TIKU_VFS_FILE, gpio_dir_2, NULL, NULL, 0 },
#endif
#if TIKU_DEVICE_HAS_PORT3
    { "3", TIKU_VFS_FILE, gpio_dir_3, NULL, NULL, 0 },
#endif
#if TIKU_DEVICE_HAS_PORT4
    { "4", TIKU_VFS_FILE, gpio_dir_4, NULL, NULL, 0 },
#endif
};

_Static_assert(sizeof(tiku_vfs_tree_gpio_children) /
               sizeof(tiku_vfs_tree_gpio_children[0])
               == TIKU_VFS_TREE_GPIO_NPORTS,
               "TIKU_VFS_TREE_GPIO_NPORTS out of sync");
_Static_assert(sizeof(tiku_vfs_tree_gpio_dir_children) /
               sizeof(tiku_vfs_tree_gpio_dir_children[0])
               == TIKU_VFS_TREE_GPIO_NPORTS,
               "TIKU_VFS_TREE_GPIO_NPORTS out of sync");

/*---------------------------------------------------------------------------*/
/* DRIVER NOTIFY — ring /dev/gpio watchers on a hardware edge                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Ring the watchers of /dev/gpio/<port>/<pin> after a pin edge.
 *
 * The bridge from the GPIO edge interrupt to the VFS watch layer:
 * the port ISR (arch/msp430/tiku_gpio_irq_arch.c) calls this with the
 * pin that just fired, and it rings that pin node's watchers via
 * tiku_vfs_notify().  A rule or `watch` on the pin then reacts to the
 * physical edge exactly as it would to a write — the value comes from
 * re-reading the node (the live PxIN level).
 *
 * GPIO pin nodes are read/write (a pin is dual-direction), so a rule
 * on one is already event-armed; before this hook a hardware edge had
 * no event source — only an explicit `write` rang it.  This supplies
 * the missing source, turning "act when the pin changes" from a poll
 * into an interrupt-driven path.
 *
 * ISR-safe: a bounds check, a constant-time table index, and the
 * (itself ISR-safe) tiku_vfs_notify() scan.  Out-of-range port/pin
 * and ports absent on this device fall through to a NULL node, which
 * tiku_vfs_notify() treats as a no-op.
 *
 * @param port  Port number (1-based, P1..P4)
 * @param pin   Pin number (0..7)
 */
void
tiku_vfs_tree_gpio_notify(uint8_t port, uint8_t pin)
{
    const tiku_vfs_node_t *node = NULL;

    if (pin > 7) {
        return;
    }
    switch (port) {
#if TIKU_DEVICE_HAS_PORT1
    case 1: node = &gpio_p1[pin]; break;
#endif
#if TIKU_DEVICE_HAS_PORT2
    case 2: node = &gpio_p2[pin]; break;
#endif
#if TIKU_DEVICE_HAS_PORT3
    case 3: node = &gpio_p3[pin]; break;
#endif
#if TIKU_DEVICE_HAS_PORT4
    case 4: node = &gpio_p4[pin]; break;
#endif
    default: break;
    }

    tiku_vfs_notify(node);   /* NULL -> no-op */
}
