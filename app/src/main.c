/*
 * FRDM-MCXA153 PMBus target: a simulated 54 V to 12 V converter at
 * CONFIG_PMBUS_ADDRESS on LPI2C0, with its temperatures read from the on-board P3T1755
 * (READ_TEMPERATURE_1) and the MCU die (READ_TEMPERATURE_2).
 *
 * LEDs: green while the output is on and no status bit is set, red while any status
 * bit is set, blue on PMBus traffic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <pmbus/pmbus.h>

#include "app.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

struct pmbus_device pmbus_dev;
struct temp_override temp_override[2];

static const struct device *const temp_dev[2] = {
	DEVICE_DT_GET_OR_NULL(DT_ALIAS(ambient_temp0)),
	DEVICE_DT_GET_OR_NULL(DT_ALIAS(die_temp0)),
};
static const enum sensor_channel temp_chan[2] = {
	SENSOR_CHAN_AMBIENT_TEMP,
	SENSOR_CHAN_DIE_TEMP,
};

static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_NODELABEL(green_led), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_NODELABEL(red_led), gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_NODELABEL(blue_led), gpios);

static bool read_temp(int i, int32_t *mc)
{
	struct sensor_value v;

	if (temp_override[i].active) {
		*mc = temp_override[i].mc;
		return true;
	}
	if (temp_dev[i] == NULL || !device_is_ready(temp_dev[i]) ||
	    sensor_sample_fetch(temp_dev[i]) != 0 ||
	    sensor_channel_get(temp_dev[i], temp_chan[i], &v) != 0) {
		return false;
	}
	*mc = v.val1 * 1000 + v.val2 / 1000;
	return true;
}

/* MFR_SERIAL: the first 8 bytes of the chip's unique ID, in hex. */
static void make_serial(char *buf, size_t len)
{
	uint8_t id[8];
	ssize_t n = hwinfo_get_device_id(id, sizeof(id));

	buf[0] = '\0';
	for (ssize_t i = 0; i < n && (size_t)(2 * i + 2) < len; i++) {
		snprintf(&buf[2 * i], 3, "%02X", id[i]);
	}
}

int main(void)
{
	char serial[17];
	struct pmbus_sensors s = {0};
	uint32_t last_transactions = 0;
	uint32_t since_sensors = 0;
	int64_t last = k_uptime_get();
	unsigned int key;
	int ret;

	gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);

	make_serial(serial, sizeof(serial));
	pmbus_device_init(&pmbus_dev, CONFIG_PMBUS_ADDRESS, &storage_device_ops, NULL,
			  DT_NODE_HAS_PROP(DT_PATH(zephyr_user), smbalert_gpios), serial);
	pmbus_dev.sim_knobs = IS_ENABLED(CONFIG_PMBUS_SIM_KNOBS);
	if (storage_init() != 0) {
		LOG_ERR("storage partition unreadable; using defaults");
	}
	pmbus_device_load_stored(&pmbus_dev);

	s.temp1_valid = read_temp(0, &s.temp1_mc);
	s.temp2_valid = read_temp(1, &s.temp2_mc);
	LOG_INF("temperature 1 (P3T1755): %s, temperature 2 (die): %s",
		s.temp1_valid ? "ok" : "missing", s.temp2_valid ? "ok" : "missing");
	pmbus_device_update(&pmbus_dev, &s);

	ret = pmbus_i2c_start();
	if (ret != 0) {
		gpio_pin_set_dt(&led_red, 1);
		return ret;
	}

	while (true) {
		k_msleep(CONFIG_PMBUS_TICK_MS);

		/* Output sequencing and fault timers run every tick, sensors less often. */
		int64_t now = k_uptime_get();
		uint32_t elapsed = (uint32_t)(now - last);
		bool fresh = false;

		last = now;
		since_sensors += elapsed;
		if (since_sensors >= CONFIG_PMBUS_UPDATE_PERIOD_MS) {
			since_sensors = 0;
			s.temp1_valid = read_temp(0, &s.temp1_mc);
			s.temp2_valid = read_temp(1, &s.temp2_mc);
			fresh = true;
		}

		key = irq_lock();
		if (fresh) {
			pmbus_dev.sensors = s;
		}
		pmbus_device_advance(&pmbus_dev, elapsed);
		bool on = pmbus_dev.output_on;
		/* Being off, busy or not power-good alone is not a fault. */
		bool flagged = (pmbus_device_status_word(&pmbus_dev) &
				~(PMBUS_SB_OFF | PMBUS_SB_BUSY | PMBUS_SW_POWER_GOOD_N)) != 0;
		uint32_t transactions = pmbus_dev.target.n_transactions;
		irq_unlock(key);

		gpio_pin_set_dt(&led_green, on && !flagged);
		gpio_pin_set_dt(&led_red, flagged);
		gpio_pin_set_dt(&led_blue, transactions != last_transactions);
		last_transactions = transactions;
	}
	return 0;
}
