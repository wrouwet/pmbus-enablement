/*
 * The PMBus device on a pretend bus, as a shared library for the Python tools
 * (tools/pmbus/transport.py, SimTransport). It runs the same library code as the
 * firmware, so the Python test suite can run without hardware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pmbus/device.h>
#include <pmbus/pmbus.h>

static struct pmbus_device dev;
static struct pmbus_sensors sensors;
static bool alert;
static bool have_stored;
static struct pmbus_device_config stored;

static void set_alert(void *ctx, bool asserted)
{
	(void)ctx;
	alert = asserted;
}

static void store(void *ctx, const struct pmbus_device_config *cfg)
{
	(void)ctx;
	stored = *cfg;
	have_stored = true;
	pmbus_device_store_done(&dev, true);
}

static bool restore(void *ctx, struct pmbus_device_config *cfg)
{
	(void)ctx;
	if (have_stored) {
		*cfg = stored;
	}
	return have_stored;
}

static const struct pmbus_device_ops ops = {
	.set_alert = set_alert,
	.store = store,
	.restore = restore,
};

void sim_set_temps(int32_t t1_mc, int32_t t2_mc)
{
	sensors.temp1_valid = true;
	sensors.temp1_mc = t1_mc;
	sensors.temp2_valid = true;
	sensors.temp2_mc = t2_mc;
	pmbus_device_update(&dev, &sensors);
}

/* Power-cycles the device. Stored config survives, like flash. */
void sim_init(uint8_t addr)
{
	alert = false;
	pmbus_device_init(&dev, addr, &ops, NULL, true, "SIM00001");
	pmbus_device_load_stored(&dev);
	sim_set_temps(25000, 30000);
}

/* Lets time pass: output sequencing, fault delays and retries, the fan. */
void sim_advance(uint32_t ms)
{
	pmbus_device_advance(&dev, ms);
}

void sim_erase_storage(void)
{
	have_stored = false;
}

int sim_alert(void)
{
	return alert;
}

/*
 * One transaction: write wn bytes, then (after a repeated start) read rn bytes.
 * Returns 0, or -1 if nobody ACKs the address.
 */
int sim_xfer(uint8_t addr, const uint8_t *w, int wn, uint8_t *r, int rn)
{
	struct pmbus_target *t = &dev.target;

	/* The firmware only answers the Alert Response Address while SMBALERT# is low. */
	if (addr != t->addr && !(addr == SMBUS_ALERT_RESPONSE_ADDR && alert)) {
		return -1;
	}
	if (wn > 0 || rn == 0) {
		pmbus_target_write_start(t);
		for (int i = 0; i < wn; i++) {
			pmbus_target_write_byte(t, w[i]);
		}
	}
	for (int i = 0; i < rn; i++) {
		if (i > 0) {
			r[i] = pmbus_target_read_byte(t);
		} else if (addr == SMBUS_ALERT_RESPONSE_ADDR) {
			r[i] = pmbus_target_ara_start(t);
		} else {
			r[i] = pmbus_target_read_start(t);
		}
	}
	pmbus_target_stop(t);
	return 0;
}
