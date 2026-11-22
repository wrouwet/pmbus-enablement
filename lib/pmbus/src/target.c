/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <pmbus/format.h>
#include <pmbus/pmbus.h>
#include <pmbus/target.h>

void pmbus_target_init(struct pmbus_target *t, uint8_t addr, const struct pmbus_target_ops *ops,
		       void *ctx)
{
	memset(t, 0, sizeof(*t));
	t->addr = addr;
	t->ops = ops;
	t->ctx = ctx;
}

static void reset(struct pmbus_target *t)
{
	t->state = PMBUS_TS_IDLE;
	t->read_seen = false;
	t->rx_overflow = false;
	t->rx_len = 0;
	t->tx_len = 0;
	t->tx_pos = 0;
}

static void cml_error(struct pmbus_target *t, uint8_t bits)
{
	if (bits & PMBUS_CML_PEC_FAILED) {
		t->n_pec_errors++;
	}
	t->ops->cml_error(t->ctx, bits);
}

/* The number of data bytes a write of this type carries, or -1 if it can't be known. */
static int write_data_len(const struct pmbus_cmd *cmd, const uint8_t *data, size_t len)
{
	switch (cmd->type) {
	case PMBUS_T_SEND_BYTE:
		return 0;
	case PMBUS_T_BYTE:
		return 1;
	case PMBUS_T_WORD:
	case PMBUS_T_WORD_PROC:
		return 2;
	case PMBUS_T_BLOCK:
		return (len >= 1) ? 1 + data[0] : -1;
	default:
		return -1;
	}
}

static void execute_write(struct pmbus_target *t)
{
	uint8_t code = t->rx[0];
	const uint8_t *data = &t->rx[1];
	size_t len = t->rx_len - 1;
	const struct pmbus_cmd *cmd = t->ops->lookup(t->ctx, code);
	int expected;
	int ret;

	if (cmd == NULL || !(cmd->access & PMBUS_A_W)) {
		cml_error(t, PMBUS_CML_INVALID_CMD);
		return;
	}
	if (t->rx_overflow) {
		cml_error(t, PMBUS_CML_INVALID_DATA);
		return;
	}

	expected = write_data_len(cmd, data, len);
	if (expected < 0 || (len != (size_t)expected && len != (size_t)expected + 1)) {
		cml_error(t, PMBUS_CML_INVALID_DATA);
		return;
	}
	if (len == (size_t)expected + 1) {
		uint8_t pec = pmbus_crc8_byte(0, (uint8_t)(t->addr << 1));

		pec = pmbus_crc8(pec, t->rx, t->rx_len - 1);
		if (pec != t->rx[t->rx_len - 1]) {
			cml_error(t, PMBUS_CML_PEC_FAILED);
			return;
		}
	}
	if (!t->ops->write_allowed(t->ctx, code)) {
		cml_error(t, PMBUS_CML_INVALID_CMD);
		return;
	}

	if (cmd->type == PMBUS_T_BLOCK) {
		ret = cmd->write(t->ctx, code, data + 1, data[0]);
	} else {
		ret = cmd->write(t->ctx, code, data, (size_t)expected);
	}
	if (ret < 0) {
		cml_error(t, PMBUS_CML_INVALID_DATA);
	}
}

/* Builds the reply to a read of the command the host just wrote. */
static void prepare_read(struct pmbus_target *t)
{
	uint8_t code = t->rx[0];
	const struct pmbus_cmd *cmd = t->ops->lookup(t->ctx, code);
	const uint8_t *in = NULL;
	size_t in_len = 0;
	uint8_t *out = t->tx;
	size_t max = 2;
	int n;

	t->tx_len = 0;

	if (cmd == NULL || !(cmd->access & PMBUS_A_R) || cmd->type == PMBUS_T_SEND_BYTE) {
		cml_error(t, PMBUS_CML_INVALID_CMD);
		return;
	}

	switch (cmd->type) {
	case PMBUS_T_BYTE:
		max = 1;
		break;
	case PMBUS_T_WORD:
		max = 2;
		break;
	case PMBUS_T_BLOCK_PROC:
	case PMBUS_T_WORD_PROC:
		if (t->rx_len < 2 || t->rx_len != 2 + t->rx[1]) {
			cml_error(t, PMBUS_CML_INVALID_DATA);
			return;
		}
		in = &t->rx[2];
		in_len = t->rx[1];
		/* fall through */
	case PMBUS_T_BLOCK:
		out = &t->tx[1];
		max = PMBUS_BLOCK_MAX;
		break;
	default:
		break;
	}
	if (cmd->type != PMBUS_T_BLOCK_PROC && cmd->type != PMBUS_T_WORD_PROC && t->rx_len != 1) {
		/* Data bytes before the repeated start, which no read protocol has. */
		cml_error(t, PMBUS_CML_INVALID_DATA);
		return;
	}

	n = cmd->read(t->ctx, code, in, in_len, out, max);
	if (n < 0 || (size_t)n > max) {
		cml_error(t, PMBUS_CML_INVALID_DATA);
		return;
	}
	if (cmd->type == PMBUS_T_BLOCK || cmd->type == PMBUS_T_BLOCK_PROC ||
	    cmd->type == PMBUS_T_WORD_PROC) {
		t->tx[0] = (uint8_t)n;
		t->tx_len = (uint8_t)(n + 1);
	} else {
		t->tx_len = (uint8_t)n;
	}

	/* The PEC covers every byte on the wire, both address bytes included. */
	uint8_t pec = pmbus_crc8_byte(0, (uint8_t)(t->addr << 1));

	pec = pmbus_crc8(pec, t->rx, t->rx_len);
	pec = pmbus_crc8_byte(pec, (uint8_t)((t->addr << 1) | 1));
	pec = pmbus_crc8(pec, t->tx, t->tx_len);
	t->tx[t->tx_len++] = pec;
}

void pmbus_target_write_start(struct pmbus_target *t)
{
	if (t->state != PMBUS_TS_IDLE) {
		/* A repeated start to this device again: finish what came before. */
		pmbus_target_stop(t);
	}
	t->state = PMBUS_TS_WRITE;
}

bool pmbus_target_write_byte(struct pmbus_target *t, uint8_t byte)
{
	if (t->state != PMBUS_TS_WRITE) {
		t->state = PMBUS_TS_WRITE;
	}
	if (t->rx_len >= sizeof(t->rx)) {
		t->rx_overflow = true;
		return false;
	}
	t->rx[t->rx_len++] = byte;
	return true;
}

uint8_t pmbus_target_read_start(struct pmbus_target *t)
{
	t->tx_len = 0;
	t->tx_pos = 0;

	/* A read with no command first (Receive Byte) is not a PMBus protocol: all ones. */
	if (t->state == PMBUS_TS_WRITE && t->rx_len >= 1) {
		prepare_read(t);
	}
	t->state = PMBUS_TS_READ;
	t->read_seen = true;
	return pmbus_target_read_byte(t);
}

uint8_t pmbus_target_ara_start(struct pmbus_target *t)
{
	if (t->state != PMBUS_TS_IDLE) {
		pmbus_target_stop(t);
	}
	t->tx_len = 0;
	t->tx_pos = 0;
	if (t->ops->alert_response(t->ctx)) {
		t->tx[0] = (uint8_t)(t->addr << 1);
		t->tx_len = 1;
	}
	t->state = PMBUS_TS_READ;
	t->read_seen = true;
	return pmbus_target_read_byte(t);
}

uint8_t pmbus_target_read_byte(struct pmbus_target *t)
{
	if (t->tx_pos < t->tx_len) {
		return t->tx[t->tx_pos++];
	}
	return 0xFF;
}

void pmbus_target_stop(struct pmbus_target *t)
{
	if (t->state == PMBUS_TS_IDLE) {
		return;
	}
	if (t->state == PMBUS_TS_WRITE && !t->read_seen && t->rx_len > 0) {
		execute_write(t);
	}
	t->n_transactions++;
	reset(t);
}

void pmbus_target_abort(struct pmbus_target *t)
{
	if (t->state != PMBUS_TS_IDLE) {
		t->n_aborts++;
	}
	reset(t);
}
