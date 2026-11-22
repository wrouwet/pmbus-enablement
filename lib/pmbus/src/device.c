/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <pmbus/device.h>
#include <pmbus/format.h>
#include <pmbus/pmbus.h>

#define VOUT_EXP (-9)
#define V(mv)    pmbus_ulinear16_encode((mv), VOUT_EXP)
#define L(milli) pmbus_linear11_encode(milli)

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* Standby power drawn from the input while the output is off. */
#define STANDBY_PIN_MW 500

/* The fan: thermal curve from 30 % at 30 °C to 100 % at 70 °C, time constant 1 s. */
#define FAN_MIN_DUTY   30
#define FAN_CURVE_LO   30000
#define FAN_CURVE_HI   70000
#define FAN_TAU_MS     1000
#define FAN_BAD_MS     2000

/* --- The command set: words, faults, status registers --- */

enum fmt { FMT_VOUT, FMT_LIN };

struct word_def {
	uint8_t code;
	uint8_t fmt;
	int32_t def; /* default, in milli-units (ms for times) */
};

static const struct word_def word_defs[PMBUS_W_COUNT] = {
	[PMBUS_W_VOUT_COMMAND] = {PMBUS_VOUT_COMMAND, FMT_VOUT, 12000},
	[PMBUS_W_VOUT_MAX] = {PMBUS_VOUT_MAX, FMT_VOUT, 14400},
	[PMBUS_W_VOUT_MARGIN_HIGH] = {PMBUS_VOUT_MARGIN_HIGH, FMT_VOUT, 12600},
	[PMBUS_W_VOUT_MARGIN_LOW] = {PMBUS_VOUT_MARGIN_LOW, FMT_VOUT, 11400},
	[PMBUS_W_VOUT_MIN] = {PMBUS_VOUT_MIN, FMT_VOUT, 9600},
	[PMBUS_W_VIN_ON] = {PMBUS_VIN_ON, FMT_LIN, 43000},
	[PMBUS_W_VIN_OFF] = {PMBUS_VIN_OFF, FMT_LIN, 41000},
	[PMBUS_W_FAN_COMMAND_1] = {PMBUS_FAN_COMMAND_1, FMT_LIN, 0},
	[PMBUS_W_VOUT_OV_FAULT] = {PMBUS_VOUT_OV_FAULT_LIMIT, FMT_VOUT, 13800},
	[PMBUS_W_VOUT_OV_WARN] = {PMBUS_VOUT_OV_WARN_LIMIT, FMT_VOUT, 13200},
	[PMBUS_W_VOUT_UV_WARN] = {PMBUS_VOUT_UV_WARN_LIMIT, FMT_VOUT, 10800},
	[PMBUS_W_VOUT_UV_FAULT] = {PMBUS_VOUT_UV_FAULT_LIMIT, FMT_VOUT, 10200},
	[PMBUS_W_IOUT_OC_FAULT] = {PMBUS_IOUT_OC_FAULT_LIMIT, FMT_LIN, 10000},
	[PMBUS_W_IOUT_OC_LV_FAULT] = {PMBUS_IOUT_OC_LV_FAULT_LIMIT, FMT_VOUT, 6000},
	[PMBUS_W_IOUT_OC_WARN] = {PMBUS_IOUT_OC_WARN_LIMIT, FMT_LIN, 8000},
	[PMBUS_W_OT_FAULT] = {PMBUS_OT_FAULT_LIMIT, FMT_LIN, 100000},
	[PMBUS_W_OT_WARN] = {PMBUS_OT_WARN_LIMIT, FMT_LIN, 85000},
	[PMBUS_W_UT_WARN] = {PMBUS_UT_WARN_LIMIT, FMT_LIN, -20000},
	[PMBUS_W_UT_FAULT] = {PMBUS_UT_FAULT_LIMIT, FMT_LIN, -40000},
	[PMBUS_W_VIN_OV_FAULT] = {PMBUS_VIN_OV_FAULT_LIMIT, FMT_LIN, 60000},
	[PMBUS_W_VIN_OV_WARN] = {PMBUS_VIN_OV_WARN_LIMIT, FMT_LIN, 58000},
	[PMBUS_W_VIN_UV_WARN] = {PMBUS_VIN_UV_WARN_LIMIT, FMT_LIN, 44000},
	[PMBUS_W_VIN_UV_FAULT] = {PMBUS_VIN_UV_FAULT_LIMIT, FMT_LIN, 40000},
	[PMBUS_W_IIN_OC_FAULT] = {PMBUS_IIN_OC_FAULT_LIMIT, FMT_LIN, 3000},
	[PMBUS_W_IIN_OC_WARN] = {PMBUS_IIN_OC_WARN_LIMIT, FMT_LIN, 2500},
	[PMBUS_W_POWER_GOOD_ON] = {PMBUS_POWER_GOOD_ON, FMT_VOUT, 11000},
	[PMBUS_W_POWER_GOOD_OFF] = {PMBUS_POWER_GOOD_OFF, FMT_VOUT, 10500},
	[PMBUS_W_TON_DELAY] = {PMBUS_TON_DELAY, FMT_LIN, 0},
	[PMBUS_W_TON_RISE] = {PMBUS_TON_RISE, FMT_LIN, 0},
	[PMBUS_W_TON_MAX_FAULT] = {PMBUS_TON_MAX_FAULT_LIMIT, FMT_LIN, 0},
	[PMBUS_W_TOFF_DELAY] = {PMBUS_TOFF_DELAY, FMT_LIN, 0},
	[PMBUS_W_TOFF_FALL] = {PMBUS_TOFF_FALL, FMT_LIN, 0},
	[PMBUS_W_TOFF_MAX_WARN] = {PMBUS_TOFF_MAX_WARN_LIMIT, FMT_LIN, 0},
	[PMBUS_W_POUT_OP_FAULT] = {PMBUS_POUT_OP_FAULT_LIMIT, FMT_LIN, 132000},
	[PMBUS_W_POUT_OP_WARN] = {PMBUS_POUT_OP_WARN_LIMIT, FMT_LIN, 115000},
	[PMBUS_W_PIN_OP_WARN] = {PMBUS_PIN_OP_WARN_LIMIT, FMT_LIN, 125000},
};

struct fault_def {
	uint8_t code; /* the response command */
	uint8_t def;  /* default response */
	uint8_t reg;  /* enum pmbus_status_reg */
	uint8_t bit;
};

/*
 * Defaults: output faults and over-temperature latch off, as most converters do; input
 * under-voltage keeps the output off while present; the rest only report.
 */
static const struct fault_def fault_defs[PMBUS_F_COUNT] = {
	[PMBUS_F_VOUT_OV] = {PMBUS_VOUT_OV_FAULT_RESPONSE, 0x80, PMBUS_S_VOUT, PMBUS_SV_OV_FAULT},
	[PMBUS_F_VOUT_UV] = {PMBUS_VOUT_UV_FAULT_RESPONSE, 0x80, PMBUS_S_VOUT, PMBUS_SV_UV_FAULT},
	[PMBUS_F_IOUT_OC] = {PMBUS_IOUT_OC_FAULT_RESPONSE, 0xC0, PMBUS_S_IOUT, PMBUS_SI_OC_FAULT},
	[PMBUS_F_IOUT_OC_LV] = {PMBUS_IOUT_OC_LV_FAULT_RESPONSE, 0x80, PMBUS_S_IOUT,
				PMBUS_SI_OC_LV_FAULT},
	[PMBUS_F_OT] = {PMBUS_OT_FAULT_RESPONSE, 0x80, PMBUS_S_TEMPERATURE, PMBUS_ST_OT_FAULT},
	[PMBUS_F_UT] = {PMBUS_UT_FAULT_RESPONSE, 0x00, PMBUS_S_TEMPERATURE, PMBUS_ST_UT_FAULT},
	[PMBUS_F_VIN_OV] = {PMBUS_VIN_OV_FAULT_RESPONSE, 0x80, PMBUS_S_INPUT, PMBUS_SIN_OV_FAULT},
	[PMBUS_F_VIN_UV] = {PMBUS_VIN_UV_FAULT_RESPONSE, 0xC0, PMBUS_S_INPUT, PMBUS_SIN_UV_FAULT},
	[PMBUS_F_IIN_OC] = {PMBUS_IIN_OC_FAULT_RESPONSE, 0x00, PMBUS_S_INPUT,
			    PMBUS_SIN_IIN_OC_FAULT},
	[PMBUS_F_TON_MAX] = {PMBUS_TON_MAX_FAULT_RESPONSE, 0x80, PMBUS_S_VOUT,
			     PMBUS_SV_TON_MAX_FAULT},
	[PMBUS_F_POUT_OP] = {PMBUS_POUT_OP_FAULT_RESPONSE, 0x00, PMBUS_S_IOUT,
			     PMBUS_SI_POUT_OP_FAULT},
};

static const uint8_t status_codes[PMBUS_S_COUNT] = {
	[PMBUS_S_VOUT] = PMBUS_STATUS_VOUT,
	[PMBUS_S_IOUT] = PMBUS_STATUS_IOUT,
	[PMBUS_S_INPUT] = PMBUS_STATUS_INPUT,
	[PMBUS_S_TEMPERATURE] = PMBUS_STATUS_TEMPERATURE,
	[PMBUS_S_CML] = PMBUS_STATUS_CML,
	[PMBUS_S_OTHER] = PMBUS_STATUS_OTHER,
	[PMBUS_S_MFR] = PMBUS_STATUS_MFR_SPECIFIC,
	[PMBUS_S_FANS_1_2] = PMBUS_STATUS_FANS_1_2,
};

/* Ratings, read-only. */
static const struct word_def rating_defs[] = {
	{PMBUS_MFR_VIN_MIN, FMT_LIN, 40000},
	{PMBUS_MFR_VIN_MAX, FMT_LIN, 60000},
	{PMBUS_MFR_IIN_MAX, FMT_LIN, 3000},
	{PMBUS_MFR_PIN_MAX, FMT_LIN, 130000},
	{PMBUS_MFR_VOUT_MIN, FMT_VOUT, 9600},
	{PMBUS_MFR_VOUT_MAX, FMT_VOUT, 14400},
	{PMBUS_MFR_IOUT_MAX, FMT_LIN, 10000},
	{PMBUS_MFR_POUT_MAX, FMT_LIN, 120000},
	{PMBUS_MFR_MAX_TEMP_1, FMT_LIN, 100000},
	{PMBUS_MFR_MAX_TEMP_2, FMT_LIN, 105000},
};

static const char ic_device_id[] = "MCXA153";
static const char ic_device_rev[] = "0";

static const struct pmbus_device_config default_strings = {
	.mfr = {
		[PMBUS_MFR_ID_STR] = {7, "EXAMPLE"},
		[PMBUS_MFR_MODEL_STR] = {13, "MCXA153-PMB-1"},
		[PMBUS_MFR_REVISION_STR] = {2, "A0"},
		[PMBUS_MFR_LOCATION_STR] = {3, "LAB"},
		[PMBUS_MFR_DATE_STR] = {6, "261122"},
		[PMBUS_MFR_SERIAL_STR] = {8, "00000000"},
	},
};

static int32_t sat32(int64_t v)
{
	return (v > INT32_MAX) ? INT32_MAX : (v < INT32_MIN) ? INT32_MIN : (int32_t)v;
}

static uint16_t encode(uint8_t fmt, int32_t milli)
{
	return (fmt == FMT_VOUT) ? V(milli) : L(milli);
}

static void set_defaults(struct pmbus_device_config *c)
{
	*c = default_strings;
	c->version = PMBUS_DEVICE_CONFIG_VERSION;
	c->operation = PMBUS_OPERATION_ON;
	c->on_off_config = PMBUS_ON_OFF_PU | PMBUS_ON_OFF_CMD;
	c->write_protect = 0;
	c->fan_config_12 = PMBUS_FAN1_INSTALLED | 0x10; /* duty cycle, 2 pulses per turn */
	for (size_t i = 0; i < PMBUS_W_COUNT; i++) {
		c->words[i] = encode(word_defs[i].fmt, word_defs[i].def);
	}
	for (size_t i = 0; i < PMBUS_F_COUNT; i++) {
		c->response[i] = fault_defs[i].def;
	}
}

bool pmbus_device_config_valid(const struct pmbus_device_config *cfg)
{
	uint8_t wp = cfg->write_protect;

	if (cfg->version != PMBUS_DEVICE_CONFIG_VERSION ||
	    (wp != 0 && wp != PMBUS_WP_ALL && wp != PMBUS_WP_OP && wp != PMBUS_WP_VOUT_CMD) ||
	    (cfg->on_off_config & ~0x1F) != 0 || (cfg->fan_config_12 & 0x0F) != 0) {
		return false;
	}
	for (size_t i = 0; i < PMBUS_MFR_STR_COUNT; i++) {
		if (cfg->mfr[i].len > PMBUS_MFR_STR_MAX) {
			return false;
		}
	}
	for (size_t i = 0; i < PMBUS_USER_DATA_COUNT; i++) {
		if (cfg->user[i].len > PMBUS_USER_DATA_MAX) {
			return false;
		}
	}
	return true;
}

/* A word as milli-units. */
static int32_t word(const struct pmbus_device *dev, enum pmbus_word w)
{
	uint16_t raw = dev->cfg.words[w];

	return (word_defs[w].fmt == FMT_VOUT) ? pmbus_ulinear16_decode(raw, VOUT_EXP)
					      : pmbus_linear11_decode(raw);
}

/* A time word in ms; negative values count as 0. */
static uint32_t ms_word(const struct pmbus_device *dev, enum pmbus_word w)
{
	int32_t v = word(dev, w);

	return (v <= 0) ? 0 : (uint32_t)(v / 1000);
}

/* --- Status and alerts --- */

static bool any_unmasked_status(const struct pmbus_device *dev)
{
	for (size_t i = 0; i < PMBUS_S_COUNT; i++) {
		if (dev->status[i] & ~dev->cfg.alert_mask[i]) {
			return true;
		}
	}
	return false;
}

static void latch(struct pmbus_device *dev, enum pmbus_status_reg reg, uint8_t bits)
{
	uint8_t fresh = bits & ~dev->status[reg];

	dev->status[reg] |= bits;
	if ((fresh & ~dev->cfg.alert_mask[reg]) && dev->has_alert_pin) {
		dev->alert_pending = true;
		dev->ops->set_alert(dev->ops_ctx, true);
	}
}

static void release_alert_if_clear(struct pmbus_device *dev)
{
	if (dev->alert_pending && !any_unmasked_status(dev)) {
		dev->alert_pending = false;
		dev->ops->set_alert(dev->ops_ctx, false);
	}
}

void pmbus_device_inject(struct pmbus_device *dev, enum pmbus_status_reg reg, uint8_t bits)
{
	if ((unsigned int)reg < PMBUS_S_COUNT) {
		latch(dev, reg, bits);
	}
}

void pmbus_device_clear_faults(struct pmbus_device *dev)
{
	memset(dev->status, 0, sizeof(dev->status));
	release_alert_if_clear(dev);
}

uint8_t pmbus_device_status_byte(const struct pmbus_device *dev)
{
	const uint8_t *s = dev->status;
	uint8_t sb = 0;

	if (dev->busy) {
		sb |= PMBUS_SB_BUSY;
	}
	if (!dev->output_on) {
		sb |= PMBUS_SB_OFF;
	}
	if (s[PMBUS_S_VOUT] & PMBUS_SV_OV_FAULT) {
		sb |= PMBUS_SB_VOUT_OV_FAULT;
	}
	if (s[PMBUS_S_IOUT] & PMBUS_SI_OC_FAULT) {
		sb |= PMBUS_SB_IOUT_OC_FAULT;
	}
	if (s[PMBUS_S_INPUT] & PMBUS_SIN_UV_FAULT) {
		sb |= PMBUS_SB_VIN_UV_FAULT;
	}
	if (s[PMBUS_S_TEMPERATURE]) {
		sb |= PMBUS_SB_TEMPERATURE;
	}
	if (s[PMBUS_S_CML]) {
		sb |= PMBUS_SB_CML;
	}
	if ((s[PMBUS_S_VOUT] & ~PMBUS_SV_OV_FAULT) || (s[PMBUS_S_IOUT] & ~PMBUS_SI_OC_FAULT) ||
	    (s[PMBUS_S_INPUT] & ~PMBUS_SIN_UV_FAULT) || s[PMBUS_S_MFR] || s[PMBUS_S_OTHER] ||
	    s[PMBUS_S_FANS_1_2]) {
		sb |= PMBUS_SB_NONE_OF_ABOVE;
	}
	return sb;
}

uint16_t pmbus_device_status_word(const struct pmbus_device *dev)
{
	const uint8_t *s = dev->status;
	uint16_t sw = pmbus_device_status_byte(dev);

	if (s[PMBUS_S_VOUT]) {
		sw |= PMBUS_SW_VOUT;
	}
	if (s[PMBUS_S_IOUT]) {
		sw |= PMBUS_SW_IOUT_POUT;
	}
	if (s[PMBUS_S_INPUT]) {
		sw |= PMBUS_SW_INPUT;
	}
	if (s[PMBUS_S_MFR]) {
		sw |= PMBUS_SW_MFR;
	}
	if (!dev->power_good) {
		sw |= PMBUS_SW_POWER_GOOD_N;
	}
	if (s[PMBUS_S_FANS_1_2]) {
		sw |= PMBUS_SW_FANS;
	}
	if (s[PMBUS_S_OTHER]) {
		sw |= PMBUS_SW_OTHER;
	}
	return sw;
}

/* --- The simulated converter --- */

static bool commanded_on(const struct pmbus_device *dev)
{
	uint8_t c = dev->cfg.on_off_config;

	if (!(c & PMBUS_ON_OFF_PU)) {
		return true;
	}
	if ((c & PMBUS_ON_OFF_CMD) && !(dev->cfg.operation & PMBUS_OPERATION_ON)) {
		return false;
	}
	if ((c & PMBUS_ON_OFF_CPR) && !dev->control_pin) {
		return false;
	}
	return (c & (PMBUS_ON_OFF_CMD | PMBUS_ON_OFF_CPR)) != 0;
}

/* When commanded off, whether to go through TOFF_DELAY/TOFF_FALL. */
static bool soft_off(const struct pmbus_device *dev)
{
	uint8_t c = dev->cfg.on_off_config;
	bool by_op = (c & PMBUS_ON_OFF_CMD) && !(dev->cfg.operation & PMBUS_OPERATION_ON);
	bool by_pin = (c & PMBUS_ON_OFF_CPR) && !dev->control_pin;

	if (by_op && !(dev->cfg.operation & PMBUS_OPERATION_SOFT_OFF)) {
		return false;
	}
	if (by_pin && (c & PMBUS_ON_OFF_CPA)) {
		return false;
	}
	return by_op || by_pin;
}

/* Something other than the on/off command keeps the output off. */
static bool blocked(const struct pmbus_device *dev)
{
	if (dev->fault_off || dev->vin_low) {
		return true;
	}
	for (size_t i = 0; i < PMBUS_F_COUNT; i++) {
		const struct pmbus_fault_state *f = &dev->fault[i];

		if (f->holding ||
		    (f->present && PMBUS_RSP(dev->cfg.response[i]) == PMBUS_RSP_INHIBIT &&
		     i != PMBUS_F_IOUT_OC)) {
			return true;
		}
	}
	return false;
}

static bool margined(const struct pmbus_device *dev)
{
	return (dev->cfg.operation & PMBUS_OPERATION_MARGIN_MASK) == PMBUS_OPERATION_MARGIN_LOW ||
	       (dev->cfg.operation & PMBUS_OPERATION_MARGIN_MASK) == PMBUS_OPERATION_MARGIN_HIGH;
}

/* The selected set point, before VOUT_MAX/VOUT_MIN. */
static int32_t requested_mv(const struct pmbus_device *dev)
{
	switch (dev->cfg.operation & PMBUS_OPERATION_MARGIN_MASK) {
	case PMBUS_OPERATION_MARGIN_LOW:
		return word(dev, PMBUS_W_VOUT_MARGIN_LOW);
	case PMBUS_OPERATION_MARGIN_HIGH:
		return word(dev, PMBUS_W_VOUT_MARGIN_HIGH);
	default:
		return word(dev, PMBUS_W_VOUT_COMMAND);
	}
}

static int32_t setpoint_mv(const struct pmbus_device *dev)
{
	int32_t v = requested_mv(dev);
	int32_t hi = word(dev, PMBUS_W_VOUT_MAX);
	int32_t lo = word(dev, PMBUS_W_VOUT_MIN);

	if (v > hi) {
		v = hi;
	}
	if (v < lo) {
		v = lo;
	}
	return v;
}

static void enter(struct pmbus_device *dev, enum pmbus_output_state s)
{
	dev->out_state = s;
	dev->state_ms = 0;
}

/* Runs the state's timer. Returns true if it ran out; *dt then holds the time left over. */
static bool timer_done(struct pmbus_device *dev, uint32_t *dt, uint32_t duration)
{
	/* state_ms < duration here, and duration < 2^31: no overflow while *dt < 2^31. */
	if (*dt >= duration - dev->state_ms) {
		*dt = dev->state_ms + *dt - duration;
		return true;
	}
	dev->state_ms += *dt;
	*dt = 0;
	return false;
}

static int32_t vout_now(const struct pmbus_device *dev)
{
	int32_t set = setpoint_mv(dev);
	uint32_t rise = ms_word(dev, PMBUS_W_TON_RISE);
	uint32_t fall = ms_word(dev, PMBUS_W_TOFF_FALL);

	switch (dev->out_state) {
	case PMBUS_OUT_RISING:
		return (rise == 0) ? set : (int32_t)((int64_t)set * dev->state_ms / rise);
	case PMBUS_OUT_ON:
	case PMBUS_OUT_TOFF_DELAY:
		return set;
	case PMBUS_OUT_FALLING:
		if (fall == 0 || dev->state_ms >= fall) {
			return 0;
		}
		return (int32_t)((int64_t)dev->fall_from_mv * (fall - dev->state_ms) / fall);
	default:
		return 0;
	}
}

/* Moves the output through its states. Zero-length states pass at once. */
static void step_output(struct pmbus_device *dev, uint32_t dt)
{
	for (int guard = 0; guard < 8; guard++) {
		enum pmbus_output_state before = dev->out_state;
		bool block = blocked(dev);
		bool run = commanded_on(dev) && !block;

		if (!run && dev->out_state != PMBUS_OUT_OFF && (block || !soft_off(dev))) {
			enter(dev, PMBUS_OUT_OFF);
			continue;
		}
		switch (dev->out_state) {
		case PMBUS_OUT_OFF:
			if (run) {
				enter(dev, PMBUS_OUT_TON_DELAY);
			}
			break;
		case PMBUS_OUT_TON_DELAY:
			if (!run) {
				enter(dev, PMBUS_OUT_OFF);
			} else if (timer_done(dev, &dt, ms_word(dev, PMBUS_W_TON_DELAY))) {
				enter(dev, PMBUS_OUT_RISING);
			}
			break;
		case PMBUS_OUT_RISING:
			if (!run) {
				enter(dev, PMBUS_OUT_TOFF_DELAY);
			} else if (timer_done(dev, &dt, ms_word(dev, PMBUS_W_TON_RISE))) {
				enter(dev, PMBUS_OUT_ON);
			}
			break;
		case PMBUS_OUT_ON:
			if (!run) {
				enter(dev, PMBUS_OUT_TOFF_DELAY);
			}
			break;
		case PMBUS_OUT_TOFF_DELAY:
			if (run) {
				enter(dev, PMBUS_OUT_ON);
			} else if (timer_done(dev, &dt, ms_word(dev, PMBUS_W_TOFF_DELAY))) {
				dev->fall_from_mv = setpoint_mv(dev);
				enter(dev, PMBUS_OUT_FALLING);
			}
			break;
		case PMBUS_OUT_FALLING:
			if (run) {
				enter(dev, PMBUS_OUT_OFF);
			} else if (timer_done(dev, &dt, ms_word(dev, PMBUS_W_TOFF_FALL))) {
				enter(dev, PMBUS_OUT_OFF);
			}
			break;
		}
		if (dev->out_state == before) {
			break;
		}
	}
}

static bool regulating(const struct pmbus_device *dev)
{
	return dev->out_state == PMBUS_OUT_ON || dev->out_state == PMBUS_OUT_TOFF_DELAY;
}

/* IOUT_OC_FAULT_RESPONSE asks for constant current rather than an immediate shutdown. */
static bool cc_mode(const struct pmbus_device *dev)
{
	return PMBUS_RSP(dev->cfg.response[PMBUS_F_IOUT_OC]) != PMBUS_RSP_IOC_OFF;
}

static void compute_electricals(struct pmbus_device *dev)
{
	int32_t set = setpoint_mv(dev);
	int32_t v = vout_now(dev);
	int64_t demand;

	if (dev->sim.vin_mv < word(dev, PMBUS_W_VIN_OFF)) {
		dev->vin_low = true;
	} else if (dev->sim.vin_mv >= word(dev, PMBUS_W_VIN_ON)) {
		dev->vin_low = false;
	}

	dev->output_on = dev->out_state != PMBUS_OUT_OFF && dev->out_state != PMBUS_OUT_TON_DELAY;
	dev->vin_mv = dev->sim.vin_mv;

	/* The load is a resistor that draws load_ma at the set point. */
	demand = (set > 0) ? (int64_t)dev->sim.load_ma * v / set : 0;
	dev->current_limited = false;
	if (dev->output_on && cc_mode(dev) && demand > word(dev, PMBUS_W_IOUT_OC_FAULT) &&
	    demand > 0) {
		int32_t limit = word(dev, PMBUS_W_IOUT_OC_FAULT);

		dev->current_limited = true;
		v = (int32_t)((int64_t)v * limit / demand);
		demand = limit;
	}
	if (!dev->output_on) {
		v = 0;
		demand = 0;
	}
	/* Host-set knobs can be huge: saturate rather than wrap. */
	dev->vout_mv = v;
	dev->iout_ma = sat32(demand);
	dev->pout_mw = sat32((int64_t)dev->vout_mv * dev->iout_ma / 1000);
	dev->pin_mw = sat32((dev->output_on ? (int64_t)dev->pout_mw * 1000 / dev->sim.eff_permille
					    : 0) +
			    STANDBY_PIN_MW);
	dev->iin_ma = (dev->vin_mv > 0) ? sat32((int64_t)dev->pin_mw * 1000 / dev->vin_mv) : 0;

	/* Hysteresis: asserts at POWER_GOOD_ON, deasserts below POWER_GOOD_OFF. */
	if (!regulating(dev)) {
		dev->power_good = false;
	} else if (dev->power_good) {
		dev->power_good = dev->vout_mv >= word(dev, PMBUS_W_POWER_GOOD_OFF);
	} else {
		dev->power_good = dev->vout_mv >= word(dev, PMBUS_W_POWER_GOOD_ON);
	}
}

/* Re-arms every fault response, as turning the unit off and on does. */
static void rearm(struct pmbus_device *dev)
{
	dev->fault_off = false;
	for (size_t i = 0; i < PMBUS_F_COUNT; i++) {
		dev->fault[i].holding = false;
		dev->fault[i].retries = 0;
		dev->fault[i].retry_ms = 0;
		dev->fault[i].delay_ms = 0;
	}
}

/* Shuts the output down for fault f and arranges the retries its response asks for. */
static void shut_down(struct pmbus_device *dev, enum pmbus_fault f)
{
	struct pmbus_fault_state *fs = &dev->fault[f];
	uint8_t rsp = dev->cfg.response[f];
	uint8_t retry = PMBUS_RSP_RETRY(rsp);

	/* A unit that is off anyway only reports the fault. */
	if (fs->holding || dev->fault_off || !commanded_on(dev)) {
		return;
	}
	fs->delay_ms = 0;
	if (retry == PMBUS_RETRY_NONE || (retry != PMBUS_RETRY_FOREVER && fs->retries >= retry)) {
		dev->fault_off = true;
		return;
	}
	if (fs->retries < 0xFF) {
		fs->retries++;
	}
	fs->holding = true;
	fs->retry_ms = PMBUS_RSP_DELAY(rsp) * PMBUS_DEVICE_RSP_UNIT_MS;
}

/* Acts on fault f being present or not, dt ms after the last evaluation. */
static void respond(struct pmbus_device *dev, enum pmbus_fault f, bool present, uint32_t dt)
{
	struct pmbus_fault_state *fs = &dev->fault[f];
	uint8_t rsp = dev->cfg.response[f];
	uint32_t delay = PMBUS_RSP_DELAY(rsp) * PMBUS_DEVICE_RSP_UNIT_MS;

	fs->present = present;
	if (!present) {
		fs->delay_ms = 0;
		if (regulating(dev)) {
			fs->retries = 0; /* the restart worked */
		}
		return;
	}
	latch(dev, (enum pmbus_status_reg)fault_defs[f].reg, fault_defs[f].bit);

	if (f == PMBUS_F_IOUT_OC) {
		switch (PMBUS_RSP(rsp)) {
		case PMBUS_RSP_IOC_CC:
		case PMBUS_RSP_IOC_CC_LV: /* the LV fault acts on its own */
			return;
		case PMBUS_RSP_IOC_CC_DLY:
			fs->delay_ms += dt;
			if (fs->delay_ms >= delay) {
				shut_down(dev, f);
			}
			return;
		default:
			shut_down(dev, f);
			return;
		}
	}
	switch (PMBUS_RSP(rsp)) {
	case PMBUS_RSP_DELAYED:
		fs->delay_ms += dt;
		if (fs->delay_ms >= delay) {
			shut_down(dev, f);
		}
		break;
	case PMBUS_RSP_SHUTDOWN:
		shut_down(dev, f);
		break;
	default: /* CONTINUE, or INHIBIT, which blocked() handles */
		break;
	}
}

static void run_retry_timers(struct pmbus_device *dev, uint32_t dt)
{
	for (size_t i = 0; i < PMBUS_F_COUNT; i++) {
		struct pmbus_fault_state *fs = &dev->fault[i];

		if (fs->holding) {
			if (fs->retry_ms > dt) {
				fs->retry_ms -= dt;
			} else {
				fs->holding = false;
				fs->retry_ms = 0;
			}
		}
	}
}

static void evaluate_limits(struct pmbus_device *dev, uint32_t dt)
{
	bool reg = regulating(dev);
	bool vout_checked = !(margined(dev) && (dev->cfg.operation & PMBUS_OPERATION_MFAULT_MASK) ==
							     PMBUS_OPERATION_MFAULT_IGNORE);
	int32_t set = setpoint_mv(dev);
	int32_t req = requested_mv(dev);
	int64_t demand = (set > 0) ? (int64_t)dev->sim.load_ma * vout_now(dev) / set : 0;

	/* Output voltage: over-voltage whenever powered, under-voltage once regulating. */
	bool ov = dev->output_on && vout_checked &&
		  dev->vout_mv > word(dev, PMBUS_W_VOUT_OV_FAULT);
	bool uv = reg && vout_checked && dev->vout_mv < word(dev, PMBUS_W_VOUT_UV_FAULT);

	if (dev->output_on && vout_checked && !ov &&
	    dev->vout_mv > word(dev, PMBUS_W_VOUT_OV_WARN)) {
		latch(dev, PMBUS_S_VOUT, PMBUS_SV_OV_WARN);
	}
	if (reg && vout_checked && !uv && dev->vout_mv < word(dev, PMBUS_W_VOUT_UV_WARN)) {
		latch(dev, PMBUS_S_VOUT, PMBUS_SV_UV_WARN);
	}
	if (commanded_on(dev) &&
	    (req > word(dev, PMBUS_W_VOUT_MAX) || req < word(dev, PMBUS_W_VOUT_MIN))) {
		latch(dev, PMBUS_S_VOUT, PMBUS_SV_MAX_MIN_WARN);
	}
	respond(dev, PMBUS_F_VOUT_OV, ov, dt);
	respond(dev, PMBUS_F_VOUT_UV, uv, dt);

	/* Start-up and shut-down timing. */
	uint32_t ton_max = ms_word(dev, PMBUS_W_TON_MAX_FAULT);
	uint32_t toff_max = ms_word(dev, PMBUS_W_TOFF_MAX_WARN);

	respond(dev, PMBUS_F_TON_MAX,
		dev->out_state == PMBUS_OUT_RISING && ton_max != 0 && dev->state_ms > ton_max &&
			dev->vout_mv < word(dev, PMBUS_W_VOUT_UV_FAULT),
		dt);
	if (dev->out_state == PMBUS_OUT_FALLING && toff_max != 0 && dev->state_ms > toff_max &&
	    dev->vout_mv > dev->fall_from_mv / 8) {
		latch(dev, PMBUS_S_VOUT, PMBUS_SV_TOFF_MAX_WARN);
	}

	/* Output current and power. */
	bool oc = dev->output_on && demand > word(dev, PMBUS_W_IOUT_OC_FAULT);

	if (dev->output_on && dev->iout_ma > word(dev, PMBUS_W_IOUT_OC_WARN)) {
		latch(dev, PMBUS_S_IOUT, PMBUS_SI_OC_WARN);
	}
	respond(dev, PMBUS_F_IOUT_OC, oc, dt);
	respond(dev, PMBUS_F_IOUT_OC_LV,
		dev->current_limited &&
			PMBUS_RSP(dev->cfg.response[PMBUS_F_IOUT_OC]) == PMBUS_RSP_IOC_CC_LV &&
			dev->vout_mv < word(dev, PMBUS_W_IOUT_OC_LV_FAULT),
		dt);

	bool op = dev->pout_mw > word(dev, PMBUS_W_POUT_OP_FAULT);

	if (!op && dev->pout_mw > word(dev, PMBUS_W_POUT_OP_WARN)) {
		latch(dev, PMBUS_S_IOUT, PMBUS_SI_POUT_OP_WARN);
	}
	respond(dev, PMBUS_F_POUT_OP, op, dt);

	/* Input. */
	bool vin_ov = dev->vin_mv > word(dev, PMBUS_W_VIN_OV_FAULT);
	bool vin_uv = dev->vin_mv < word(dev, PMBUS_W_VIN_UV_FAULT);
	bool iin_oc = dev->iin_ma > word(dev, PMBUS_W_IIN_OC_FAULT);

	if (!vin_ov && dev->vin_mv > word(dev, PMBUS_W_VIN_OV_WARN)) {
		latch(dev, PMBUS_S_INPUT, PMBUS_SIN_OV_WARN);
	}
	if (!vin_uv && dev->vin_mv < word(dev, PMBUS_W_VIN_UV_WARN)) {
		latch(dev, PMBUS_S_INPUT, PMBUS_SIN_UV_WARN);
	}
	if (dev->vin_low && commanded_on(dev)) {
		latch(dev, PMBUS_S_INPUT, PMBUS_SIN_OFF_LOW_IN);
	}
	if (!iin_oc && dev->iin_ma > word(dev, PMBUS_W_IIN_OC_WARN)) {
		latch(dev, PMBUS_S_INPUT, PMBUS_SIN_IIN_OC_WARN);
	}
	if (dev->pin_mw > word(dev, PMBUS_W_PIN_OP_WARN)) {
		latch(dev, PMBUS_S_INPUT, PMBUS_SIN_PIN_OP_WARN);
	}
	respond(dev, PMBUS_F_VIN_OV, vin_ov, dt);
	respond(dev, PMBUS_F_VIN_UV, vin_uv, dt);
	respond(dev, PMBUS_F_IIN_OC, iin_oc, dt);

	/* Temperature limits apply to the hottest (and coldest) valid sensor. */
	bool any_temp = false;
	int32_t hot = 0, cold = 0;

	if (dev->sensors.temp1_valid) {
		hot = cold = dev->sensors.temp1_mc;
		any_temp = true;
	}
	if (dev->sensors.temp2_valid) {
		if (!any_temp || dev->sensors.temp2_mc > hot) {
			hot = dev->sensors.temp2_mc;
		}
		if (!any_temp || dev->sensors.temp2_mc < cold) {
			cold = dev->sensors.temp2_mc;
		}
		any_temp = true;
	}
	bool ot = any_temp && hot > word(dev, PMBUS_W_OT_FAULT);
	bool ut = any_temp && cold < word(dev, PMBUS_W_UT_FAULT);

	if (any_temp && !ot && hot > word(dev, PMBUS_W_OT_WARN)) {
		latch(dev, PMBUS_S_TEMPERATURE, PMBUS_ST_OT_WARN);
	}
	if (any_temp && !ut && cold < word(dev, PMBUS_W_UT_WARN)) {
		latch(dev, PMBUS_S_TEMPERATURE, PMBUS_ST_UT_WARN);
	}
	respond(dev, PMBUS_F_OT, ot, dt);
	respond(dev, PMBUS_F_UT, ut, dt);
}

/* --- The fan --- */

static int32_t hottest(const struct pmbus_device *dev)
{
	int32_t t = FAN_CURVE_LO;

	if (dev->sensors.temp1_valid && dev->sensors.temp1_mc > t) {
		t = dev->sensors.temp1_mc;
	}
	if (dev->sensors.temp2_valid && dev->sensors.temp2_mc > t) {
		t = dev->sensors.temp2_mc;
	}
	return t;
}

/* The speed the fan should run at: the thermal curve, or FAN_COMMAND_1 if higher. */
static int32_t fan1_target_rpm(const struct pmbus_device *dev)
{
	int32_t t = hottest(dev);
	int64_t duty_milli; /* percent * 1000 */
	int32_t cmd = word(dev, PMBUS_W_FAN_COMMAND_1);
	int64_t cmd_rpm;
	int64_t rpm;

	if (!(dev->cfg.fan_config_12 & PMBUS_FAN1_INSTALLED)) {
		return 0;
	}
	if (t >= FAN_CURVE_HI) {
		duty_milli = 100000;
	} else {
		duty_milli = FAN_MIN_DUTY * 1000 +
			     (int64_t)(100 - FAN_MIN_DUTY) * (t - FAN_CURVE_LO) * 1000 /
				     (FAN_CURVE_HI - FAN_CURVE_LO);
	}
	rpm = duty_milli * PMBUS_DEVICE_FAN_MAX_RPM / 100000;
	if (dev->cfg.fan_config_12 & PMBUS_FAN1_RPM) {
		cmd_rpm = cmd / 1000;
	} else {
		cmd_rpm = (int64_t)cmd * PMBUS_DEVICE_FAN_MAX_RPM / 100000;
	}
	if (cmd_rpm > rpm) {
		rpm = cmd_rpm;
	}
	if (rpm > PMBUS_DEVICE_FAN_MAX_RPM) {
		rpm = PMBUS_DEVICE_FAN_MAX_RPM;
	}
	return (int32_t)rpm;
}

static void update_fan(struct pmbus_device *dev, uint32_t dt)
{
	int32_t target = fan1_target_rpm(dev);
	int32_t goal = dev->sim.fan1_failed ? 0 : target;

	/* First-order lag towards the goal; every step moves at least 1 RPM, so it arrives. */
	if (dt != 0 && goal != dev->fan1_rpm) {
		int32_t delta = (int32_t)((int64_t)(goal - dev->fan1_rpm) * dt / (FAN_TAU_MS + dt));

		if (delta == 0) {
			delta = (goal > dev->fan1_rpm) ? 1 : -1;
		}
		dev->fan1_rpm += delta;
	}
	if (target == 0) {
		dev->fan1_low_ms = 0;
		return;
	}
	if (dev->fan1_rpm * 2 < target) {
		dev->fan1_low_ms += dt;
	} else {
		dev->fan1_low_ms = 0;
	}
	if (dev->fan1_low_ms >= FAN_BAD_MS) {
		latch(dev, PMBUS_S_FANS_1_2,
		      (dev->fan1_rpm * 10 < target) ? PMBUS_SF_FAN1_FAULT : PMBUS_SF_FAN1_WARN);
	}
}

/* --- Putting it together --- */

static void evaluate(struct pmbus_device *dev, uint32_t dt)
{
	run_retry_timers(dev, dt);
	compute_electricals(dev); /* VIN hysteresis first: it decides whether we may run */
	step_output(dev, dt);
	compute_electricals(dev);
	evaluate_limits(dev, dt);
	/* A fault may have shut the output down, or an inhibit may have lifted. */
	step_output(dev, 0);
	compute_electricals(dev);
	update_fan(dev, dt);
}

void pmbus_device_update(struct pmbus_device *dev, const struct pmbus_sensors *sensors)
{
	if (sensors != NULL) {
		dev->sensors = *sensors;
	}
	evaluate(dev, 0);
}

/* Long gaps are cut into ticks, so retries and delays inside them all happen. */
#define MAX_TICK_MS 10

void pmbus_device_advance(struct pmbus_device *dev, uint32_t ms)
{
	do {
		uint32_t step = (ms > MAX_TICK_MS) ? MAX_TICK_MS : ms;

		evaluate(dev, step);
		ms -= step;
	} while (ms > 0);
}

void pmbus_device_set_control_pin(struct pmbus_device *dev, bool asserted)
{
	if (asserted && !dev->control_pin) {
		rearm(dev);
	}
	dev->control_pin = asserted;
	pmbus_device_update(dev, NULL);
}

void pmbus_device_store_done(struct pmbus_device *dev, bool ok)
{
	dev->busy = false;
	if (!ok) {
		latch(dev, PMBUS_S_CML, PMBUS_CML_MEMORY_FAULT);
	}
}

/* --- Command handlers --- */

static void put_word(uint8_t *buf, uint16_t w)
{
	buf[0] = (uint8_t)w;
	buf[1] = (uint8_t)(w >> 8);
}

static uint16_t get_word(const uint8_t *buf)
{
	return (uint16_t)(buf[0] | (buf[1] << 8));
}

static int word_index(uint8_t code)
{
	for (size_t i = 0; i < PMBUS_W_COUNT; i++) {
		if (word_defs[i].code == code) {
			return (int)i;
		}
	}
	return -1;
}

static int fault_index(uint8_t code)
{
	for (size_t i = 0; i < PMBUS_F_COUNT; i++) {
		if (fault_defs[i].code == code) {
			return (int)i;
		}
	}
	return -1;
}

static int status_index(uint8_t code)
{
	for (size_t i = 0; i < PMBUS_S_COUNT; i++) {
		if (status_codes[i] == code) {
			return (int)i;
		}
	}
	return -1;
}

static const struct word_def *rating(uint8_t code)
{
	for (size_t i = 0; i < ARRAY_LEN(rating_defs); i++) {
		if (rating_defs[i].code == code) {
			return &rating_defs[i];
		}
	}
	return NULL;
}

static int read_byte(void *ctx, uint8_t code, const uint8_t *in, size_t in_len, uint8_t *buf,
		     size_t max)
{
	struct pmbus_device *dev = ctx;
	int i;

	(void)in;
	(void)in_len;
	(void)max;

	switch (code) {
	case PMBUS_PAGE:
		buf[0] = 0;
		break;
	case PMBUS_OPERATION:
		buf[0] = dev->cfg.operation;
		break;
	case PMBUS_ON_OFF_CONFIG:
		buf[0] = dev->cfg.on_off_config;
		break;
	case PMBUS_WRITE_PROTECT:
		buf[0] = dev->cfg.write_protect;
		break;
	case PMBUS_CAPABILITY:
		buf[0] = PMBUS_CAP_PEC | PMBUS_CAP_400KHZ |
			 (dev->has_alert_pin ? PMBUS_CAP_SMBALERT : 0);
		break;
	case PMBUS_VOUT_MODE:
		buf[0] = PMBUS_DEVICE_VOUT_MODE;
		break;
	case PMBUS_FAN_CONFIG_12:
		buf[0] = dev->cfg.fan_config_12;
		break;
	case PMBUS_STATUS_BYTE:
		buf[0] = pmbus_device_status_byte(dev);
		break;
	case PMBUS_PMBUS_REVISION:
		buf[0] = PMBUS_REVISION_1_3;
		break;
	case PMBUS_MFR_SPECIFIC_02:
		buf[0] = dev->sim.fan1_failed ? 1 : 0;
		break;
	default:
		i = fault_index(code);
		if (i >= 0) {
			buf[0] = dev->cfg.response[i];
			break;
		}
		i = status_index(code);
		if (i < 0) {
			return -1;
		}
		buf[0] = dev->status[i];
		break;
	}
	return 1;
}

static int read_word(void *ctx, uint8_t code, const uint8_t *in, size_t in_len, uint8_t *buf,
		     size_t max)
{
	struct pmbus_device *dev = ctx;
	const struct word_def *r;
	uint16_t w;
	int i;

	(void)in;
	(void)in_len;
	(void)max;

	switch (code) {
	case PMBUS_STATUS_WORD:
		w = pmbus_device_status_word(dev);
		break;
	case PMBUS_READ_VIN:
		w = L(dev->vin_mv);
		break;
	case PMBUS_READ_IIN:
		w = L(dev->iin_ma);
		break;
	case PMBUS_READ_VOUT:
		w = V(dev->vout_mv);
		break;
	case PMBUS_READ_IOUT:
		w = L(dev->iout_ma);
		break;
	case PMBUS_READ_TEMPERATURE_1:
		w = L(dev->sensors.temp1_mc);
		break;
	case PMBUS_READ_TEMPERATURE_2:
		w = L(dev->sensors.temp2_mc);
		break;
	case PMBUS_READ_FAN_SPEED_1:
		w = L(dev->fan1_rpm * 1000);
		break;
	case PMBUS_READ_POUT:
		w = L(dev->pout_mw);
		break;
	case PMBUS_READ_PIN:
		w = L(dev->pin_mw);
		break;
	case PMBUS_MFR_SPECIFIC_00:
		w = L(dev->sim.load_ma);
		break;
	case PMBUS_MFR_SPECIFIC_01:
		w = L(dev->sim.vin_mv);
		break;
	default:
		r = rating(code);
		if (r != NULL) {
			w = encode(r->fmt, r->def);
			break;
		}
		i = word_index(code);
		if (i < 0) {
			return -1;
		}
		w = dev->cfg.words[i];
		break;
	}
	put_word(buf, w);
	return 2;
}

static int read_block(void *ctx, uint8_t code, const uint8_t *in, size_t in_len, uint8_t *buf,
		      size_t max)
{
	struct pmbus_device *dev = ctx;
	const void *src;
	size_t len;

	(void)in;
	(void)in_len;

	if (code >= PMBUS_MFR_ID && code <= PMBUS_MFR_SERIAL) {
		const struct pmbus_mfr_string *s = &dev->cfg.mfr[code - PMBUS_MFR_ID];

		src = s->s;
		len = s->len;
	} else if (code >= PMBUS_USER_DATA_00 && code <= PMBUS_USER_DATA_15) {
		const struct pmbus_user_data *u = &dev->cfg.user[code - PMBUS_USER_DATA_00];

		src = u->d;
		len = u->len;
	} else if (code == PMBUS_IC_DEVICE_ID) {
		src = ic_device_id;
		len = sizeof(ic_device_id) - 1;
	} else if (code == PMBUS_IC_DEVICE_REV) {
		src = ic_device_rev;
		len = sizeof(ic_device_rev) - 1;
	} else {
		return -1;
	}
	if (len > max) {
		return -1;
	}
	memcpy(buf, src, len);
	return (int)len;
}

/* SMBALERT_MASK: the host writes which status register, the device answers its mask. */
static int read_alert_mask(void *ctx, uint8_t code, const uint8_t *in, size_t in_len,
			   uint8_t *buf, size_t max)
{
	struct pmbus_device *dev = ctx;
	int i;

	(void)code;
	(void)max;

	if (in_len != 1) {
		return -1;
	}
	i = status_index(in[0]);
	if (i < 0) {
		return -1;
	}
	buf[0] = dev->cfg.alert_mask[i];
	return 1;
}

static const struct pmbus_cmd *lookup(void *ctx, uint8_t code);

static int read_query(void *ctx, uint8_t code, const uint8_t *in, size_t in_len, uint8_t *buf,
		      size_t max)
{
	const struct pmbus_cmd *cmd;

	(void)code;
	(void)max;

	if (in_len != 1) {
		return -1;
	}
	cmd = lookup(ctx, in[0]);
	buf[0] = 0;
	if (cmd != NULL) {
		buf[0] = PMBUS_QUERY_SUPPORTED | cmd->query;
		if (cmd->access & PMBUS_A_W) {
			buf[0] |= PMBUS_QUERY_WRITE;
		}
		if (cmd->access & PMBUS_A_R) {
			buf[0] |= PMBUS_QUERY_READ;
		}
	}
	return 1;
}

static void restore_config(struct pmbus_device *dev)
{
	struct pmbus_mfr_string serial = dev->cfg.mfr[PMBUS_MFR_SERIAL_STR];

	/* Restore in place: the config is too big to copy on an interrupt stack. */
	if (!dev->ops->restore(dev->ops_ctx, &dev->cfg) || !pmbus_device_config_valid(&dev->cfg)) {
		set_defaults(&dev->cfg);
		dev->cfg.mfr[PMBUS_MFR_SERIAL_STR] = serial;
	}
}

static int write_cmd(void *ctx, uint8_t code, const uint8_t *data, size_t len)
{
	struct pmbus_device *dev = ctx;
	int i;

	switch (code) {
	case PMBUS_PAGE:
		return (data[0] == 0 || data[0] == 0xFF) ? 0 : -1;
	case PMBUS_OPERATION:
		if (!(data[0] & PMBUS_OPERATION_ON)) {
			rearm(dev);
		}
		dev->cfg.operation = data[0];
		break;
	case PMBUS_ON_OFF_CONFIG:
		dev->cfg.on_off_config = data[0] & 0x1F;
		break;
	case PMBUS_CLEAR_FAULTS:
		pmbus_device_clear_faults(dev);
		return 0;
	case PMBUS_WRITE_PROTECT:
		if (data[0] != 0 && data[0] != PMBUS_WP_ALL && data[0] != PMBUS_WP_OP &&
		    data[0] != PMBUS_WP_VOUT_CMD) {
			return -1;
		}
		dev->cfg.write_protect = data[0];
		return 0;
	case PMBUS_STORE_DEFAULT_ALL:
		if (dev->busy) {
			return -1; /* one store at a time */
		}
		dev->busy = true;
		dev->ops->store(dev->ops_ctx, &dev->cfg);
		return 0;
	case PMBUS_RESTORE_DEFAULT_ALL:
		if (dev->busy) {
			return -1;
		}
		restore_config(dev);
		rearm(dev);
		break;
	case PMBUS_SMBALERT_MASK:
		i = status_index(data[0]);
		if (i < 0) {
			return -1;
		}
		dev->cfg.alert_mask[i] = data[1];
		release_alert_if_clear(dev);
		return 0;
	case PMBUS_FAN_CONFIG_12:
		if (data[0] & 0x0F) {
			return -1; /* there is no fan 2 */
		}
		dev->cfg.fan_config_12 = data[0];
		break;
	case PMBUS_MFR_ID:
	case PMBUS_MFR_MODEL:
	case PMBUS_MFR_REVISION:
	case PMBUS_MFR_LOCATION:
	case PMBUS_MFR_DATE:
	case PMBUS_MFR_SERIAL:
		if (len > PMBUS_MFR_STR_MAX) {
			return -1;
		}
		dev->cfg.mfr[code - PMBUS_MFR_ID].len = (uint8_t)len;
		memcpy(dev->cfg.mfr[code - PMBUS_MFR_ID].s, data, len);
		return 0;
	case PMBUS_MFR_SPECIFIC_00:
		dev->sim.load_ma = pmbus_linear11_decode(get_word(data));
		break;
	case PMBUS_MFR_SPECIFIC_01:
		dev->sim.vin_mv = pmbus_linear11_decode(get_word(data));
		break;
	case PMBUS_MFR_SPECIFIC_02:
		dev->sim.fan1_failed = (data[0] & 1) != 0;
		break;
	case PMBUS_MFR_SPECIFIC_03:
		/* Inject: low byte the status command, high byte the bits to latch. */
		i = status_index(data[0]);
		if (i < 0) {
			return -1;
		}
		latch(dev, (enum pmbus_status_reg)i, data[1]);
		return 0;
	default:
		if (code >= PMBUS_USER_DATA_00 && code <= PMBUS_USER_DATA_15) {
			if (len > PMBUS_USER_DATA_MAX) {
				return -1;
			}
			dev->cfg.user[code - PMBUS_USER_DATA_00].len = (uint8_t)len;
			memcpy(dev->cfg.user[code - PMBUS_USER_DATA_00].d, data, len);
			return 0;
		}
		i = status_index(code);
		if (i >= 0) {
			/* Writing 1 to a status bit clears it. */
			dev->status[i] &= (uint8_t)~data[0];
			release_alert_if_clear(dev);
			return 0;
		}
		i = fault_index(code);
		if (i >= 0) {
			dev->cfg.response[i] = data[0];
			break;
		}
		i = word_index(code);
		if (i < 0) {
			return -1;
		}
		dev->cfg.words[i] = get_word(data);
		break;
	}
	pmbus_device_update(dev, NULL);
	return 0;
}

#define RW  (PMBUS_A_R | PMBUS_A_W)
#define RO  PMBUS_A_R
#define WO  PMBUS_A_W
#define LIN PMBUS_QUERY_FMT_LINEAR
#define NN  PMBUS_QUERY_FMT_NONNUM

#define CMD_SEND(c)        {c, PMBUS_T_SEND_BYTE, WO, NN, NULL, write_cmd}
#define CMD_BYTE(c, a, q)  {c, PMBUS_T_BYTE, a, q, read_byte, write_cmd}
#define CMD_WORD(c, a, q)  {c, PMBUS_T_WORD, a, q, read_word, write_cmd}
#define CMD_BLOCK(c, a)    {c, PMBUS_T_BLOCK, a, NN, read_block, write_cmd}

static const struct pmbus_cmd commands[] = {
	CMD_BYTE(PMBUS_PAGE, RW, NN),
	CMD_BYTE(PMBUS_OPERATION, RW, NN),
	CMD_BYTE(PMBUS_ON_OFF_CONFIG, RW, NN),
	CMD_SEND(PMBUS_CLEAR_FAULTS),
	CMD_BYTE(PMBUS_WRITE_PROTECT, RW, NN),
	CMD_SEND(PMBUS_STORE_DEFAULT_ALL),
	CMD_SEND(PMBUS_RESTORE_DEFAULT_ALL),
	CMD_BYTE(PMBUS_CAPABILITY, RO, NN),
	{PMBUS_QUERY, PMBUS_T_BLOCK_PROC, RO, NN, read_query, NULL},
	{PMBUS_SMBALERT_MASK, PMBUS_T_WORD_PROC, RW, NN, read_alert_mask, write_cmd},
	CMD_BYTE(PMBUS_VOUT_MODE, RO, NN),
	CMD_WORD(PMBUS_VOUT_COMMAND, RW, LIN),
	CMD_WORD(PMBUS_VOUT_MAX, RW, LIN),
	CMD_WORD(PMBUS_VOUT_MARGIN_HIGH, RW, LIN),
	CMD_WORD(PMBUS_VOUT_MARGIN_LOW, RW, LIN),
	CMD_WORD(PMBUS_VOUT_MIN, RW, LIN),
	CMD_WORD(PMBUS_VIN_ON, RW, LIN),
	CMD_WORD(PMBUS_VIN_OFF, RW, LIN),
	CMD_BYTE(PMBUS_FAN_CONFIG_12, RW, NN),
	CMD_WORD(PMBUS_FAN_COMMAND_1, RW, LIN),
	CMD_WORD(PMBUS_VOUT_OV_FAULT_LIMIT, RW, LIN),
	CMD_BYTE(PMBUS_VOUT_OV_FAULT_RESPONSE, RW, NN),
	CMD_WORD(PMBUS_VOUT_OV_WARN_LIMIT, RW, LIN),
	CMD_WORD(PMBUS_VOUT_UV_WARN_LIMIT, RW, LIN),
	CMD_WORD(PMBUS_VOUT_UV_FAULT_LIMIT, RW, LIN),
	CMD_BYTE(PMBUS_VOUT_UV_FAULT_RESPONSE, RW, NN),
	CMD_WORD(PMBUS_IOUT_OC_FAULT_LIMIT, RW, LIN),
	CMD_BYTE(PMBUS_IOUT_OC_FAULT_RESPONSE, RW, NN),
	CMD_WORD(PMBUS_IOUT_OC_LV_FAULT_LIMIT, RW, LIN),
	CMD_BYTE(PMBUS_IOUT_OC_LV_FAULT_RESPONSE, RW, NN),
	CMD_WORD(PMBUS_IOUT_OC_WARN_LIMIT, RW, LIN),
	CMD_WORD(PMBUS_OT_FAULT_LIMIT, RW, LIN),
	CMD_BYTE(PMBUS_OT_FAULT_RESPONSE, RW, NN),
	CMD_WORD(PMBUS_OT_WARN_LIMIT, RW, LIN),
	CMD_WORD(PMBUS_UT_WARN_LIMIT, RW, LIN),
	CMD_WORD(PMBUS_UT_FAULT_LIMIT, RW, LIN),
	CMD_BYTE(PMBUS_UT_FAULT_RESPONSE, RW, NN),
	CMD_WORD(PMBUS_VIN_OV_FAULT_LIMIT, RW, LIN),
	CMD_BYTE(PMBUS_VIN_OV_FAULT_RESPONSE, RW, NN),
	CMD_WORD(PMBUS_VIN_OV_WARN_LIMIT, RW, LIN),
	CMD_WORD(PMBUS_VIN_UV_WARN_LIMIT, RW, LIN),
	CMD_WORD(PMBUS_VIN_UV_FAULT_LIMIT, RW, LIN),
	CMD_BYTE(PMBUS_VIN_UV_FAULT_RESPONSE, RW, NN),
	CMD_WORD(PMBUS_IIN_OC_FAULT_LIMIT, RW, LIN),
	CMD_BYTE(PMBUS_IIN_OC_FAULT_RESPONSE, RW, NN),
	CMD_WORD(PMBUS_IIN_OC_WARN_LIMIT, RW, LIN),
	CMD_WORD(PMBUS_POWER_GOOD_ON, RW, LIN),
	CMD_WORD(PMBUS_POWER_GOOD_OFF, RW, LIN),
	CMD_WORD(PMBUS_TON_DELAY, RW, LIN),
	CMD_WORD(PMBUS_TON_RISE, RW, LIN),
	CMD_WORD(PMBUS_TON_MAX_FAULT_LIMIT, RW, LIN),
	CMD_BYTE(PMBUS_TON_MAX_FAULT_RESPONSE, RW, NN),
	CMD_WORD(PMBUS_TOFF_DELAY, RW, LIN),
	CMD_WORD(PMBUS_TOFF_FALL, RW, LIN),
	CMD_WORD(PMBUS_TOFF_MAX_WARN_LIMIT, RW, LIN),
	CMD_WORD(PMBUS_POUT_OP_FAULT_LIMIT, RW, LIN),
	CMD_BYTE(PMBUS_POUT_OP_FAULT_RESPONSE, RW, NN),
	CMD_WORD(PMBUS_POUT_OP_WARN_LIMIT, RW, LIN),
	CMD_WORD(PMBUS_PIN_OP_WARN_LIMIT, RW, LIN),
	CMD_BYTE(PMBUS_STATUS_BYTE, RO, NN),
	CMD_WORD(PMBUS_STATUS_WORD, RO, NN),
	CMD_BYTE(PMBUS_STATUS_VOUT, RW, NN),
	CMD_BYTE(PMBUS_STATUS_IOUT, RW, NN),
	CMD_BYTE(PMBUS_STATUS_INPUT, RW, NN),
	CMD_BYTE(PMBUS_STATUS_TEMPERATURE, RW, NN),
	CMD_BYTE(PMBUS_STATUS_CML, RW, NN),
	CMD_BYTE(PMBUS_STATUS_OTHER, RW, NN),
	CMD_BYTE(PMBUS_STATUS_MFR_SPECIFIC, RW, NN),
	CMD_BYTE(PMBUS_STATUS_FANS_1_2, RW, NN),
	CMD_WORD(PMBUS_READ_VIN, RO, LIN),
	CMD_WORD(PMBUS_READ_IIN, RO, LIN),
	CMD_WORD(PMBUS_READ_VOUT, RO, LIN),
	CMD_WORD(PMBUS_READ_IOUT, RO, LIN),
	CMD_WORD(PMBUS_READ_TEMPERATURE_1, RO, LIN),
	CMD_WORD(PMBUS_READ_TEMPERATURE_2, RO, LIN),
	CMD_WORD(PMBUS_READ_FAN_SPEED_1, RO, LIN),
	CMD_WORD(PMBUS_READ_POUT, RO, LIN),
	CMD_WORD(PMBUS_READ_PIN, RO, LIN),
	CMD_BYTE(PMBUS_PMBUS_REVISION, RO, NN),
	CMD_BLOCK(PMBUS_MFR_ID, RW),
	CMD_BLOCK(PMBUS_MFR_MODEL, RW),
	CMD_BLOCK(PMBUS_MFR_REVISION, RW),
	CMD_BLOCK(PMBUS_MFR_LOCATION, RW),
	CMD_BLOCK(PMBUS_MFR_DATE, RW),
	CMD_BLOCK(PMBUS_MFR_SERIAL, RW),
	CMD_WORD(PMBUS_MFR_VIN_MIN, RO, LIN),
	CMD_WORD(PMBUS_MFR_VIN_MAX, RO, LIN),
	CMD_WORD(PMBUS_MFR_IIN_MAX, RO, LIN),
	CMD_WORD(PMBUS_MFR_PIN_MAX, RO, LIN),
	CMD_WORD(PMBUS_MFR_VOUT_MIN, RO, LIN),
	CMD_WORD(PMBUS_MFR_VOUT_MAX, RO, LIN),
	CMD_WORD(PMBUS_MFR_IOUT_MAX, RO, LIN),
	CMD_WORD(PMBUS_MFR_POUT_MAX, RO, LIN),
	CMD_BLOCK(PMBUS_IC_DEVICE_ID, RO),
	CMD_BLOCK(PMBUS_IC_DEVICE_REV, RO),
	CMD_BLOCK(0xB0, RW), CMD_BLOCK(0xB1, RW), CMD_BLOCK(0xB2, RW), CMD_BLOCK(0xB3, RW),
	CMD_BLOCK(0xB4, RW), CMD_BLOCK(0xB5, RW), CMD_BLOCK(0xB6, RW), CMD_BLOCK(0xB7, RW),
	CMD_BLOCK(0xB8, RW), CMD_BLOCK(0xB9, RW), CMD_BLOCK(0xBA, RW), CMD_BLOCK(0xBB, RW),
	CMD_BLOCK(0xBC, RW), CMD_BLOCK(0xBD, RW), CMD_BLOCK(0xBE, RW), CMD_BLOCK(0xBF, RW),
	CMD_WORD(PMBUS_MFR_MAX_TEMP_1, RO, LIN),
	CMD_WORD(PMBUS_MFR_MAX_TEMP_2, RO, LIN),
	/* Manufacturer-specific knobs for the simulation. */
	CMD_WORD(PMBUS_MFR_SPECIFIC_00, RW, LIN), /* load current */
	CMD_WORD(PMBUS_MFR_SPECIFIC_01, RW, LIN), /* input voltage */
	CMD_BYTE(PMBUS_MFR_SPECIFIC_02, RW, NN),  /* bit 0: fan 1 failed */
	{PMBUS_MFR_SPECIFIC_03, PMBUS_T_WORD, WO, NN, NULL, write_cmd}, /* inject status */
};

static const struct pmbus_cmd *lookup(void *ctx, uint8_t code)
{
	struct pmbus_device *dev = ctx;

	/* A temperature without a working sensor reads as unsupported. */
	if ((code == PMBUS_READ_TEMPERATURE_1 && !dev->sensors.temp1_valid) ||
	    (code == PMBUS_READ_TEMPERATURE_2 && !dev->sensors.temp2_valid)) {
		return NULL;
	}
	if (!dev->sim_knobs && code >= PMBUS_MFR_SPECIFIC_00 && code <= PMBUS_MFR_SPECIFIC_03) {
		return NULL;
	}
	for (size_t i = 0; i < ARRAY_LEN(commands); i++) {
		if (commands[i].code == code) {
			return &commands[i];
		}
	}
	return NULL;
}

static bool write_allowed(void *ctx, uint8_t code)
{
	struct pmbus_device *dev = ctx;
	uint8_t wp = dev->cfg.write_protect;

	/*
	 * CLEAR_FAULTS and PAGE stay writable at every level, so a host such as Linux, which
	 * clears faults after probing, keeps working on a write-protected device.
	 */
	if (code == PMBUS_WRITE_PROTECT || code == PMBUS_CLEAR_FAULTS || code == PMBUS_PAGE) {
		return true;
	}
	if (wp & PMBUS_WP_ALL) {
		return false;
	}
	if (wp & PMBUS_WP_OP) {
		return code == PMBUS_OPERATION;
	}
	if (wp & PMBUS_WP_VOUT_CMD) {
		return code == PMBUS_OPERATION || code == PMBUS_ON_OFF_CONFIG ||
		       code == PMBUS_VOUT_COMMAND;
	}
	return true;
}

static void cml_error(void *ctx, uint8_t bits)
{
	struct pmbus_device *dev = ctx;

	latch(dev, PMBUS_S_CML, bits);
}

static bool alert_response(void *ctx)
{
	struct pmbus_device *dev = ctx;

	if (!dev->alert_pending) {
		return false;
	}
	dev->alert_pending = false;
	dev->ops->set_alert(dev->ops_ctx, false);
	return true;
}

static const struct pmbus_target_ops target_ops = {
	.lookup = lookup,
	.write_allowed = write_allowed,
	.cml_error = cml_error,
	.alert_response = alert_response,
};

void pmbus_device_init(struct pmbus_device *dev, uint8_t addr, const struct pmbus_device_ops *ops,
		       void *ops_ctx, bool has_alert_pin, const char *serial)
{
	memset(dev, 0, sizeof(*dev));
	pmbus_target_init(&dev->target, addr, &target_ops, dev);
	dev->ops = ops;
	dev->ops_ctx = ops_ctx;
	dev->has_alert_pin = has_alert_pin;
	dev->control_pin = true;
	dev->sim_knobs = true;
	set_defaults(&dev->cfg);
	if (serial != NULL) {
		size_t n = strlen(serial);

		if (n > PMBUS_MFR_STR_MAX) {
			n = PMBUS_MFR_STR_MAX;
		}
		dev->cfg.mfr[PMBUS_MFR_SERIAL_STR].len = (uint8_t)n;
		memcpy(dev->cfg.mfr[PMBUS_MFR_SERIAL_STR].s, serial, n);
	}
	dev->sim.vin_mv = 54000;
	dev->sim.load_ma = 5000;
	dev->sim.eff_permille = 950;
	compute_electricals(dev);
	step_output(dev, 0);
	compute_electricals(dev);
	dev->fan1_rpm = fan1_target_rpm(dev); /* already spinning at power-on */
}

void pmbus_device_load_stored(struct pmbus_device *dev)
{
	struct pmbus_mfr_string serial = dev->cfg.mfr[PMBUS_MFR_SERIAL_STR];

	if (!dev->ops->restore(dev->ops_ctx, &dev->cfg) || !pmbus_device_config_valid(&dev->cfg)) {
		set_defaults(&dev->cfg);
		dev->cfg.mfr[PMBUS_MFR_SERIAL_STR] = serial;
	}
	compute_electricals(dev);
	step_output(dev, 0);
	compute_electricals(dev);
}
