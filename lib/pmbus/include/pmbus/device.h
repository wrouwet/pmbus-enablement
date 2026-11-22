/*
 * A single-page PMBus device: a simulated 54 V to 12 V converter whose temperatures
 * come from real sensors.
 *
 * The model implements the commands, keeps limits and set points as the raw words the
 * host wrote (so reading back gives exactly what was written), latches status bits,
 * acts on faults as the fault response bytes say, sequences the output through
 * TON_DELAY/TON_RISE and TOFF_DELAY/TOFF_FALL, simulates a fan, and drives SMBALERT#.
 * Everything that touches hardware or storage goes through struct pmbus_device_ops.
 *
 * Concurrency: the I2C side (struct pmbus_target) calls into the device from interrupt
 * context. Callers in thread context must keep that interrupt out while they call
 * pmbus_device_* functions, e.g. with irq_lock() on Zephyr.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PMBUS_DEVICE_H_
#define PMBUS_DEVICE_H_

#include <stdbool.h>
#include <stdint.h>

#include <pmbus/target.h>

#define PMBUS_MFR_STR_MAX  20
#define PMBUS_USER_DATA_MAX 16
#define PMBUS_USER_DATA_COUNT 16

/*
 * The read/write words: set points, limits and timings, in the order struct
 * pmbus_device_config stores them. Output voltages are ULINEAR16 (VOUT_MODE), the rest
 * LINEAR11; times are in milliseconds.
 */
enum pmbus_word {
	PMBUS_W_VOUT_COMMAND,
	PMBUS_W_VOUT_MAX,
	PMBUS_W_VOUT_MARGIN_HIGH,
	PMBUS_W_VOUT_MARGIN_LOW,
	PMBUS_W_VOUT_MIN,
	PMBUS_W_VIN_ON,
	PMBUS_W_VIN_OFF,
	PMBUS_W_FAN_COMMAND_1,
	PMBUS_W_VOUT_OV_FAULT,
	PMBUS_W_VOUT_OV_WARN,
	PMBUS_W_VOUT_UV_WARN,
	PMBUS_W_VOUT_UV_FAULT,
	PMBUS_W_IOUT_OC_FAULT,
	PMBUS_W_IOUT_OC_LV_FAULT,
	PMBUS_W_IOUT_OC_WARN,
	PMBUS_W_OT_FAULT,
	PMBUS_W_OT_WARN,
	PMBUS_W_UT_WARN,
	PMBUS_W_UT_FAULT,
	PMBUS_W_VIN_OV_FAULT,
	PMBUS_W_VIN_OV_WARN,
	PMBUS_W_VIN_UV_WARN,
	PMBUS_W_VIN_UV_FAULT,
	PMBUS_W_IIN_OC_FAULT,
	PMBUS_W_IIN_OC_WARN,
	PMBUS_W_POWER_GOOD_ON,
	PMBUS_W_POWER_GOOD_OFF,
	PMBUS_W_TON_DELAY,
	PMBUS_W_TON_RISE,
	PMBUS_W_TON_MAX_FAULT,
	PMBUS_W_TOFF_DELAY,
	PMBUS_W_TOFF_FALL,
	PMBUS_W_TOFF_MAX_WARN,
	PMBUS_W_POUT_OP_FAULT,
	PMBUS_W_POUT_OP_WARN,
	PMBUS_W_PIN_OP_WARN,
	PMBUS_W_COUNT,
};

/* Faults with a response byte, in the order struct pmbus_device_config stores them. */
enum pmbus_fault {
	PMBUS_F_VOUT_OV,
	PMBUS_F_VOUT_UV,
	PMBUS_F_IOUT_OC,
	PMBUS_F_IOUT_OC_LV,
	PMBUS_F_OT,
	PMBUS_F_UT,
	PMBUS_F_VIN_OV,
	PMBUS_F_VIN_UV,
	PMBUS_F_IIN_OC,
	PMBUS_F_TON_MAX,
	PMBUS_F_POUT_OP,
	PMBUS_F_COUNT,
};

/* The latched status registers, each with its SMBALERT_MASK byte. */
enum pmbus_status_reg {
	PMBUS_S_VOUT,
	PMBUS_S_IOUT,
	PMBUS_S_INPUT,
	PMBUS_S_TEMPERATURE,
	PMBUS_S_CML,
	PMBUS_S_OTHER,
	PMBUS_S_MFR,
	PMBUS_S_FANS_1_2,
	PMBUS_S_COUNT,
};

enum pmbus_mfr_str {
	PMBUS_MFR_ID_STR,
	PMBUS_MFR_MODEL_STR,
	PMBUS_MFR_REVISION_STR,
	PMBUS_MFR_LOCATION_STR,
	PMBUS_MFR_DATE_STR,
	PMBUS_MFR_SERIAL_STR,
	PMBUS_MFR_STR_COUNT,
};

struct pmbus_mfr_string {
	uint8_t len;
	char s[PMBUS_MFR_STR_MAX];
};

struct pmbus_user_data {
	uint8_t len;
	uint8_t d[PMBUS_USER_DATA_MAX];
};

/* What STORE_DEFAULT_ALL saves and RESTORE_DEFAULT_ALL loads. */
struct pmbus_device_config {
	uint16_t version;
	uint8_t operation;
	uint8_t on_off_config;
	uint8_t write_protect;
	uint8_t fan_config_12;
	uint16_t words[PMBUS_W_COUNT];
	uint8_t response[PMBUS_F_COUNT];
	uint8_t alert_mask[PMBUS_S_COUNT];
	struct pmbus_mfr_string mfr[PMBUS_MFR_STR_COUNT];
	struct pmbus_user_data user[PMBUS_USER_DATA_COUNT];
};

#define PMBUS_DEVICE_CONFIG_VERSION 2

/* Says whether a config (e.g. one read back from flash) is safe to use. */
bool pmbus_device_config_valid(const struct pmbus_device_config *cfg);

struct pmbus_device_ops {
	/* SMBALERT#: true pulls the line low. */
	void (*set_alert)(void *ctx, bool asserted);
	/*
	 * STORE_DEFAULT_ALL. Called from interrupt context: copy the config and write it to
	 * flash later, then call pmbus_device_store_done().
	 */
	void (*store)(void *ctx, const struct pmbus_device_config *cfg);
	/* RESTORE_DEFAULT_ALL. Returns false if nothing was stored. Must be quick. */
	bool (*restore)(void *ctx, struct pmbus_device_config *cfg);
};

/* Readings from real sensors, in milli-degrees Celsius. */
struct pmbus_sensors {
	bool temp1_valid;
	int32_t temp1_mc;
	bool temp2_valid;
	int32_t temp2_mc;
};

/* The simulated side, in milli-units. */
struct pmbus_sim {
	int32_t vin_mv;       /* input voltage */
	int32_t load_ma;      /* output current at the set point (a resistive load) */
	int32_t eff_permille; /* conversion efficiency */
	bool fan1_failed;     /* the fan stops */
};

enum pmbus_output_state {
	PMBUS_OUT_OFF,
	PMBUS_OUT_TON_DELAY,
	PMBUS_OUT_RISING,
	PMBUS_OUT_ON,
	PMBUS_OUT_TOFF_DELAY,
	PMBUS_OUT_FALLING,
};

/* What the device tracks for each fault between evaluations. */
struct pmbus_fault_state {
	bool present;      /* the condition holds now */
	bool holding;      /* keeping the output off until retry_ms runs out */
	uint8_t retries;   /* restarts tried since the unit was last enabled */
	uint32_t delay_ms; /* how long the condition has held, for delayed responses */
	uint32_t retry_ms; /* time left before the next restart attempt */
};

struct pmbus_device {
	struct pmbus_target target;
	const struct pmbus_device_ops *ops;
	void *ops_ctx;

	struct pmbus_device_config cfg;
	bool has_alert_pin;
	bool control_pin; /* the CONTROL input; true means asserted */
	/* MFR_SPECIFIC_00..03 (load, VIN, fan failure, status injection) exist on the bus. */
	bool sim_knobs;

	struct pmbus_sensors sensors;
	struct pmbus_sim sim;

	/* Live telemetry. */
	int32_t vin_mv, iin_ma, vout_mv, iout_ma, pout_mw, pin_mw;
	int32_t fan1_rpm;
	bool output_on;   /* the output is powered (any state but OFF and TON_DELAY) */
	bool power_good;
	bool current_limited;
	bool vin_low;     /* below VIN_OFF, and not yet back above VIN_ON */
	bool fault_off;   /* latched off by a fault until the unit is turned off and on */
	bool busy;

	enum pmbus_output_state out_state;
	uint32_t state_ms;    /* time in out_state */
	int32_t fall_from_mv; /* VOUT when TOFF_FALL began */
	uint32_t fan1_low_ms; /* how long the fan has run below half its target */

	struct pmbus_fault_state fault[PMBUS_F_COUNT];

	/* Latched status registers. */
	uint8_t status[PMBUS_S_COUNT];
	bool alert_pending;
};

/*
 * Sets the device up with the built-in defaults: VOUT 12 V, VIN 54 V, a 5 A load and
 * dummy identification strings. serial may be NULL.
 */
void pmbus_device_init(struct pmbus_device *dev, uint8_t addr, const struct pmbus_device_ops *ops,
		       void *ops_ctx, bool has_alert_pin, const char *serial);

/* Loads the stored config, if there is a valid one. Call once after pmbus_device_init(). */
void pmbus_device_load_stored(struct pmbus_device *dev);

/*
 * Takes new sensor readings (sensors may be NULL), recomputes telemetry and evaluates
 * the limits, without letting time pass.
 */
void pmbus_device_update(struct pmbus_device *dev, const struct pmbus_sensors *sensors);

/* Lets ms milliseconds pass: output sequencing, fault delays and retries, the fan. */
void pmbus_device_advance(struct pmbus_device *dev, uint32_t ms);

/* Called once a store that pmbus_device_ops.store started has finished. */
void pmbus_device_store_done(struct pmbus_device *dev, bool ok);

void pmbus_device_set_control_pin(struct pmbus_device *dev, bool asserted);

/* Latches status bits, e.g. to test a host's alert handling. */
void pmbus_device_inject(struct pmbus_device *dev, enum pmbus_status_reg reg, uint8_t bits);

uint8_t pmbus_device_status_byte(const struct pmbus_device *dev);
uint16_t pmbus_device_status_word(const struct pmbus_device *dev);
void pmbus_device_clear_faults(struct pmbus_device *dev);

/* VOUT_MODE is fixed: linear, exponent -9 (1.95 mV steps, up to 128 V). */
#define PMBUS_DEVICE_VOUT_MODE 0x17

/* The time unit of bits 2:0 of a fault response byte: the delay and the retry interval. */
#define PMBUS_DEVICE_RSP_UNIT_MS 100

/* The simulated fan: full speed, and the thermal curve's end points. */
#define PMBUS_DEVICE_FAN_MAX_RPM 18000

#endif /* PMBUS_DEVICE_H_ */
