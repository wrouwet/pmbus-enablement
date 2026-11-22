/*
 * Shell commands on the debug UART for watching the device and steering the
 * simulation by hand.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include <pmbus/pmbus.h>

#include "app.h"

/* Prints a milli-unit value as a decimal with three places. */
#define MILLI(v) ((v) < 0 ? "-" : ""), abs((v) / 1000), abs((v) % 1000)

/* What `pmbus status` prints, copied out under the lock (the device itself is large). */
struct snapshot {
	uint8_t addr;
	bool output_on, fault_off, power_good, current_limited, alert_pending;
	enum pmbus_output_state state;
	int32_t vin_mv, iin_ma, pin_mw, vout_mv, iout_ma, pout_mw, fan1_rpm;
	struct pmbus_sensors sensors;
	uint16_t status_word;
	uint8_t status[PMBUS_S_COUNT];
	uint32_t n_transactions, n_pec_errors, n_aborts;
};

static const char *const state_names[] = {
	[PMBUS_OUT_OFF] = "off",
	[PMBUS_OUT_TON_DELAY] = "TON_DELAY",
	[PMBUS_OUT_RISING] = "rising",
	[PMBUS_OUT_ON] = "on",
	[PMBUS_OUT_TOFF_DELAY] = "TOFF_DELAY",
	[PMBUS_OUT_FALLING] = "falling",
};

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct snapshot d;
	uint32_t timeouts;
	unsigned int key;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	key = irq_lock();
	d.addr = pmbus_dev.target.addr;
	d.output_on = pmbus_dev.output_on;
	d.fault_off = pmbus_dev.fault_off;
	d.power_good = pmbus_dev.power_good;
	d.current_limited = pmbus_dev.current_limited;
	d.alert_pending = pmbus_dev.alert_pending;
	d.state = pmbus_dev.out_state;
	d.vin_mv = pmbus_dev.vin_mv;
	d.iin_ma = pmbus_dev.iin_ma;
	d.pin_mw = pmbus_dev.pin_mw;
	d.vout_mv = pmbus_dev.vout_mv;
	d.iout_ma = pmbus_dev.iout_ma;
	d.pout_mw = pmbus_dev.pout_mw;
	d.fan1_rpm = pmbus_dev.fan1_rpm;
	d.sensors = pmbus_dev.sensors;
	d.status_word = pmbus_device_status_word(&pmbus_dev);
	memcpy(d.status, pmbus_dev.status, sizeof(d.status));
	d.n_transactions = pmbus_dev.target.n_transactions;
	d.n_pec_errors = pmbus_dev.target.n_pec_errors;
	d.n_aborts = pmbus_dev.target.n_aborts;
	irq_unlock(key);
	pmbus_i2c_stats(&timeouts);

	shell_print(sh, "address 0x%02x, output %s%s%s%s", d.addr, state_names[d.state],
		    d.fault_off ? " (latched off by a fault)" : "",
		    d.power_good ? ", power good" : "",
		    d.current_limited ? ", current limited" : "");
	shell_print(sh, "VIN  %s%d.%03d V   IIN  %s%d.%03d A   PIN  %s%d.%03d W", MILLI(d.vin_mv),
		    MILLI(d.iin_ma), MILLI(d.pin_mw));
	shell_print(sh, "VOUT %s%d.%03d V   IOUT %s%d.%03d A   POUT %s%d.%03d W",
		    MILLI(d.vout_mv), MILLI(d.iout_ma), MILLI(d.pout_mw));
	shell_print(sh, "TEMP1 %s%s%d.%03d C   TEMP2 %s%s%d.%03d C   FAN1 %d RPM",
		    d.sensors.temp1_valid ? "" : "(missing) ", MILLI(d.sensors.temp1_mc),
		    d.sensors.temp2_valid ? "" : "(missing) ", MILLI(d.sensors.temp2_mc),
		    (int)d.fan1_rpm);
	shell_print(sh, "STATUS_WORD 0x%04x  VOUT 0x%02x  IOUT 0x%02x  INPUT 0x%02x  "
		    "TEMPERATURE 0x%02x  CML 0x%02x  OTHER 0x%02x  FANS 0x%02x",
		    d.status_word, d.status[PMBUS_S_VOUT], d.status[PMBUS_S_IOUT],
		    d.status[PMBUS_S_INPUT], d.status[PMBUS_S_TEMPERATURE], d.status[PMBUS_S_CML],
		    d.status[PMBUS_S_OTHER], d.status[PMBUS_S_FANS_1_2]);
	shell_print(sh, "SMBALERT# %s, transactions %u, PEC errors %u, aborts %u, timeouts %u",
		    d.alert_pending ? "asserted" : "released", d.n_transactions, d.n_pec_errors,
		    d.n_aborts, timeouts);
	return 0;
}

/* Parses a whole decimal or 0x number; rejects junk and out-of-range values. */
static int parse_long(const struct shell *sh, const char *arg, long min, long max, long *out)
{
	char *end;
	long v;

	errno = 0;
	v = strtol(arg, &end, 0);
	if (errno != 0 || end == arg || *end != '\0' || v < min || v > max) {
		shell_error(sh, "'%s': expected a number from %ld to %ld", arg, min, max);
		return -EINVAL;
	}
	*out = v;
	return 0;
}

static int cmd_load(const struct shell *sh, size_t argc, char **argv)
{
	unsigned int key;
	long v;

	ARG_UNUSED(argc);
	if (parse_long(sh, argv[1], 0, 1000000, &v) != 0) {
		return -EINVAL;
	}
	key = irq_lock();
	pmbus_dev.sim.load_ma = (int32_t)v;
	pmbus_device_update(&pmbus_dev, NULL);
	irq_unlock(key);
	shell_print(sh, "load %s mA", argv[1]);
	return 0;
}

static int cmd_vin(const struct shell *sh, size_t argc, char **argv)
{
	unsigned int key;
	long v;

	ARG_UNUSED(argc);
	if (parse_long(sh, argv[1], 0, 1000000, &v) != 0) {
		return -EINVAL;
	}
	key = irq_lock();
	pmbus_dev.sim.vin_mv = (int32_t)v;
	pmbus_device_update(&pmbus_dev, NULL);
	irq_unlock(key);
	shell_print(sh, "VIN %s mV", argv[1]);
	return 0;
}

static int cmd_temp(const struct shell *sh, size_t argc, char **argv)
{
	long i, mc;

	ARG_UNUSED(argc);
	if (parse_long(sh, argv[1], 1, 2, &i) != 0) {
		return -EINVAL;
	}
	if (strcmp(argv[2], "off") == 0) {
		temp_override[i - 1].active = false;
		shell_print(sh, "temperature %ld: real sensor", i);
	} else {
		if (parse_long(sh, argv[2], -273000, 1000000, &mc) != 0) {
			return -EINVAL;
		}
		temp_override[i - 1].mc = (int32_t)mc;
		temp_override[i - 1].active = true;
		shell_print(sh, "temperature %ld: %s m°C until 'pmbus temp %ld off'", i, argv[2],
			    i);
	}
	return 0;
}

static int cmd_control(const struct shell *sh, size_t argc, char **argv)
{
	unsigned int key;
	long v;

	ARG_UNUSED(argc);
	if (parse_long(sh, argv[1], 0, 1, &v) != 0) {
		return -EINVAL;
	}
	key = irq_lock();
	pmbus_device_set_control_pin(&pmbus_dev, v != 0);
	irq_unlock(key);
	shell_print(sh, "CONTROL %s", argv[1]);
	return 0;
}

static int cmd_clear(const struct shell *sh, size_t argc, char **argv)
{
	unsigned int key = irq_lock();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	pmbus_device_clear_faults(&pmbus_dev);
	irq_unlock(key);
	shell_print(sh, "faults cleared");
	return 0;
}

static int cmd_fan(const struct shell *sh, size_t argc, char **argv)
{
	unsigned int key;

	ARG_UNUSED(argc);
	if (strcmp(argv[1], "fail") != 0 && strcmp(argv[1], "ok") != 0) {
		shell_error(sh, "fan fail|ok");
		return -EINVAL;
	}
	key = irq_lock();
	pmbus_dev.sim.fan1_failed = strcmp(argv[1], "fail") == 0;
	irq_unlock(key);
	shell_print(sh, "fan 1 %s", argv[1]);
	return 0;
}

static int cmd_inject(const struct shell *sh, size_t argc, char **argv)
{
	static const char *const names[PMBUS_S_COUNT] = {
		"vout", "iout", "input", "temperature", "cml", "other", "mfr", "fans",
	};
	unsigned int key;
	long bits;

	ARG_UNUSED(argc);
	if (parse_long(sh, argv[2], 1, 0xFF, &bits) != 0) {
		return -EINVAL;
	}
	for (int i = 0; i < PMBUS_S_COUNT; i++) {
		if (strcmp(argv[1], names[i]) == 0) {
			key = irq_lock();
			pmbus_device_inject(&pmbus_dev, (enum pmbus_status_reg)i, (uint8_t)bits);
			irq_unlock(key);
			shell_print(sh, "STATUS_%s |= 0x%02lx", names[i], bits);
			return 0;
		}
	}
	shell_error(sh, "register: vout iout input temperature cml other mfr fans");
	return -EINVAL;
}

SHELL_STATIC_SUBCMD_SET_CREATE(pmbus_cmds,
	SHELL_CMD(status, NULL, "Telemetry, status registers and counters", cmd_status),
	SHELL_CMD_ARG(load, NULL, "<mA>  Simulated load current", cmd_load, 2, 0),
	SHELL_CMD_ARG(vin, NULL, "<mV>  Simulated input voltage", cmd_vin, 2, 0),
	SHELL_CMD_ARG(temp, NULL, "<1|2> <m°C|off>  Override a temperature sensor",
		      cmd_temp, 3, 0),
	SHELL_CMD_ARG(control, NULL, "<0|1>  The CONTROL input (see ON_OFF_CONFIG)",
		      cmd_control, 2, 0),
	SHELL_CMD_ARG(fan, NULL, "<fail|ok>  Stop or restart the simulated fan", cmd_fan, 2, 0),
	SHELL_CMD_ARG(inject, NULL, "<register> <bits>  Latch status bits (alert testing)",
		      cmd_inject, 3, 0),
	SHELL_CMD(clear, NULL, "CLEAR_FAULTS", cmd_clear),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(pmbus, &pmbus_cmds, "PMBus target", NULL);
