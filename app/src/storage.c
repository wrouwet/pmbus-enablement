/*
 * STORE_DEFAULT_ALL and RESTORE_DEFAULT_ALL, backed by the storage partition.
 *
 * One record lives at the start of the partition. A RAM copy answers
 * RESTORE_DEFAULT_ALL straight from the I2C interrupt; a work item does the flash
 * erase and write that STORE_DEFAULT_ALL asks for.
 *
 * Any bus host may send STORE_DEFAULT_ALL (PMBus has no authentication, and
 * WRITE_PROTECT can be cleared by anyone), and flash survives a limited number of
 * erases. So a store that changes nothing writes nothing, flash writes are spaced at
 * least CONFIG_PMBUS_STORE_MIN_INTERVAL_MS apart (the device stays BUSY meanwhile), and
 * after CONFIG_PMBUS_STORE_MAX_PER_BOOT writes further stores fail with a memory fault
 * until the next reset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>

#include "app.h"

LOG_MODULE_REGISTER(storage, LOG_LEVEL_INF);

#define STORAGE_ID   PARTITION_ID(storage_partition)
#define RECORD_MAGIC 0x504d4231 /* "PMB1" */

struct record {
	uint32_t magic;
	struct pmbus_device_config cfg;
	uint32_t crc;
};

/* Flash writes go in 16-byte phrases. */
#define RECORD_SIZE ROUND_UP(sizeof(struct record), 16)

static union {
	struct record r;
	uint8_t raw[RECORD_SIZE];
} __aligned(4) shadow, pending;

static bool shadow_valid;
static uint32_t n_writes;
static int64_t last_write_ms = -CONFIG_PMBUS_STORE_MIN_INTERVAL_MS;

static uint32_t record_crc(const struct record *r)
{
	return crc32_ieee((const uint8_t *)r, offsetof(struct record, crc));
}

int storage_init(void)
{
	const struct flash_area *fa;
	int ret = flash_area_open(STORAGE_ID, &fa);

	if (ret != 0) {
		return ret;
	}
	ret = flash_area_read(fa, 0, shadow.raw, sizeof(shadow.raw));
	flash_area_close(fa);
	if (ret != 0) {
		return ret;
	}
	shadow_valid = shadow.r.magic == RECORD_MAGIC && shadow.r.crc == record_crc(&shadow.r);
	LOG_INF("stored config: %s", shadow_valid ? "found" : "none");
	return 0;
}

static void store_work_fn(struct k_work *work)
{
	const struct flash_area *fa;
	unsigned int key;
	bool ok = false;

	ARG_UNUSED(work);

	if (flash_area_open(STORAGE_ID, &fa) == 0) {
		ok = flash_area_flatten(fa, 0, fa->fa_size) == 0 &&
		     flash_area_write(fa, 0, pending.raw, sizeof(pending.raw)) == 0;
		flash_area_close(fa);
	}
	n_writes++;
	last_write_ms = k_uptime_get();
	key = irq_lock();
	if (ok) {
		shadow = pending;
		shadow_valid = true;
	}
	pmbus_device_store_done(&pmbus_dev, ok);
	irq_unlock(key);
	LOG_INF("STORE_DEFAULT_ALL %s (%u this boot)", ok ? "done" : "failed", n_writes);
}

static K_WORK_DELAYABLE_DEFINE(store_work, store_work_fn);

static void set_alert(void *ctx, bool asserted)
{
	ARG_UNUSED(ctx);
	pmbus_i2c_set_alert(asserted);
}

/* Called from the I2C interrupt. */
static void store(void *ctx, const struct pmbus_device_config *cfg)
{
	ARG_UNUSED(ctx);
	/* Nothing new to store: done, without touching flash. */
	if (shadow_valid && memcmp(&shadow.r.cfg, cfg, sizeof(*cfg)) == 0) {
		pmbus_device_store_done(&pmbus_dev, true);
		return;
	}
	if (n_writes >= CONFIG_PMBUS_STORE_MAX_PER_BOOT) {
		pmbus_device_store_done(&pmbus_dev, false);
		return;
	}
	/* The device refuses a second store while BUSY, so pending is ours until done. */
	memset(&pending, 0xFF, sizeof(pending));
	pending.r.magic = RECORD_MAGIC;
	memcpy(&pending.r.cfg, cfg, sizeof(*cfg));
	pending.r.crc = record_crc(&pending.r);

	int64_t wait = last_write_ms + CONFIG_PMBUS_STORE_MIN_INTERVAL_MS - k_uptime_get();

	k_work_schedule(&store_work, K_MSEC(wait > 0 ? wait : 0));
}

/* The device validates what it gets (pmbus_device_config_valid()) before using it. */
static bool restore(void *ctx, struct pmbus_device_config *cfg)
{
	ARG_UNUSED(ctx);
	if (!shadow_valid) {
		return false;
	}
	memcpy(cfg, &shadow.r.cfg, sizeof(*cfg));
	return true;
}

const struct pmbus_device_ops storage_device_ops = {
	.set_alert = set_alert,
	.store = store,
	.restore = restore,
};
