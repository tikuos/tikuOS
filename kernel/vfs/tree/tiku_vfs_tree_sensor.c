/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_sensor.c - /dev/sensors VFS nodes.
 *
 * Model-specific controls over the sensor kit's single-instance temperature
 * drivers: initialize, read, resolution and shutdown.  Listing the subtree
 * and reading its metadata never touch a bus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_sensor.h"
#include <stdio.h>
#include <string.h>

#if TIKU_VFS_SENSOR_I2C
#include <interfaces/bus/tiku_i2c_bus.h>
#include <tikukits/sensors/temperature/tiku_kits_sensor_mcp9808.h>
#include <tikukits/sensors/temperature/tiku_kits_sensor_adt7410.h>
#endif
#if TIKU_VFS_SENSOR_OW
#include <interfaces/onewire/tiku_onewire.h>
#include <tikukits/sensors/temperature/tiku_kits_sensor_ds18b20.h>
#include <kernel/timers/tiku_clock.h>
#endif

/*---------------------------------------------------------------------------*/
/* /dev/sensors/enabled                                                      */
/*---------------------------------------------------------------------------*/

static const tiku_vfs_desc_t cached_text = TIKU_VFS_DESC(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);

/** @brief /dev/sensors/enabled: 1 when this build has the sensor drivers. */
static int
enabled_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)TIKU_VFS_SENSOR_I2C);
}

#if TIKU_VFS_SENSOR_I2C

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/* A temperature read can clear interrupt or status bits on some of these
 * sensors, so it is an effect, not a passive observation. */
static const tiku_vfs_desc_t temperature = TIKU_VFS_DESC_FLAGS(
    TIKU_VFS_T_I32, TIKU_VFS_U_MILLICELSIUS, TIKU_VFS_FRESH_LIVE,
    TIKU_VFS_E_BUS, TIKU_VFS_DF_READ_EFFECT);
static const tiku_vfs_desc_t bus_number = TIKU_VFS_DESC(
    TIKU_VFS_T_U32, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_LIVE, TIKU_VFS_E_BUS);
static const tiku_vfs_desc_t bus_bool = TIKU_VFS_DESC(
    TIKU_VFS_T_BOOL, TIKU_VFS_U_BOOL, TIKU_VFS_FRESH_LIVE, TIKU_VFS_E_BUS);

/**
 * @brief Map a sensor-kit result to a VFS status.
 *
 * A driver that is not initialized, or a device that does not answer its
 * init, is ENOTSUP; any other failure is EIO.
 */
static int
sensor_result(int rc)
{
    if (rc == TIKU_KITS_SENSOR_ERR_PARAM) {
        return TIKU_VFS_EINVAL;
    }
    if (rc == TIKU_KITS_SENSOR_ERR_NO_DEVICE) {
        return TIKU_VFS_ENOTSUP;
    }
    return (rc != TIKU_KITS_SENSOR_OK) ? TIKU_VFS_EIO : TIKU_VFS_OK;
}

/**
 * @brief Parse a written decimal of 1..3 digits, trailing blanks aside.
 *
 * @return The value (0..255), or -1 for anything else
 */
static int
number(const char *buf, size_t len)
{
    unsigned value = 0u;
    size_t i;

    while (len > 0u && (buf[len - 1u] == '\n' || buf[len - 1u] == '\r' ||
                        buf[len - 1u] == ' ' || buf[len - 1u] == '\t')) {
        len--;
    }
    if (len == 0u || len > 3u) {
        return -1;
    }
    for (i = 0u; i < len; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            return -1;
        }
        value = value * 10u + (unsigned)(buf[i] - '0');
    }
    return (value <= 255u) ? (int)value : -1;
}

/**
 * @brief Render a kit temperature (sixteenths of a degree) in millidegrees.
 *
 * @return Bytes rendered, or the VFS status for a failed read @p rc
 */
static int
render_temp(int rc, const tiku_kits_sensor_temp_t *t, char *buf, size_t max)
{
    long mc;

    if (rc != TIKU_KITS_SENSOR_OK) {
        return sensor_result(rc);
    }
    mc = (long)t->integer * 1000L + ((long)t->frac * 1000L + 8L) / 16L;
    return snprintf(buf, max, "%ld\n", t->negative ? -mc : mc);
}

/*
 * The state, address and init handlers of one I2C model.  An address write
 * in [low, high] opens the bus at standard speed when nothing has opened it,
 * then initializes the driver at that address.
 */
#define I2C_FUNCS(model, low, high)                                          \
    static int model##_state(char *buf, size_t max)                          \
    {                                                                        \
        return snprintf(buf, max, "%s\n",                                    \
                        tiku_kits_sensor_##model##_address() ?               \
                        "initialized" : "not-initialized");                  \
    }                                                                        \
    static int model##_address(char *buf, size_t max)                        \
    {                                                                        \
        return snprintf(buf, max, "%u\n",                                    \
                        (unsigned)tiku_kits_sensor_##model##_address());     \
    }                                                                        \
    static int model##_init(const char *buf, size_t len)                     \
    {                                                                        \
        const tiku_i2c_config_t cfg = { TIKU_I2C_SPEED_STANDARD };           \
        int addr = number(buf, len);                                         \
                                                                             \
        if (addr < (low) || addr > (high)) {                                 \
            return TIKU_VFS_EINVAL;                                          \
        }                                                                    \
        if (tiku_i2c_get_config() == NULL && tiku_i2c_init(&cfg) != 0) {     \
            return TIKU_VFS_ENOTSUP;                                         \
        }                                                                    \
        return sensor_result(tiku_kits_sensor_##model##_init((uint8_t)addr));\
    }

I2C_FUNCS(mcp9808, TIKU_KITS_SENSOR_MCP9808_ADDR_MIN,
          TIKU_KITS_SENSOR_MCP9808_ADDR_MAX)
I2C_FUNCS(adt7410, TIKU_KITS_SENSOR_ADT7410_ADDR_MIN,
          TIKU_KITS_SENSOR_ADT7410_ADDR_MAX)

/** @brief mcp9808/temperature_mc: ENOTSUP until the driver is initialized. */
static int
mcp9808_temperature(char *buf, size_t max)
{
    tiku_kits_sensor_temp_t t;
    int rc;

    if (tiku_kits_sensor_mcp9808_address() == 0u) {
        return TIKU_VFS_ENOTSUP;
    }
    rc = tiku_kits_sensor_mcp9808_read(&t);
    return render_temp(rc, &t, buf, max);
}

/** @brief adt7410/temperature_mc: at the configured resolution. */
static int
adt7410_temperature(char *buf, size_t max)
{
    int32_t mc;
    int rc = tiku_kits_sensor_adt7410_read_mc(&mc);

    if (rc != TIKU_KITS_SENSOR_OK) {
        return sensor_result(rc);
    }
    return snprintf(buf, max, "%ld\n", (long)mc);
}

/* The read and write handlers of one model's configuration setting. */
#define CONFIG_FUNCS(model, setting)                                         \
    static int model##_##setting##_read(char *buf, size_t max)               \
    {                                                                        \
        uint8_t value;                                                       \
        int rc = tiku_kits_sensor_##model##_get_##setting(&value);           \
                                                                             \
        if (rc != TIKU_KITS_SENSOR_OK) {                                     \
            return sensor_result(rc);                                        \
        }                                                                    \
        return snprintf(buf, max, "%u\n", (unsigned)value);                  \
    }                                                                        \
    static int model##_##setting##_write(const char *buf, size_t len)        \
    {                                                                        \
        int value = number(buf, len);                                        \
                                                                             \
        if (value < 0) {                                                     \
            return TIKU_VFS_EINVAL;                                          \
        }                                                                    \
        return sensor_result(                                                \
            tiku_kits_sensor_##model##_set_##setting((uint8_t)value));       \
    }

CONFIG_FUNCS(mcp9808, resolution)
CONFIG_FUNCS(mcp9808, shutdown)
CONFIG_FUNCS(adt7410, resolution)
CONFIG_FUNCS(adt7410, shutdown)

/* The node table of one I2C model.  Macro parameter names stay distinct from
 * the designated member names they sit beside. */
#define SENSOR_NODES(m)                                                      \
    { .name = "state", .type = TIKU_VFS_FILE, .read = m##_state,             \
      .desc = &cached_text },                                                \
    { .name = "address", .type = TIKU_VFS_FILE, .read = m##_address,         \
      .write = m##_init, .desc = &cached_text,                               \
      .req_cap = TIKU_VFS_CAP_HW },                                          \
    { .name = "temperature_mc", .type = TIKU_VFS_FILE,                       \
      .read = m##_temperature, .desc = &temperature },                       \
    { .name = "resolution_bits", .type = TIKU_VFS_FILE,                      \
      .read = m##_resolution_read, .write = m##_resolution_write,            \
      .desc = &bus_number, .req_cap = TIKU_VFS_CAP_HW },                     \
    { .name = "shutdown", .type = TIKU_VFS_FILE,                             \
      .read = m##_shutdown_read, .write = m##_shutdown_write,                \
      .desc = &bus_bool, .req_cap = TIKU_VFS_CAP_HW }

static const tiku_vfs_node_t mcp_nodes[] = { SENSOR_NODES(mcp9808) };
static const tiku_vfs_node_t adt_nodes[] = { SENSOR_NODES(adt7410) };

#if TIKU_VFS_SENSOR_OW

/*---------------------------------------------------------------------------*/
/* /dev/sensors/ds18b20                                                      */
/*---------------------------------------------------------------------------*/

/* 750 ms, the 12-bit conversion time, in whole ticks plus one: the first
 * tick can arrive just after the command, which would cut the wait short. */
#define DS_CONVERSION_TICKS                                                  \
    ((tiku_clock_time_t)((TIKU_CLOCK_SECOND * 3UL + 3UL) / 4UL + 1UL))

static uint8_t converting;              /* a conversion is running        */
static uint8_t converted;               /* a result is ready to read      */
static tiku_clock_time_t conversion_start;

/**
 * @brief Report whether a conversion is still running.
 *
 * A conversion is done once DS_CONVERSION_TICKS have passed since it was
 * started; this is when converting turns into converted.
 */
static int
ds_busy(void)
{
    if (converting &&
        (tiku_clock_time_t)(tiku_clock_time() - conversion_start) >=
        DS_CONVERSION_TICKS) {
        converting = 0u;
        converted = 1u;
    }
    return converting;
}

/** @brief True when a written value is exactly @p expected, line ends aside. */
static int
token(const char *buf, size_t len, const char *expected)
{
    while (len > 0u && (buf[len - 1u] == '\n' || buf[len - 1u] == '\r')) {
        len--;
    }
    return len == strlen(expected) && memcmp(buf, expected, len) == 0;
}

/** @brief ds18b20/state: not-initialized, converting, ready or not-sampled. */
static int
ds_state(char *buf, size_t max)
{
    const char *state;

    if (!tiku_kits_sensor_ds18b20_ready()) {
        state = "not-initialized";
    } else if (ds_busy()) {
        state = "converting";
    } else if (converted) {
        state = "ready";
    } else {
        state = "not-sampled";
    }
    return snprintf(buf, max, "%s\n", state);
}

/**
 * @brief ds18b20/init: "init" opens the 1-Wire bus if needed, then probes.
 *
 * EBUSY while a conversion runs or another user owns the bus pin.
 */
static int
ds_init(const char *buf, size_t len)
{
    int rc;

    if (!token(buf, len, "init")) {
        return TIKU_VFS_EINVAL;
    }
    if (ds_busy()) {
        return TIKU_VFS_EBUSY;
    }
    converted = 0u;
    if (!tiku_onewire_is_open()) {
        rc = tiku_onewire_init();
        if (rc == TIKU_OW_ERR_BUSY) {
            return TIKU_VFS_EBUSY;
        }
        if (rc != TIKU_OW_OK) {
            return TIKU_VFS_ENOTSUP;
        }
    }
    return sensor_result(tiku_kits_sensor_ds18b20_init());
}

/** @brief ds18b20/convert: "start" begins a conversion. */
static int
ds_convert(const char *buf, size_t len)
{
    int rc;

    if (!token(buf, len, "start")) {
        return TIKU_VFS_EINVAL;
    }
    if (!tiku_kits_sensor_ds18b20_ready()) {
        return TIKU_VFS_ENOTSUP;
    }
    if (ds_busy()) {
        return TIKU_VFS_EBUSY;
    }
    rc = tiku_kits_sensor_ds18b20_start_conversion();
    if (rc == TIKU_KITS_SENSOR_OK) {
        converted = 0u;
        converting = 1u;
        conversion_start = tiku_clock_time();
    }
    return sensor_result(rc);
}

/** @brief ds18b20/temperature_mc: EBUSY until a conversion has finished. */
static int
ds_temperature(char *buf, size_t max)
{
    tiku_kits_sensor_temp_t t;
    int rc;

    if (!tiku_kits_sensor_ds18b20_ready()) {
        return TIKU_VFS_ENOTSUP;
    }
    if (ds_busy() || !converted) {
        return TIKU_VFS_EBUSY;
    }
    rc = tiku_kits_sensor_ds18b20_read(&t);
    return render_temp(rc, &t, buf, max);
}

/** @brief ds18b20/resolution_bits: EBUSY while a conversion runs. */
static int
ds_resolution_read(char *buf, size_t max)
{
    uint8_t bits;
    int rc;

    if (ds_busy()) {
        return TIKU_VFS_EBUSY;
    }
    rc = tiku_kits_sensor_ds18b20_get_resolution(&bits);
    if (rc != TIKU_KITS_SENSOR_OK) {
        return sensor_result(rc);
    }
    return snprintf(buf, max, "%u\n", (unsigned)bits);
}

/**
 * @brief ds18b20/resolution_bits: set 9..12 bits.
 *
 * A change discards the last result, which was taken at the old resolution.
 */
static int
ds_resolution_write(const char *buf, size_t len)
{
    int bits = number(buf, len);
    int rc;

    if (bits < TIKU_KITS_SENSOR_DS18B20_RES_MIN ||
        bits > TIKU_KITS_SENSOR_DS18B20_RES_MAX) {
        return TIKU_VFS_EINVAL;
    }
    if (ds_busy()) {
        return TIKU_VFS_EBUSY;
    }
    rc = tiku_kits_sensor_ds18b20_set_resolution((uint8_t)bits);
    if (rc == TIKU_KITS_SENSOR_OK) {
        converted = 0u;
    }
    return sensor_result(rc);
}

static const tiku_vfs_node_t ds_nodes[] = {
    { .name = "state", .type = TIKU_VFS_FILE, .read = ds_state,
      .desc = &cached_text },
    { .name = "init", .type = TIKU_VFS_FILE, .write = ds_init,
      .req_cap = TIKU_VFS_CAP_HW },
    { .name = "convert", .type = TIKU_VFS_FILE, .write = ds_convert,
      .req_cap = TIKU_VFS_CAP_HW },
    { .name = "temperature_mc", .type = TIKU_VFS_FILE,
      .read = ds_temperature, .desc = &temperature },
    { .name = "resolution_bits", .type = TIKU_VFS_FILE,
      .read = ds_resolution_read, .write = ds_resolution_write,
      .desc = &bus_number, .req_cap = TIKU_VFS_CAP_HW },
};

#endif /* TIKU_VFS_SENSOR_OW */
#endif /* TIKU_VFS_SENSOR_I2C */

/*---------------------------------------------------------------------------*/
/* /dev/sensors                                                              */
/*---------------------------------------------------------------------------*/

const tiku_vfs_node_t tiku_vfs_tree_sensor_children[] = {
    { .name = "enabled", .type = TIKU_VFS_FILE, .read = enabled_read,
      .desc = &cached_text },
#if TIKU_VFS_SENSOR_I2C
    { .name = "mcp9808", .type = TIKU_VFS_DIR, .children = mcp_nodes,
      .child_count = sizeof(mcp_nodes) / sizeof(mcp_nodes[0]) },
    { .name = "adt7410", .type = TIKU_VFS_DIR, .children = adt_nodes,
      .child_count = sizeof(adt_nodes) / sizeof(adt_nodes[0]) },
#if TIKU_VFS_SENSOR_OW
    { .name = "ds18b20", .type = TIKU_VFS_DIR, .children = ds_nodes,
      .child_count = sizeof(ds_nodes) / sizeof(ds_nodes[0]) },
#endif
#endif
};

_Static_assert(sizeof(tiku_vfs_tree_sensor_children) /
               sizeof(tiku_vfs_tree_sensor_children[0])
               == TIKU_VFS_TREE_SENSOR_NCHILD,
               "TIKU_VFS_TREE_SENSOR_NCHILD out of sync");
